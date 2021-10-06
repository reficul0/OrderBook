#pragma once

#ifndef MARKET_DATA_SNAPSHOT_H
#define MARKET_DATA_SNAPSHOT_H

#include "OrderBook.h"

struct MarketDataSnapshot
{
	MarketDataSnapshot() = default;

	template<typename OrdersContainerT>
	void add_orders(OrdersContainerT const &orders);

	using sorted_by_price_orders_t = boost::multi_index::multi_index_container<
		OrderData,
		boost::multi_index::indexed_by<
			boost::multi_index::ordered_non_unique<
				boost::multi_index::tag<struct OrdersByPrice>,
				boost::multi_index::const_mem_fun<OrderData, price_t, &OrderData::GetPrice>,
				std::less<price_t>
			>
		>
	>;

private:
	std::array<sorted_by_price_orders_t, Order::Type::_EnumElementsCount> _orders{};
public:
	decltype(_orders) const& GetOrders() const
	{
		return _orders;
	}
};

template<typename OrdersContainerT>
inline void MarketDataSnapshot::add_orders(OrdersContainerT const &orders_container)
{
	for (auto const &order : orders_container)
	{
		auto order_copy = order;
		_orders[order.GetType()].emplace(std::move(order_copy));
	}
}

#endif
