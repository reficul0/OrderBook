#include "pch.h"

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

class OrderBook::Impl
{
public:
	Impl() = default;
	~Impl() = default;

	order_id_t post(std::unique_ptr<Order> order)
	{
		_merge(std::make_shared<OrderData>(_id_counter, std::move(order)));
		return _id_counter++;
	}
	
	OrderData::ptr_t cancel(order_id_t id)
	{
		boost::optional<OrderData::ptr_t> ret_val = boost::none;

		auto& orders_by_id = _book.get<OrdersById>();
		auto const order_iter = orders_by_id.find(id);
		if (order_iter != orders_by_id.end())
		{
			auto order = std::move(order_iter.get_node()->value());
			orders_by_id.erase(order_iter);
			return std::move(order);
		}
		return{};
	}

	OrderData::ptr_t const& get_data(order_id_t id) const
	{
		auto& orders_by_id = _book.get<OrdersById>();
		auto const order_iter = orders_by_id.find(id);
		if (order_iter != orders_by_id.end())
			return *order_iter;

		throw std::logic_error("There is no order with same id");
	}
	
	std::unique_ptr<MarketDataSnapshot> get_snapshot() const
	{
		return MarketDataSnapshot::create(_book.begin(), _book.end());
	}
private:
	/**
	 * \brief Сведение заявок.
	 */
	void _merge(OrderData::ptr_t new_order)
	{
		// Мержим заявки из стакана с новоприбывшей.
		auto& orders_by_price_and_type = _book.get<OrdersByPriceAndType>();
		// Мержить можем если пришедшая заявка - ask, тогда будем мёржить bid-ы, и наоборот.
		auto const order_type_that_can_be_merged = _get_order_type_for_merge_with((Order::Type)new_order->GetType());
		auto const key_of_merging_orders = boost::make_tuple(new_order->GetPrice(), order_type_that_can_be_merged);

		// Сначала получим все зявки, которые можно слить с новоприбывшей ..
		auto orders_for_merge_iters_pair = orders_by_price_and_type.equal_range(key_of_merging_orders);

		std::list<order_id_t> satisfied_orders_from_book;
		auto update_book_orders_after_merge = [&book_orders = _book, &new_order, &satisfied_orders_from_book]
		{
			auto &orders_by_id = book_orders.get<OrdersById>();

			// Если в стакане после мёржа есть удовлетворённые заявки ..
			if (satisfied_orders_from_book.empty() == false)
			{
				// .. то удалим их из стакана ..
				for (auto satisfied_order_id : satisfied_orders_from_book)
					orders_by_id.erase(satisfied_order_id);
			}

			// Если заявку надо добавить в стакан(она не удовлетворена после мёржа) ..
			if (new_order->GetQuantity() != 0)
			{
				// .. добавим её в стакан.
				auto new_order_in_book_iter = orders_by_id.find(new_order->order_id);
				assert(new_order_in_book_iter == orders_by_id.end());
				
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
				// Если сливаемая заявка удовлетворена ..
				if (new_order->GetQuantity() == 0)
					// .. то сливать больше нечего.
					break;

				// Первой сливается заявка, которая была зарегистрирована раньше других.
				// При этом, если слияние займёт больше 1й итерации, то учтём, что мы можем встретить уже удовлетворённые заявки.
				// Уже удовлетворённые не удаляем здесь по одной, а удалим после мёржа все сразу.
				auto iter_to_merging_order_with_top_prioroty = std::min_element(
					orders_for_merge_iters_pair.first,
					orders_for_merge_iters_pair.second,
					[](decltype(*orders_for_merge_iters_pair.first) lhs, decltype(*orders_for_merge_iters_pair.first) rhs)
					{
						return lhs->order_id < rhs->order_id
							&& _is_order_satisfied(lhs) == false;
					}
				);
				auto &merging_order_with_top_priority = *iter_to_merging_order_with_top_prioroty;
				// Если заявка для слияния найдена ..
				if (iter_to_merging_order_with_top_prioroty != orders_for_merge_iters_pair.second
					// .. и она не удовлетворена ..
					&& _is_order_satisfied(merging_order_with_top_priority) == false
				) {
					auto &new_order_quantity = new_order->GetQuantity();
					auto &merging_order_with_top_priority_quantity = merging_order_with_top_priority->GetQuantity();
					// .. сливаем её.
					auto const quantity = (std::min)(new_order_quantity, merging_order_with_top_priority_quantity);
					new_order_quantity -= quantity;
					merging_order_with_top_priority_quantity -= quantity;
					// Если после мёржа заявка из стакана удовлетворена ..
					if (_is_order_satisfied(merging_order_with_top_priority))
						// .. отметим, что её надо удалить.
						satisfied_orders_from_book.emplace_back(merging_order_with_top_priority->order_id);
				}
				else // Иначе заявок для слияния больше нет.
					break;
			}
		}
		update_book_orders_after_merge();
	}
	/**
	 * \brief Удавлетворена ли заявка.
	 */
	static bool _is_order_satisfied(OrderData::ptr_t const &order)
	{
		return order->GetQuantity() == 0;
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

	orders_book_t _book{};
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

OrderData::ptr_t OrderBook::cancel(order_id_t id)
{
	return _impl->cancel(id);
}

OrderData::ptr_t const& OrderBook::get_data(order_id_t id) const
{
	return _impl->get_data(id);
}

std::unique_ptr<MarketDataSnapshot> OrderBook::get_snapshot() const
{
	return _impl->get_snapshot();
}