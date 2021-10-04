#include "pch.h"

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

order_id_t OrderBook::place(std::unique_ptr<Order> order)
{
	// Приоритет при мёрже у заявки с наименьшим id.
	// Простенькая реализация для удовлетворения этой цели через врайт лок всего этапа мёржа и добавления в стакан.
	// TODO: Если будет критичным, то можно делать это решение пооптимальнее, а именно как минимум не лочить всё ридлоком.
	// TODO: Так же возможно перенести merge в отдельный поток.
	boost::unique_lock<boost::shared_mutex> write_lock(_mutex);
	auto order_id = ++_id_counter;
	
	auto &orders_by_id = _orders.get<OrdersById>();
	auto const element_iter = orders_by_id.find(order_id);
	assert(element_iter == orders_by_id.end());

	{
		OrderData order_data(order_id, std::move(order));
		_merge(order_data);
		if (order_data.order->quantity)
			auto emplacement_result = _orders.emplace_hint(element_iter, std::move(order_data));
	}
	
	return order_id;
}

boost::optional<OrderData> OrderBook::cancel(order_id_t id)
{
	boost::optional<OrderData> ret_val = boost::none;
	
	boost::upgrade_lock<boost::shared_mutex> read_lock(_mutex);
	
	auto& orders_by_id = _orders .get<OrdersById>();
	auto const order_iter = orders_by_id.find(id);
	if(order_iter != orders_by_id.end())
	{
		boost::upgrade_to_unique_lock<boost::shared_mutex> write_lock(read_lock);
		ret_val = std::move(order_iter.get_node()->value());
		orders_by_id.erase(order_iter);
	}
	
	return std::move(ret_val);
	
}

OrderData const& OrderBook::get_data(order_id_t id)
{
	boost::shared_lock<boost::shared_mutex> read_lock(_mutex);
	
	auto& orders_by_id = _orders.get<OrdersById>();
	auto const order_iter = orders_by_id.find(id);
	if (order_iter == orders_by_id.end())
		throw std::logic_error("There is no order with same id");
	
	return *order_iter;
}

void OrderBook::_merge(OrderData &new_order)
{
	auto& orders_by_price_and_type = _orders.get<OrdersByPriceAndType>();
	// Мержить можем если пришедшая заявка - ask, тогда будем мёржить bid-ы, и наоборот.
	auto const order_type_that_can_be_merged = _get_order_type_for_merge_with((Order::Type)new_order.GetType());
	auto const key_of_merging_orders =  boost::make_tuple(new_order.GetPrice(), order_type_that_can_be_merged);

	// Сначала получим все зявки, которые можно слить с новой заявкой ..
	auto orders_for_merge_iters_pair = orders_by_price_and_type.equal_range(key_of_merging_orders);
	// .. если таких нет ..
	if(orders_for_merge_iters_pair.first == orders_for_merge_iters_pair.second)
		// .. сливать больше нечего.
		return;
	
	std::list<order_id_t> satisfied_orders;
	while (true) 
	{
		if(_is_order_satisfied(new_order))
			// .. сливать больше нечего.
			break;

		// Первой сливается заявка, которая была зарегистрирована раньше других.
		// При этом, если слияние займёт больше 1-й итерации, то учтём, что мы можем встретить уже удовлетворённые заявки.
		// Уже удовлетворённые не удаляем сразу, а удалим после мёржа, так эффективнее.
		auto merging_order_with_top_prioroty = std::min_element(
			orders_for_merge_iters_pair.first,
			orders_for_merge_iters_pair.second,
			[](decltype(*orders_for_merge_iters_pair.first) lhs, decltype(*orders_for_merge_iters_pair.first) rhs)
			{
				return lhs.order_id < rhs.order_id && lhs.order->quantity;
			}
		);
		// Если заявка для слияния найдена ..
		if(merging_order_with_top_prioroty != orders_for_merge_iters_pair.second && merging_order_with_top_prioroty->order->quantity)
		{
			// .. сливаем её.
			auto const quantity = (std::min)(new_order.order->quantity, merging_order_with_top_prioroty->order->quantity);
			new_order.order->quantity -= quantity;
			merging_order_with_top_prioroty->order->quantity -= quantity;
			
			if (_is_order_satisfied(*merging_order_with_top_prioroty))
				satisfied_orders.emplace_back(merging_order_with_top_prioroty->order_id);
		}
	}

	// Если есть удовлетворённые заявки ..
	if (satisfied_orders.empty())
		return;
	
	// .. удалим их.
	auto& orders_by_id = _orders.get<OrdersById>();
	for(auto satisfied_order_id : satisfied_orders) 
		orders_by_id.erase(satisfied_order_id);
}

bool OrderBook::_is_order_satisfied(OrderData const& order)
{
	return order.order->quantity == 0;
}

Order::Type OrderBook::_get_order_type_for_merge_with(Order::Type merge_with_me)
{
	BOOST_STATIC_ASSERT_MSG((size_t)Order::Type::_EnumElementsCount == 2, "На данный момент учитываются только Ask и Bid. При увеличении количества типов заявок, получение типа для мёржа надо переписать.");
	// Отталкиваемся от знания того, что типа сейчас всего два и один из Ask или Bid равен нулю, а второй не равен нулю.
	return (Order::Type)!merge_with_me;
}

std::unique_ptr<MarketDataSnapshot> OrderBook::get_snapshot()
{
	boost::shared_lock<boost::shared_mutex> read_lock(_mutex);
	return std::make_unique<MarketDataSnapshot>(_orders);
}

