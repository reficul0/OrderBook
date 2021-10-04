#include "pch.h"

#include "OrderBook.h"

#include <boost/thread/future.hpp>

#include "MarketDataSnapshot.h"

order_id_t OrderBook::place(std::unique_ptr<Order> order)
{
	// Приоритет при мёрже у заявки с наименьшим id.
	// Простенькая реализация для удовлетворения этой цели через врайт лок всего этапа мёржа и добавления в стакан.
	boost::unique_lock<boost::shared_mutex> write_lock(_orders_book_mutex);
	auto order_id = ++_id_counter;

	{
		boost::upgrade_lock<boost::shared_mutex> merging_orders_read_lock(_merging_orders_mutex);
		
		auto& merging_orders_by_id = _merging_orders.get<OrdersById>();
		auto merging_order_iter = merging_orders_by_id.find(order_id);
		assert(merging_order_iter == merging_orders_by_id.end());

		boost::upgrade_to_unique_lock<boost::shared_mutex> merging_orders_write_lock(merging_orders_read_lock);
		merging_order_iter = merging_orders_by_id.emplace_hint(merging_order_iter, OrderData(order_id, std::move(order)));
		assert(merging_order_iter != _merging_orders.end());
	}
	
	_merger.GetStrand().post([this, order_id]() mutable { _merge(order_id); });
	
	return order_id;
}

template<typename OrdersMultiIndexContainerT>
boost::optional<OrderData> cancel_impl(order_id_t id, boost::shared_mutex &container_mutex, OrdersMultiIndexContainerT &container)
{
	boost::optional<OrderData> ret_val = boost::none;
	boost::upgrade_lock<boost::shared_mutex> read_lock(container_mutex);
	
	auto& orders_by_id = container.template get<OrdersById>();
	auto const order_iter = orders_by_id.find(id);
	if (order_iter != orders_by_id.end())
	{
		boost::upgrade_to_unique_lock<boost::shared_mutex> write_lock(read_lock);
		
		if (orders_by_id.modify(order_iter, [&](OrderData& order) { ret_val = std::move(order); })) {
			orders_by_id.erase(order_iter);
		}
	}
	
	return std::move(ret_val);
}

boost::optional<OrderData> OrderBook::cancel(order_id_t id)
{
	if (auto book_order = cancel_impl(id, _orders_book_mutex, _orders_book))
		return std::move(book_order);
	
	if (auto merging_order = cancel_impl(id, _merging_orders_mutex, _merging_orders))
		return std::move(merging_order);

	return boost::none;
}

template<typename OrdersMultiIndexContainerT>
boost::optional<OrderData const&> get_data_impl(order_id_t id, boost::shared_mutex &container_mutex, OrdersMultiIndexContainerT &container)
{
	boost::shared_lock<boost::shared_mutex> read_lock(container_mutex);
	auto& orders_by_id = container.template get<OrdersById>();
	auto const order_iter = orders_by_id.find(id);
	if (order_iter != orders_by_id.end())
		return *order_iter;
	
	return boost::none;
}

OrderData const& OrderBook::get_data(order_id_t id)
{
	if (auto book_order = get_data_impl(id, _orders_book_mutex, _orders_book))
		if(_is_order_satisfied(*book_order) == false)
			return *book_order;

	if (auto merging_order = get_data_impl(id, _merging_orders_mutex, _merging_orders))
		if (_is_order_satisfied(*merging_order) == false)
			return *merging_order;
	
	throw std::logic_error("There is no order with same id");
}

void OrderBook::_merge(order_id_t id)
{
	// Сначала получим заявку, которую будем мёржить.
	boost::upgrade_lock<boost::shared_mutex> merging_orders_read_lock(_merging_orders_mutex);
	
	auto &merging_orders_by_id = _merging_orders.get<OrdersById>();
	auto merging_order_iter = merging_orders_by_id.find(id);
	assert(merging_order_iter != merging_orders_by_id.end());
	OrderData const &new_order = *merging_order_iter;

	// Мержим заявки из стакана с новоприбывшей.
	{
		boost::upgrade_lock<boost::shared_mutex> orders_read_lock(_orders_book_mutex);
		
		auto& orders_by_price_and_type = _orders_book.get<OrdersByPriceAndType>();
		// Мержить можем если пришедшая заявка - ask, тогда будем мёржить bid-ы, и наоборот.
		auto const order_type_that_can_be_merged = _get_order_type_for_merge_with((Order::Type)new_order.GetType());
		auto const key_of_merging_orders = boost::make_tuple(new_order.GetPrice(), order_type_that_can_be_merged);

		// Сначала получим все зявки, которые можно слить с новой заявкой ..
		auto orders_for_merge_iters_pair = orders_by_price_and_type.equal_range(key_of_merging_orders);
		// .. если таких нет ..
		if (orders_for_merge_iters_pair.first == orders_for_merge_iters_pair.second)
			// .. сливать больше нечего.
			return;

		std::list<order_id_t> satisfied_orders;
		while (true)
		{
			boost::this_thread::interruption_point();
			if (_is_order_satisfied(new_order))
				// .. сливать больше нечего.
				break;

			boost::upgrade_to_unique_lock<boost::shared_mutex> orders_write_lock(orders_read_lock);
			// Первой сливается заявка, которая была зарегистрирована раньше других.
			// При этом, если слияние займёт больше 1-й итерации, то учтём, что мы можем встретить уже удовлетворённые заявки.
			// Уже удовлетворённые не удаляем сразу, а удалим после мёржа, так эффективнее.
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
				{
					boost::upgrade_to_unique_lock<boost::shared_mutex> merging_orders_write_lock(merging_orders_read_lock);
					// .. сливаем её.
					auto const quantity = (std::min)(new_order.order->quantity, iter_to_merging_order_with_top_prioroty->order->quantity);
					new_order.order->quantity -= quantity;
					iter_to_merging_order_with_top_prioroty->order->quantity -= quantity;
				}

				if (_is_order_satisfied(*iter_to_merging_order_with_top_prioroty))
					satisfied_orders.emplace_back(iter_to_merging_order_with_top_prioroty->order_id);
			}
		}

		// Если есть удовлетворённые заявки ..
		if (satisfied_orders.empty())
			return;
		// .. удалим их из стакана.
		auto& orders_by_id = _orders_book.get<OrdersById>();
		{
			boost::upgrade_to_unique_lock<boost::shared_mutex> book_write_lock(orders_read_lock);
			for (auto satisfied_order_id : satisfied_orders)
			{
				boost::this_thread::interruption_point();
				orders_by_id.erase(satisfied_order_id);
			}

			boost::upgrade_to_unique_lock<boost::shared_mutex> merging_orders_write_lock(merging_orders_read_lock);
			// Если добавляемая заявка всё ещё не удовлетворена ..
			if (_is_order_satisfied(new_order) == false)
			{
				// .. добавим её в стакан ..
				// .. но перед этим удали её из списка сливаемых заявок.
				boost::optional<OrderData> merging_order_data;
				// Ничего не должно помешать нам удалить элемент.
				assert(merging_orders_by_id.modify(merging_order_iter, [&merging_order_data](OrderData& order) { merging_order_data = std::move(order); }));
				merging_orders_by_id.erase(merging_order_iter);
				
				auto new_order_iter = orders_by_id.find(merging_order_data->order_id);
				assert(new_order_iter == orders_by_id.end());
				
				new_order_iter = orders_by_id.emplace_hint(new_order_iter, std::move(*merging_order_data));
				assert(new_order_iter != orders_by_id.end());
			}
			else
				_merging_orders.erase(merging_order_iter);
		}
	}
}

bool OrderBook::_is_order_satisfied(OrderData const& order)
{
	return order.order->quantity == 0;
}

Order::Type OrderBook::_get_order_type_for_merge_with(Order::Type merge_with_me)
{
	BOOST_STATIC_ASSERT_MSG((size_t)Order::Type::_EnumElementsCount == 2, "На данный момент учитываются только Ask и Bid. При добавлении новых типов заявок, получение типа для мёржа надо переписать.");
	// Отталкиваемся от знания того, что типа сейчас всего два и один из Ask или Bid равен нулю, а второй не равен нулю.
	return (Order::Type)!merge_with_me;
}

std::unique_ptr<MarketDataSnapshot> OrderBook::get_snapshot()
{
	// todo: std::lock(book_read_lock, merging_orders_read_lock)
	// todo: отобразить в снапшоте информацию о merging_orders_read_lock
	boost::shared_lock<boost::shared_mutex> book_read_lock(_orders_book_mutex);
	return std::make_unique<MarketDataSnapshot>(_orders_book);
}