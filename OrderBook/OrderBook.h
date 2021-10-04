#pragma once

#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include "Order.h"

struct MarketDataSnapshot;

struct OrderData
{
	OrderData(order_id_t order_id, std::unique_ptr<Order> order) noexcept
		: order_id(order_id)
		, order(std::move(order))
	{}
	OrderData(OrderData &&other) noexcept
		: order_id(other.order_id)
		, order(std::move(other.order))
	{
	}
	OrderData& operator=(OrderData && other)
		noexcept(noexcept(
			std::make_unique<Order>(std::declval<Order::Type>(), std::declval<price_t>(), std::declval<size_t>())
			))
	{
		if (this != &other)
		{
			this->order_id = other.order_id;
			this->order = std::move(other.order);
		}
		return *this;
	}

	price_t GetPrice() const
	{
		return order->price;
	}
	typename std::underlying_type<decltype(Order::type)>::type GetType() const
	{
		return static_cast<typename std::underlying_type<decltype(Order::type)>::type>(order->type);
	}
	
	order_id_t order_id;
	std::unique_ptr<Order> order;
private:
	// Разрешаем копировать данные заявки только для создания снапшота
	friend struct MarketDataSnapshot;
	OrderData(OrderData const& other)
		noexcept(noexcept(
				std::make_unique<Order>(std::declval<Order::Type>(), std::declval<price_t>(), std::declval<size_t>())
			))
		: order_id(other.order_id)
		, order(std::make_unique<Order>(other.order->type, other.order->price, other.order->quantity))
	{
	}
};

class OrderBook
{
public:
	using orders_t = boost::multi_index::multi_index_container<
		OrderData,
		boost::multi_index::indexed_by<
			boost::multi_index::hashed_unique<
				boost::multi_index::tag<struct OrdersById>,
			    boost::multi_index::member<OrderData, decltype(OrderData::order_id), &OrderData::order_id>
			>,
			boost::multi_index::hashed_non_unique<
				boost::multi_index::tag<struct OrdersByType>,
				boost::multi_index::const_mem_fun<OrderData, decltype(std::declval<OrderData>().GetType()), &OrderData::GetType>
			>,
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
	order_id_t place(std::unique_ptr<Order>);
	/**
	 * \brief Отмена заявки
	 * \return Данные отменённой заявки. Если такой заявки не было, то boost::none.
	 */
	boost::optional<OrderData> cancel(order_id_t);
	/**
	 * \brief Получение данных заявки
	 * \return Данные заявки
	 */
	OrderData const& get_data(order_id_t);
	std::unique_ptr<MarketDataSnapshot> get_snapshot();

private:
	/**
	 * \brief Сведение заявок
	 */
	void _merge(OrderData &new_order);
	/**
	 * \brief Удавлетворена ли заявка
	 */
	static bool _is_order_satisfied(OrderData const &order);
	/**
	 * \brief Получить тип заявки, с которой можно провести слияние
	 * \param merge_with_me Тип заявки, которая будет сливаться
	 * \return Тип заявки, с которой можно провести слияние
	 */
	static Order::Type _get_order_type_for_merge_with(Order::Type merge_with_me);

	boost::shared_mutex mutable _mutex;
	// изменять только в контексте write lock-a мьютекса.
	order_id_t _id_counter = 0;
	orders_t _orders{};
};

#endif