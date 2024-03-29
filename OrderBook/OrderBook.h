﻿#pragma once

#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include "async_tasks_executor.h"
#include "Order.h"

struct MarketDataSnapshot;

struct OrderData
{
	OrderData() = default;
	OrderData(order_id_t order_id, std::unique_ptr<Order> order) noexcept
		: order_id(std::move(order_id))
		, _order(std::move(order))
	{}


	OrderData(OrderData const& other)
		noexcept(noexcept(
			std::make_unique<Order>(std::declval<Order::Type>(), std::declval<price_t>(), std::declval<quantity_t>())
			))
		: order_id(other.order_id)
		, _order(std::make_unique<Order>(other._order->type, other._order->price, other._order->quantity))
	{
	}
	
	OrderData(OrderData &&other) noexcept
		: order_id(std::move(other.order_id))
		, _order(std::move(other._order))
	{
	}
	OrderData& operator=(OrderData && other)
		noexcept(noexcept(
			std::make_unique<Order>(std::declval<Order::Type>(), std::declval<price_t>(), std::declval<quantity_t>())
			))
	{
		if (this != &other)
		{
			this->order_id = std::move(other.order_id);
			this->_order = std::move(other._order);
		}
		return *this;
	}

	price_t GetPrice() const
	{
		return _order->price;
	}
	typename std::underlying_type<decltype(Order::type)>::type GetType() const
	{
		return static_cast<typename std::underlying_type<decltype(Order::type)>::type>(_order->type);
	}

	quantity_t &GetQuantity() const
	{
		return _order->quantity;
	}
	
	order_id_t order_id;
private:

	std::unique_ptr<Order> _order;
};

/**
 * \brief Стакан заявок.
 * \warning Сведение заявок происходит в отдельном потоке.
 */
class OrderBook : boost::noncopyable
{
public:
	using orders_by_id_hashed_unique_index_t = boost::multi_index::hashed_unique<
		boost::multi_index::tag<struct OrdersById>,
		boost::multi_index::member<OrderData, decltype(OrderData::order_id), &OrderData::order_id>
	>;
	using orders_book_t = boost::multi_index::multi_index_container<
		OrderData,
		boost::multi_index::indexed_by<
			orders_by_id_hashed_unique_index_t,
			boost::multi_index::hashed_non_unique<
				boost::multi_index::tag<struct OrdersByPriceAndType>,
			    boost::multi_index::composite_key<
					OrderData,
					boost::multi_index::const_mem_fun<OrderData, price_t, &OrderData::GetPrice>,
					boost::multi_index::const_mem_fun<OrderData, decltype(std::declval<OrderData>().GetType()), &OrderData::GetType>
				>
			>
		>
	>;
	
	/**
	 * \brief Постановка заявок
	 * \return id заявки
	 */
	order_id_t post(std::unique_ptr<Order>);
	/**
	 * \brief Отмена заявки
	 * \return Данные отменённой заявки. Если такой заявки не было(либо уже нет, то есть её отменили), то boost::none.
	 */
	boost::optional<OrderData> cancel(order_id_t const &);
	/**
	 * \brief Получение данных заявки
	 * \return Данные заявки
	 */
	OrderData get_data(order_id_t const &) const;
	/**
	 * \brief Получить срез данных, которые есть в стакане на момент вызова.
	 * \return Срез.
	 */
	std::unique_ptr<MarketDataSnapshot> get_snapshot() const;

	OrderBook();
	~OrderBook();
private:
	class Impl;
	std::unique_ptr<Impl> _impl;
};

#endif