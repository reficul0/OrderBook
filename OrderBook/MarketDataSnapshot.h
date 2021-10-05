#pragma once

#ifndef MARKET_DATA_SNAPSHOT_H
#define MARKET_DATA_SNAPSHOT_H

#include "OrderBook.h"

struct MarketDataSnapshot
{
	explicit MarketDataSnapshot(
		OrderBook::orders_book_t const &book_orders, boost::shared_mutex &book_mutex, 
		OrderBook::buffered_orders_t const &buffered_orders, boost::shared_mutex &buffered_orders_mutex
	) {
		// TODO: Если будет критичным, то ускорить.
		_copy(book_orders, book_mutex);
		_copy(buffered_orders, buffered_orders_mutex);
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
	void _copy_to(sorted_by_price_orders_t &dst, IterT src_begin, IterT src_end)
	{
		for (; src_begin != src_end; ++src_begin)
		{
			auto order_copy = *src_begin;
			dst.emplace(std::move(order_copy));
		}
	}
	template<typename SrcOrdersContainerT>
	void _copy_orders_of_type(SrcOrdersContainerT const &src, uint8_t order_type)
	{
		auto const orders_iters_pair = src.equal_range(order_type);
		if (orders_iters_pair.second != orders_iters_pair.first)
		{
			_copy_to(orders[order_type], orders_iters_pair.first, orders_iters_pair.second);
		}
	}
	
	template<typename OrdersContainerT>
	void _copy(OrdersContainerT &container, boost::shared_mutex &container_mutex)
	{
		boost::shared_lock<boost::shared_mutex> container_read_lock(container_mutex);
		auto &orders_by_type = container.template get<OrdersByType>();
		for (uint8_t order_type = 0; order_type < Order::Type::_EnumElementsCount; ++order_type)
			_copy_orders_of_type(orders_by_type, order_type);
	}
};

#endif