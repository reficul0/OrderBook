#include "pch.h"

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

namespace details
{
	namespace
	{
		template<typename OrdersMultiIndexContainerT>
		boost::optional<OrderData> cancel(order_id_t const &id, boost::shared_mutex &container_mutex, OrdersMultiIndexContainerT &container)
		{
			boost::optional<OrderData> ret_val = boost::none;
			boost::upgrade_lock<boost::shared_mutex> read_lock(container_mutex);

			auto& orders_by_id = container.template get<OrdersById>();
			auto const order_iter = orders_by_id.find(id);
			if (order_iter != orders_by_id.end())
			{
				boost::upgrade_to_unique_lock<boost::shared_mutex> write_lock(read_lock);
				ret_val = std::move(order_iter.get_node()->value());
				orders_by_id.erase(order_iter);
			}

			return std::move(ret_val);
		}

		template<typename OrdersMultiIndexContainerT>
		boost::optional<OrderData const&> get_data(order_id_t const &id, boost::shared_mutex &container_mutex, OrdersMultiIndexContainerT &container)
		{
			boost::shared_lock<boost::shared_mutex> read_lock(container_mutex);
			auto& orders_by_id = container.template get<OrdersById>();
			auto const order_iter = orders_by_id.find(id);
			if (order_iter != orders_by_id.end())
				return *order_iter;

			return boost::none;
		}

		template<typename ContainerT, typename MutexT>
		struct ContainerWithSynchronization
		{
			MutexT mutable mutex;
			// \warning Изменять только в контексте write lock-a \ref{mutex}
			ContainerT container{};
		};
	}
}

class OrderBook::Impl
{
public:
	Impl()
	{
		_orders_merger.StartTasksExecution();
	}
	~Impl()
	{
		_orders_merger.StopTasksExecution();
	}

	order_id_t post(std::unique_ptr<Order> order)
	{
		boost::unique_lock<boost::shared_mutex> merging_orders_read_lock(_merging.mutex);
		/* \warning Не будем лочить \ref{_id_counter} отдельно, ибо здесь всёравно гуляет не больше 1 потока, поскольку это контекст write lock-a.
		 *		И \ref{_id_counter} изменяется только здесь. В связи со всем этим дополнительной синхронизации не надо.
		 */
		auto order_id = ++_id_counter;

		auto& merging_orders_by_id = _merging.container.get<OrdersById>();
		auto merging_order_iter = merging_orders_by_id.find(order_id);
		assert(merging_order_iter == merging_orders_by_id.end());

		merging_order_iter = merging_orders_by_id.emplace_hint(merging_order_iter, OrderData(order_id, std::move(order)));
		assert(merging_order_iter != _merging.container.end());

		_orders_merger.GetService().post([this, order_id]() mutable { _merge(std::move(order_id)); return true; });
		
		return order_id;
	}
	
	boost::optional<OrderData> cancel(order_id_t const &id)
	{
		if (auto book_order = details::cancel(id, _book.mutex, _book.container))
			return std::move(book_order);

		if (auto merging_order = details::cancel(id, _merging.mutex, _merging.container))
			return std::move(merging_order);

		return boost::none;
	}

	OrderData const& get_data(order_id_t const &id) const
	{
		if (auto const book_order = details::get_data(id, _book.mutex, _book.container))
			if (_is_order_satisfied(*book_order) == false)
				return *book_order;

		if (auto const merging_order = details::get_data(id, _merging.mutex, _merging.container))
			if (_is_order_satisfied(*merging_order) == false)
				return *merging_order;

		throw std::logic_error("There is no order with same id");
	}
	
	std::unique_ptr<MarketDataSnapshot> get_snapshot() const
	{
		auto snapshot = std::make_unique<MarketDataSnapshot>();
		boost::shared_lock<boost::shared_mutex> book_read_lock(_book.mutex, boost::defer_lock);
		boost::shared_lock<boost::shared_mutex> merging_orders_read_lock(_merging.mutex, boost::defer_lock);
		std::lock(book_read_lock, merging_orders_read_lock);
		
		snapshot->add_orders(_book.container);
		snapshot->add_orders(_merging.container);

		return std::move(snapshot);
	}
private:
	/**
	 * \brief Сведение заявок.
	 */
	void _merge(order_id_t id)
	{
		// Сначала получим заявку, которую будем мёржить.
		OrderData new_order;
		{
			boost::upgrade_lock<boost::shared_mutex> merging_orders_read_lock(_merging.mutex);

			auto &merging_orders_by_id = _merging.container.get<OrdersById>();
			auto new_order_iter = merging_orders_by_id.find(id);
			if (new_order_iter == merging_orders_by_id.end())
			{
				// добавление заявки отменили.
				return;
			}
			
			boost::upgrade_to_unique_lock<boost::shared_mutex> merging_orders_write_lock(merging_orders_read_lock);
			
			new_order = std::move(new_order_iter.get_node()->value());
			merging_orders_by_id.erase(new_order_iter);
		}

		// Мержим заявки из стакана с новоприбывшей.
		{
			boost::upgrade_lock<boost::shared_mutex> orders_read_lock(_book.mutex);

			auto& orders_by_price_and_type = _book.container.get<OrdersByPriceAndType>();
			// Мержить можем если пришедшая заявка - ask, тогда будем мёржить bid-ы, и наоборот.
			auto const order_type_that_can_be_merged = _get_order_type_for_merge_with((Order::Type)new_order.GetType());
			auto const key_of_merging_orders = boost::make_tuple(new_order.GetPrice(), order_type_that_can_be_merged);

			// Сначала получим все зявки, которые можно слить с новоприбывшей ..
			auto orders_for_merge_iters_pair = orders_by_price_and_type.equal_range(key_of_merging_orders);

			std::list<order_id_t> satisfied_orders_from_book;
			auto update_book_orders_after_merge = [&book_orders = _book.container, &new_order, &satisfied_orders_from_book, &orders_read_lock]
			{
				auto &orders_by_id = book_orders.get<OrdersById>();

				// Если в стакане после мёржа есть удовлетворённые заявки ..
				if (satisfied_orders_from_book.empty() == false)
				{
					// .. то удалим их из стакана ..
					boost::upgrade_to_unique_lock<boost::shared_mutex> book_write_lock(orders_read_lock);
					for (auto satisfied_order_id : satisfied_orders_from_book)
					{
						boost::this_thread::interruption_point();
						orders_by_id.erase(satisfied_order_id);
					}
				}

				// Если заявку надо добавить в стакан(она не удовлетворена после мёржа) ..
				if (new_order.order->quantity != 0)
				{
					auto new_order_in_book_iter = orders_by_id.find(new_order.order_id);
					assert(new_order_in_book_iter == orders_by_id.end());

					boost::upgrade_to_unique_lock<boost::shared_mutex> book_write_lock(orders_read_lock);
					
					new_order_in_book_iter = orders_by_id.emplace_hint(new_order_in_book_iter, std::move(new_order));
					assert(new_order_in_book_iter != orders_by_id.end());
				}
			};
			// .. если есть с кем сливать ..
			if (orders_for_merge_iters_pair.first != orders_for_merge_iters_pair.second)
			{
				// .. то сливаем.
				while (true)
				{
					boost::this_thread::interruption_point();
					// Если сливаемая заявка удовлетворена ..
					if (new_order.order->quantity == 0)
						// .. то сливать больше нечего.
						break;

					boost::upgrade_to_unique_lock<boost::shared_mutex> orders_write_lock(orders_read_lock);
					// Первой сливается заявка, которая была зарегистрирована раньше других.
					// При этом, если слияние займёт больше 1й итерации, то учтём, что мы можем встретить уже удовлетворённые заявки.
					// Уже удовлетворённые не удаляем здесь по одной, а удалим после мёржа все сразу.
					auto iter_to_merging_order_with_top_prioroty = std::min_element(
						orders_for_merge_iters_pair.first,
						orders_for_merge_iters_pair.second,
						[](decltype(*orders_for_merge_iters_pair.first) lhs, decltype(*orders_for_merge_iters_pair.first) rhs)
						{
							return lhs.order_id < rhs.order_id
								&& _is_order_satisfied(lhs) == false;
						}
					);
					// Если заявка для слияния найдена ..
					if (iter_to_merging_order_with_top_prioroty != orders_for_merge_iters_pair.second
						// .. и она не удовлетворена ..
						&& _is_order_satisfied(*iter_to_merging_order_with_top_prioroty) == false
					) {
						// .. сливаем её.
						auto const quantity = (std::min)(new_order.order->quantity, iter_to_merging_order_with_top_prioroty->order->quantity);
						new_order.order->quantity -= quantity;
						iter_to_merging_order_with_top_prioroty->order->quantity -= quantity;
						// Если после мёржа заявка из стакана удовлетворена ..
						if (_is_order_satisfied(*iter_to_merging_order_with_top_prioroty))
							// .. отметим, что её надо удалить.
							satisfied_orders_from_book.emplace_back(iter_to_merging_order_with_top_prioroty->order_id);
					}
					else // Иначе заявок для слияния больше нет.
						break;
				}
			}
			update_book_orders_after_merge();
		}
	}
	/**
	 * \brief Удавлетворена ли заявка.
	 */
	static bool _is_order_satisfied(OrderData const &order)
	{
		return order.order->quantity == 0;
	}
	/**
	 * \brief Получить тип заявки, с которой можно провести слияние.
	 * \param merge_with_me Тип заявки, которая будет сливаться.
	 * \return Тип заявки, с которой можно провести слияние.
	 */
	static Order::Type _get_order_type_for_merge_with(Order::Type merge_with_me)
	{
		BOOST_STATIC_ASSERT_MSG((size_t)Order::Type::_EnumElementsCount == 2, "На данный момент учитываются только Ask и Bid. При добавлении новых типов заявок, получение типа для мёржа надо переписать.");
		// Отталкиваемся от знания того, что типа сейчас всего два и один из Ask или Bid равен нулю, а второй не равен нулю.
		return (Order::Type)!merge_with_me;
	}

	using buffered_orders_t = boost::multi_index::multi_index_container<
		OrderData,
		boost::multi_index::indexed_by<orders_by_id_hashed_unique_index_t>
	>;

	// \brief Исполнитель, который, по велению стакана, занимается сведением заявок в отдельном потоке.
	tools::async::TasksExecutor _orders_merger;

	details::ContainerWithSynchronization<orders_book_t, boost::shared_mutex> _book{};
	details::ContainerWithSynchronization<buffered_orders_t, boost::shared_mutex> _merging{};
	
	// \warning Изменять только в контексте write lock-a.
	order_id_t _id_counter = 0;
};

OrderBook::~OrderBook() = default;
OrderBook::OrderBook()
	: _impl(std::make_unique<Impl>())
{
}

order_id_t OrderBook::post(std::unique_ptr<Order> order)
{
	return _impl->post(std::move(order));
}

boost::optional<OrderData> OrderBook::cancel(order_id_t const &id)
{
	return _impl->cancel(id);
}

OrderData const& OrderBook::get_data(order_id_t const &id) const
{
	return _impl->get_data(id);
}

std::unique_ptr<MarketDataSnapshot> OrderBook::get_snapshot() const
{
	return _impl->get_snapshot();
}