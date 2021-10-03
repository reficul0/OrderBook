#include "pch.h"

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

order_id_t OrderBook::place(std::unique_ptr<Order> order)
{
	auto locked_orders = _orders.adopt_synchronize();
	boost::upgrade_lock<boost::shared_mutex> read_lock(*locked_orders.mutex());
	
	// todo: генерировать id, которого точно нет в контейнере
	auto order_id = locked_orders->size();

	OrderData order_data(order_id, std::move(order));
	auto &orders_by_id = locked_orders->get<OrdersById>();
	auto const element_iter = orders_by_id.find(order_data.order_id);
	assert(element_iter == orders_by_id.end());

	{
		_merge(order_data, locked_orders, read_lock);
		boost::upgrade_to_unique_lock<boost::shared_mutex> write_lock(read_lock);
		if (order_data.order->quantity)
			auto emplacement_result = locked_orders->emplace_hint(element_iter, std::move(order_data));
	}
	
	return order_id;
}

boost::optional<OrderData> OrderBook::cancel(order_id_t id)
{
	boost::optional<OrderData> ret_val = boost::none;
	
	auto locked_orders = _orders.adopt_synchronize();
	boost::upgrade_lock<boost::shared_mutex> read_lock(*locked_orders.mutex());
	
	auto& orders_by_id = locked_orders->get<OrdersById>();
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
	auto read_locked_orders = _orders.synchronize();
	
	auto& orders_by_id = read_locked_orders->get<OrdersById>();
	auto const order_iter = read_locked_orders->find(id);
	if (order_iter == read_locked_orders->end())
		throw std::exception("There is no order with same id");
	
	return *order_iter;
}

void OrderBook::_merge(OrderData &new_order, boost::unique_lock_ptr<orders_t, boost::shared_mutex> &locked_orders, boost::upgrade_lock<boost::shared_mutex> &orders_read_lock)
{
	auto& orders_by_price = locked_orders->get<OrdersByPriceAndType>();
	// под inverted подразумевается был inverted(ask) == bid и наоборот
	auto const inverted_type = (uint8_t)!new_order.GetType();

	// todo: сделать бы поменьше поисков, да и пооптимальее вобщем
	while (true) 
	{
		auto const inverted_type_orders_iters_pair = orders_by_price.equal_range(boost::make_tuple(new_order.GetPrice(), inverted_type));
		// пока есть что мёржить
		if (inverted_type_orders_iters_pair.first == inverted_type_orders_iters_pair.second
			|| new_order.order->quantity == 0)
			break;
		
		auto inverted_order_with_smallest_id = std::min_element(
			inverted_type_orders_iters_pair.first,
			inverted_type_orders_iters_pair.second,
			[](decltype(*inverted_type_orders_iters_pair.first) lhs, decltype(*inverted_type_orders_iters_pair.first) rhs)
			{
				return lhs.order_id < rhs.order_id;
			}
		);
		if(inverted_order_with_smallest_id != inverted_type_orders_iters_pair.second)
		{
			auto const quantity = (std::min)(new_order.order->quantity, inverted_order_with_smallest_id->order->quantity);
			new_order.order->quantity -= quantity;
			inverted_order_with_smallest_id->order->quantity -= quantity;

			if (inverted_order_with_smallest_id->order->quantity == 0)
			{
				boost::upgrade_to_unique_lock<boost::shared_mutex> write_lock(orders_read_lock);
				orders_by_price.erase(inverted_order_with_smallest_id);
			}
			else // new_order.order->quantity == 0
				break;
		}
	}
}

std::unique_ptr<MarketDataSnapshot> OrderBook::get_snapshot()
{
	auto read_locked_orders = _orders.synchronize();
	return std::make_unique<MarketDataSnapshot>(*read_locked_orders);
}