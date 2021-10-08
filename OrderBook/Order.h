#pragma once

#ifndef ORDER_H
#define ORDER_H

#include <cinttypes>

using order_id_t = boost::multiprecision::uint256_t;
using price_t = double;
using quantity_t = size_t;

struct Order
{
	enum Type : uint8_t
	{
		Ask = 0,
		Bid = 1,
		_EnumElementsCount
	};
	
	Type type;
	price_t price;
	quantity_t quantity;

	Order(Type type, price_t price, quantity_t quantity) noexcept
		: type(type)
		, price(price)
		, quantity(quantity)
	{
	}
};

#endif
