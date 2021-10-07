#pragma once

#ifndef MARKET_DATA_SNAPSHOT_H
#define MARKET_DATA_SNAPSHOT_H

#include "OrderBook.h"

struct MarketDataSnapshot
{
	template<typename OrdersPtrIterT>
	void add_orders(OrdersPtrIterT orders_begin, OrdersPtrIterT orders_end);

	template<typename OrdersPtrIterT>
	static std::unique_ptr<MarketDataSnapshot> create(OrdersPtrIterT orders_begin, OrdersPtrIterT orders_end);
	
	using sorted_by_price_orders_t = boost::multi_index::multi_index_container<
		OrderData,
		boost::multi_index::indexed_by<
			boost::multi_index::ordered_non_unique<
				boost::multi_index::tag<struct OrdersByPriceAsc>,
				boost::multi_index::const_mem_fun<OrderData, price_t, &OrderData::GetPrice>,
				std::less<price_t>
			>
		>
	>;

private:
	MarketDataSnapshot() noexcept
	{
	}

	std::array<sorted_by_price_orders_t, Order::Type::_EnumElementsCount> _orders{};
public:
	decltype(_orders) const& GetOrders() const
	{
		return _orders;
	}
};

template<typename OrdersPtrIterT>
inline void MarketDataSnapshot::add_orders(OrdersPtrIterT orders_begin, OrdersPtrIterT orders_end)
{
	for (auto current_iter = orders_begin; current_iter != orders_end; ++current_iter)
	{
		auto current_copy= **current_iter;
		_orders[(*current_iter)->GetType()].emplace(std::move(current_copy));
	}
}

template <typename OrdersPtrIterT>
std::unique_ptr<MarketDataSnapshot> MarketDataSnapshot::create(OrdersPtrIterT orders_begin, OrdersPtrIterT orders_end)
{
	auto snapshot = std::unique_ptr<MarketDataSnapshot>(new MarketDataSnapshot());
	snapshot->add_orders(orders_begin, orders_end);
	return std::move(snapshot);
}

#endif
