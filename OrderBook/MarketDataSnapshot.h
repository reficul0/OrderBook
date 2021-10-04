#pragma once

#ifndef MARKET_DATA_SNAPSHOT_H
#define MARKET_DATA_SNAPSHOT_H

#include "OrderBook.h"

struct MarketDataSnapshot
{
	explicit MarketDataSnapshot(OrderBook::orders_t const &src_orders)
	{
		// TODO: Если будет критичным, то ускорить.
		auto& orders_by_type = src_orders.get<OrdersByType>();
		for (uint8_t order_type = 0; order_type < Order::Type::_EnumElementsCount; ++order_type)
		{
			auto const orders_iters_pair = orders_by_type.equal_range(order_type);
			if (orders_iters_pair.second == orders_iters_pair.first)
				continue;
			_copy(orders[order_type], orders_iters_pair.first, orders_iters_pair.second);
		}
	}

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

	std::array<sorted_by_price_orders_t, Order::Type::_EnumElementsCount> orders;
private:
	template<typename IterT>
	void _copy(sorted_by_price_orders_t &dst, IterT src_begin, IterT src_end)
	{
		for (; src_begin != src_end; ++src_begin)
		{
			auto order_copy = *src_begin;
			dst.emplace(std::move(order_copy));
		}
	}
};

#endif