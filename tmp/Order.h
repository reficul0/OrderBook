#pragma once

#ifndef ORDER_H
#define ORDER_H

#include <cinttypes>

using order_id_t = size_t;
using price_t = double;

enum class OrderType : uint8_t
{
	Ask = 0,
	Bid = 1,
	_EnumElementsCount
};

struct Order
{
	OrderType type;
	price_t price;
	size_t quantity;

	Order(OrderType type, price_t price, size_t quantity) noexcept
		: type(type)
		, price(price)
		, quantity(quantity)
	{
	}
};

#endif
