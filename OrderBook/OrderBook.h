#pragma once

#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include "async_tasks_executor.h"
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
			std::make_unique<Order>(std::declval<Order::Type>(), std::declval<price_t>(), std::declval<quantity_t>())
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
				std::make_unique<Order>(std::declval<Order::Type>(), std::declval<price_t>(), std::declval<quantity_t>())
			))
		: order_id(other.order_id)
		, order(std::make_unique<Order>(other.order->type, other.order->price, other.order->quantity))
	{
	}
};

/**
 * \brief Стакан заявок.
 * \warning Сведение заявок происходит в отдельном потоке.
 * \todo удостовериться в том, что тут нет дедлоков и гонок, таки мьютекса теперь два.
 */
class OrderBook
{
public:
	using orders_by_id_hashed_unique_index_t = boost::multi_index::hashed_unique<
		boost::multi_index::tag<struct OrdersById>,
		boost::multi_index::member<OrderData, decltype(OrderData::order_id), &OrderData::order_id>
	>;
	using orders_by_type_hashed_non_unique_index_t = boost::multi_index::hashed_non_unique<
		boost::multi_index::tag<struct OrdersByType>,
		boost::multi_index::const_mem_fun<OrderData, decltype(std::declval<OrderData>().GetType()), &OrderData::GetType>
	>;
	using orders_book_t = boost::multi_index::multi_index_container<
		OrderData,
		boost::multi_index::indexed_by<
			orders_by_id_hashed_unique_index_t,
			orders_by_type_hashed_non_unique_index_t,
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
	 * \return Данные отменённой заявки. Если такой заявки не было(либо уже нет), то boost::none.
	 */
	boost::optional<OrderData> cancel(order_id_t);
	/**
	 * \brief Получение данных заявки
	 * \return Данные заявки
	 */
	OrderData const& get_data(order_id_t);
	/**
	 * \brief Получить срез данных, которые есть в стакане на момент вызова.
	 * \return Срез.
	 */
	std::unique_ptr<MarketDataSnapshot> get_snapshot();

	OrderBook()
	{
		_merger.StartTasksExecution();
	}
	~OrderBook()
	{
		_merger.StopTasksExecution();
	}
private:
	/**
	 * \brief Сведение заявок.
	 */
	void _merge(order_id_t id);
	/**
	 * \brief Удавлетворена ли заявка.
	 */
	static bool _is_order_satisfied(OrderData const &order);
	/**
	 * \brief Получить тип заявки, с которой можно провести слияние.
	 * \param merge_with_me Тип заявки, которая будет сливаться.
	 * \return Тип заявки, с которой можно провести слияние.
	 */
	static Order::Type _get_order_type_for_merge_with(Order::Type merge_with_me);

	// Чтобы не выставлять информацию про буффер наружу. Но она нужна при формировании снапшота.
	friend struct MarketDataSnapshot;
	using buffered_orders_t = boost::multi_index::multi_index_container<
		OrderData,
		boost::multi_index::indexed_by<
			orders_by_id_hashed_unique_index_t,
			orders_by_type_hashed_non_unique_index_t
		>
	>;

	// \brief Исполнитель, который, по велению стакана, занимается сведением заявок в отдельном потоке.
	tools::async::TasksExecutor _merger;
	
	boost::shared_mutex mutable _orders_book_mutex;
	// \warning Изменять только в контексте write lock-a \ref{_orders_book_mutex}`а.
	// TODO: мб всётаки в атомик его(или синхронайзд)?
	order_id_t _id_counter = 0;
	// \warning Изменять только в контексте write lock-a \ref{_orders_book_mutex}`а.
	orders_book_t _orders_book{};

	boost::shared_mutex mutable _merging_orders_mutex;
	// \warning Изменять только в контексте write lock-a \ref{_merging_orders_mutex} `а.
	buffered_orders_t _merging_orders{};
};

#endif