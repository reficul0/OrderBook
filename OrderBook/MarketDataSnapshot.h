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
	std::array<sorted_by_price_orders_t, Order::Type::_EnumElementsCount> orders{};
};

template<typename OrdersContainerT>
inline void MarketDataSnapshot::add_orders(OrdersContainerT const &orders_container)
{
	auto &orders_by_type = orders_container.template get<OrdersByType>();
	for (uint8_t order_type = 0; order_type < Order::Type::_EnumElementsCount; ++order_type)
	{
		auto const orders_iters_pair = orders_by_type.equal_range(order_type);
		if (orders_iters_pair.second == orders_iters_pair.first)
			return;
		for (auto current_order = orders_iters_pair.first; current_order != orders_iters_pair.second; ++current_order)
		{
			auto order_copy = *current_order;
			orders[order_type].emplace(std::move(order_copy));
		}
	}
}

#endif
