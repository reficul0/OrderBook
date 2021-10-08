#include "pch.h"

#define BOOST_TEST_MODULE OrderBookTests
#include <boost/chrono/ceil.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/thread/future.hpp>

#define IS_CI_BUILD // TODO: дефайнить это должна билд машина

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

class OrderBookTestWrapper : boost::noncopyable
{
public:
	/**
	 * \brief Постановка заявок
	 * \return id заявки
	 */
	order_id_t post(std::unique_ptr<Order> order)
	{
		auto order_id = _book.post(std::move(order));
		// даём время на мёрж 
		boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
		return order_id;
	}
	/**
	 * \brief Отмена заявки
	 * \return Данные отменённой заявки. Если такой заявки не было(либо уже нет, то есть её отменили), то boost::none.
	 */
	boost::optional<OrderData> cancel(order_id_t const &id)
	{
		return _book.cancel(id);
	}
	/**
	 * \brief Получение данных заявки
	 * \return Данные заявки
	 */
	OrderData get_data(order_id_t const &id) const
	{
		return _book.get_data(id);
	}
	/**
	 * \brief Получить срез данных, которые есть в стакане на момент вызова.
	 * \return Срез.
	 */
	std::unique_ptr<MarketDataSnapshot> get_snapshot() const
	{
		return _book.get_snapshot();
	}

	OrderBookTestWrapper() = default;
	~OrderBookTestWrapper() = default;
private:
	OrderBook _book;
};

BOOST_AUTO_TEST_SUITE(OrderBookSpecialCases)

BOOST_AUTO_TEST_CASE(EmptyBookSnapshotGetting)
{
	OrderBook book;
	auto const &snapshot = book.get_snapshot();
	BOOST_TEST(snapshot->GetOrders()[Order::Type::Ask].empty());
	BOOST_TEST(snapshot->GetOrders()[Order::Type::Bid].empty());
}

BOOST_AUTO_TEST_CASE(EmptyBookOrderDataGetting)
{
	OrderBook book;
	BOOST_CHECK_THROW(book.get_data(0), std::exception);
}

BOOST_AUTO_TEST_CASE(EmptyBookOrderCancelling)
{
	OrderBook book;
	auto order_data = book.cancel(0);
	BOOST_TEST((false == (bool)order_data));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(OrderBookEssentialOperations)

BOOST_AUTO_TEST_CASE(OrderPostingAndCancelling)
{
	OrderBookTestWrapper book;

	auto order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	
	auto const &order_data = book.get_data(order_id);
	BOOST_TEST(order_data.GetPrice() == 4);
	BOOST_TEST(order_data.GetQuantity() == 300);
	
	auto cancelled_order_data = book.cancel(order_id);
	BOOST_TEST(cancelled_order_data.is_initialized());
	BOOST_TEST(cancelled_order_data->GetPrice() == 4);
	BOOST_TEST(cancelled_order_data->GetQuantity() == 300);

	BOOST_CHECK_THROW(book.get_data(order_id), std::exception);
}

BOOST_AUTO_TEST_CASE(SignleOrderSnapshotGetting)
{
	OrderBookTestWrapper book;

	auto order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	
	auto const &snapshot = book.get_snapshot();

	auto &asks = snapshot->GetOrders()[Order::Type::Ask];
	BOOST_TEST(asks.size() == 1);
	
	auto &front_order = *asks.begin();
	
	BOOST_TEST(front_order.order_id == order_id);
	BOOST_TEST(front_order.GetPrice() == 4);
	BOOST_TEST(front_order.GetQuantity() == 300);
}

BOOST_AUTO_TEST_CASE(SignleOrderMerging, *boost::unit_test::timeout(1))
{
	OrderBookTestWrapper book;

	auto constexpr summary_quantity = 300;
	
	auto const ask_order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, summary_quantity));
	BOOST_TEST_PASSPOINT();
	
	book.post(std::make_unique<Order>(Order::Type::Bid, 4, summary_quantity - 1));
	BOOST_TEST_PASSPOINT();
	
	{
		auto const &snapshot = book.get_snapshot();

		auto &asks = snapshot->GetOrders()[Order::Type::Ask];
		// помещённый ask удовлетворён не полностью
		BOOST_TEST(asks.size() == 1);

		auto &bids = snapshot->GetOrders()[Order::Type::Bid];
		// а вот бида уже нет
		BOOST_TEST(bids.empty());
	}

	auto const &order_data = book.get_data(ask_order_id);
	// аск всё ещё не удовлетворён
	BOOST_TEST(order_data.GetQuantity() == 1);

	book.post(std::make_unique<Order>(Order::Type::Bid, 4, 1));
	// аск удовлетворён
	BOOST_CHECK_THROW(book.get_data(ask_order_id), std::exception);

	{
		auto const &snapshot = book.get_snapshot();

		auto &asks = snapshot->GetOrders()[Order::Type::Ask];
		// все вски удовлетворены
		BOOST_TEST(asks.empty());

		auto &bids = snapshot->GetOrders()[Order::Type::Bid];
		// все биды удовлетворены
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_CASE(TopPriorityOrderMerging, *boost::unit_test::timeout(1))
{
	OrderBookTestWrapper book;

	auto constexpr summary_quantity = 300;
	auto constexpr second_order_quantity = 1;
	auto constexpr top_priority_order_quantity = summary_quantity - second_order_quantity;
	
	auto const top_priority_order_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, top_priority_order_quantity));
	BOOST_TEST_PASSPOINT();
	auto const less_priority_order_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, second_order_quantity));
	BOOST_TEST_PASSPOINT();

	book.post(std::make_unique<Order>(Order::Type::Ask, 4, top_priority_order_quantity));
	// Должна быть слита самая приоритетная заявка ..
	BOOST_CHECK_THROW(book.get_data(top_priority_order_id), std::exception);
	// .. а менее приоритетная не должна.
	BOOST_CHECK_NO_THROW(book.get_data(less_priority_order_id));
}

BOOST_AUTO_TEST_CASE(TwoInARowOrdersMerging, *boost::unit_test::timeout(1))
{
	OrderBookTestWrapper book;

	auto constexpr summary_quantity = 300;
	auto constexpr second_order_quantity = 1;
	
	book.post(std::make_unique<Order>(Order::Type::Bid, 4, summary_quantity - second_order_quantity));
	BOOST_TEST_PASSPOINT();
	book.post(std::make_unique<Order>(Order::Type::Bid, 4, second_order_quantity));
	BOOST_TEST_PASSPOINT();
	book.post(std::make_unique<Order>(Order::Type::Ask, 4, summary_quantity));
	BOOST_TEST_PASSPOINT();

	{
		auto const &snapshot = book.get_snapshot();

		auto &asks = snapshot->GetOrders()[Order::Type::Ask];
		// помещённый ask удовлетворён полностью
		BOOST_TEST(asks.empty());

		auto &bids = snapshot->GetOrders()[Order::Type::Bid];
		// биды тоже все удовлетворены
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_CASE(OrderDataDoNotChangeWhenOrderChanged, *boost::unit_test::timeout(1))
{
	/* get_data должен возвращать копию данных, которая формируется в контексте критической секции, что обеспечивает защиту от гонок.
	 * если get_data возвращает ссылку(изменения в контейнере отображаются в возвращённом обхъекте),
	 * то возникает вероятность гонок, которую пользователь не сможет предотвратить(поскольку все инструменты синхронизации спрятаны от него).
	 */
	OrderBookTestWrapper book;

	auto constexpr bid_quantity = 300;
	auto constexpr ask_quantity = 1;

	auto bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, bid_quantity));
	BOOST_TEST_PASSPOINT();

	auto const &bid_data = book.get_data(bid_id);
	BOOST_TEST_PASSPOINT();

	BOOST_TEST(bid_data.GetQuantity() == bid_quantity);

	book.post(std::make_unique<Order>(Order::Type::Ask, 4, ask_quantity));
	BOOST_TEST_PASSPOINT();

	BOOST_TEST(bid_data.GetQuantity() == bid_quantity);
}

BOOST_AUTO_TEST_CASE(SnapshotDataDoNotChangeWhenOrderChanged, *boost::unit_test::timeout(1))
{
	OrderBookTestWrapper book;

	auto constexpr bid_quantity = 300;
	auto constexpr ask_quantity = 1;

	auto bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, bid_quantity));
	BOOST_TEST_PASSPOINT();

	auto snapshot = book.get_snapshot();
	auto const &snapshot_bid_orders = snapshot->GetOrders()[Order::Type::Bid];
	auto const bid_data_iter = std::find_if(snapshot_bid_orders.begin(), snapshot_bid_orders.end(),
		[bid_id](OrderData const &order)
		{
			return order.order_id == bid_id;
		}
	);

	BOOST_TEST_PASSPOINT();

	BOOST_TEST(bid_data_iter->GetQuantity() == bid_quantity);

	book.post(std::make_unique<Order>(Order::Type::Ask, 4, ask_quantity));
	BOOST_TEST_PASSPOINT();

	BOOST_TEST(bid_data_iter->GetQuantity() == bid_quantity);
}

BOOST_AUTO_TEST_CASE(SnapshotDataAreInAscPriceOrder, *boost::unit_test::timeout(1))
{
	OrderBookTestWrapper book;

	book.post(std::make_unique<Order>(Order::Type::Bid, 5, 300));
	book.post(std::make_unique<Order>(Order::Type::Bid, 1, 300));
	book.post(std::make_unique<Order>(Order::Type::Bid, 10, 300));
	BOOST_TEST_PASSPOINT();

	auto snapshot = book.get_snapshot();
	auto const &snapshot_bid_orders = snapshot->GetOrders()[Order::Type::Bid];
	BOOST_TEST(
		std::is_sorted(
			snapshot_bid_orders.begin(),
			snapshot_bid_orders.end(),
			[comparator = std::less<price_t>()](OrderData const &lhs, OrderData const &rhs)
			{
				return comparator(lhs.GetPrice(), rhs.GetPrice());
			}
		)
	);
	BOOST_TEST_PASSPOINT();

	book.post(std::make_unique<Order>(Order::Type::Ask, 3, 300));
	book.post(std::make_unique<Order>(Order::Type::Ask, 20, 300));
	book.post(std::make_unique<Order>(Order::Type::Ask, 10, 300));
	BOOST_TEST_PASSPOINT();

	auto const &snapshot_ask_orders = snapshot->GetOrders()[Order::Type::Ask];
	BOOST_TEST(
		std::is_sorted(
			snapshot_ask_orders.begin(),
			snapshot_ask_orders.end(),
			[comparator = std::less<price_t>()](OrderData const &lhs, OrderData const &rhs)
	{
		return comparator(lhs.GetPrice(), rhs.GetPrice());
	}
	)
	);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(OperationsOnSatisfactedOrders)

BOOST_AUTO_TEST_CASE(CancellingDataOfSatisfactedOrder, *boost::unit_test::timeout(1))
{
	OrderBookTestWrapper book;

	auto ask_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	auto bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, 300));
	BOOST_TEST_PASSPOINT();

	auto cancelled_ask = book.cancel(ask_id);
	BOOST_TEST((false == cancelled_ask.is_initialized()));

	auto cancelled_bid = book.cancel(ask_id);
	BOOST_TEST((false == cancelled_bid.is_initialized()));
}

BOOST_AUTO_TEST_CASE(GettingDataOfSatisfactedOrder, *boost::unit_test::timeout(1))
{
	OrderBookTestWrapper book;

	auto ask_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	auto bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, 300));
	BOOST_TEST_PASSPOINT();

	BOOST_CHECK_THROW(book.get_data(ask_id), std::exception);
	BOOST_CHECK_THROW(book.get_data(bid_id), std::exception);
}

BOOST_AUTO_TEST_CASE(LookupDataOfSatisfactedOrderInSnapsot, *boost::unit_test::timeout(1))
{
	OrderBookTestWrapper book;

	auto order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	book.post(std::make_unique<Order>(Order::Type::Bid, 4, 300));
	BOOST_TEST_PASSPOINT();

	auto snapshot = book.get_snapshot();
	BOOST_TEST(snapshot->GetOrders()[Order::Type::Ask].empty());
	BOOST_TEST(snapshot->GetOrders()[Order::Type::Bid].empty());
}

BOOST_AUTO_TEST_SUITE_END()

#ifdef IS_CI_BUILD

BOOST_AUTO_TEST_SUITE(OrderBookOperationsDuration)

/* Тесты выше и так не быстрые, но те, что ниже занимают ещё больше времени. Прогонять их каждый раз нет смысла, ибо их долго ждать.
 * То есть, если тесты долгие, то люди рано или поздно устанут их ждать и просто выключат.
 * Такие тесты есть смысл запускать только на билдмашине.
 */

void poster_thread(OrderBook &book)
{
	try
	{
		while (true)
		{
			auto constexpr max_quantity = 3000;
			for (size_t i = 0; i < max_quantity; ++i)
			{
				boost::this_thread::interruption_point();
				book.post(std::make_unique<Order>(Order::Type::Ask, 4, max_quantity - i));
			}

			// Бида пусть будут в обратном порядке, для того, чтобы мёрж был ещё дольше
			for (size_t i = 0; i < max_quantity; ++i)
			{
				boost::this_thread::interruption_point();
				book.post(std::make_unique<Order>(Order::Type::Bid, 4, i));
			}

		}
	}
	catch (const boost::thread_interrupted&)
	{
	}
}
void get_snapshot_thread(OrderBook &book)
{
	try
	{
		while (true)
		{
			boost::this_thread::interruption_point();
			auto snapshot = book.get_snapshot();
		}
	}
	catch (const boost::thread_interrupted&)
	{
	}
}
void get_data_thread(OrderBook &book)
{

	while (true)
	{
		try
		{
			boost::this_thread::interruption_point();
			/* Будем запрашивать всего 0-й индекс. Заявка с таким индексом найдётся единожды, в остальные разы будет бросать исключения.
			 * Но даже бросая исключения лукап будет производиться для всех значений. А значит и мьтексы будут лочиться все.
			 *
			 */
			auto const &data = book.get_data(0);
		}
		catch (const boost::thread_interrupted&)
		{
			break;
		}
		catch (const std::exception&)
		{
			// get_data бросает исключение, поскольку заявки с индексом 0 нет, ибо заявка с таким индексом только самая первая.
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////// Deadlock тесты для открытого api стакана.
////// Алгоритм тестирования эвристический. Считаем, что если за время нагрузки в тесте не было дедлока, то его и нет.
//////		То есть если тест не повис(и кончился таймаутом), то всё хорошо.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOST_AUTO_TEST_CASE(GetSnapshotWhenPostDeadlockTest, *boost::unit_test::timeout(30))
{
	OrderBook book;

	boost::thread_group threads;

	threads.create_thread(boost::bind(&poster_thread, std::ref(book)));
	threads.create_thread(boost::bind(&poster_thread, std::ref(book)));
	
	threads.create_thread(boost::bind(&get_snapshot_thread, std::ref(book)));
	threads.create_thread(boost::bind(&get_snapshot_thread, std::ref(book)));

	boost::this_thread::sleep_for(boost::chrono::seconds(10));
	threads.interrupt_all();
	threads.join_all();
}

BOOST_AUTO_TEST_CASE(GetDataWhenPostDeadlockTest, *boost::unit_test::timeout(30))
{
	OrderBook book;

	boost::thread_group threads;

	threads.create_thread(boost::bind(&poster_thread, std::ref(book)));
	threads.create_thread(boost::bind(&poster_thread, std::ref(book)));

	threads.create_thread(boost::bind(&get_data_thread, std::ref(book)));
	threads.create_thread(boost::bind(&get_data_thread, std::ref(book)));

	boost::this_thread::sleep_for(boost::chrono::seconds(10));
	threads.interrupt_all();
	threads.join_all();
}

BOOST_AUTO_TEST_CASE(GetDataAndGetSnapshotWhenPostDeadlockTest, *boost::unit_test::timeout(40))
{
	OrderBook book;

	boost::thread_group threads;

	threads.create_thread(boost::bind(&poster_thread, std::ref(book)));
	threads.create_thread(boost::bind(&poster_thread, std::ref(book)));

	threads.create_thread(boost::bind(&get_data_thread, std::ref(book)));
	threads.create_thread(boost::bind(&get_data_thread, std::ref(book)));

	threads.create_thread(boost::bind(&get_snapshot_thread, std::ref(book)));
	threads.create_thread(boost::bind(&get_snapshot_thread, std::ref(book)));

	boost::this_thread::sleep_for(boost::chrono::seconds(20));
	threads.interrupt_all();
	threads.join_all();
}

/////////////////////////////////////////////////////////////////////////////////////////////////
////// Проверка операций на то, что они будут прерваны по первому велению юзера
/////////////////////////////////////////////////////////////////////////////////////////////////
BOOST_AUTO_TEST_CASE(OrdersMergingInterruptionTest, *boost::unit_test::timeout(10))
{
	std::unique_ptr<OrderBook> book = std::make_unique<OrderBook>();

	// Сначала нагрузим побольше заявок для мёржа.

	auto constexpr max_quantity = 3000;
	for (size_t i = 0; i < max_quantity; ++i)
		book->post(std::make_unique<Order>(Order::Type::Ask, 4, max_quantity - i));

	BOOST_TEST_PASSPOINT();

	// Бида пусть будут в обратном порядке, для того, чтобы мёрж был ещё дольше
	for (size_t i = 0; i < max_quantity; ++i)
		book->post(std::make_unique<Order>(Order::Type::Bid, 4, i));
	
	boost::thread merge_interruption_thread(
		[&book] { book.reset(); }
	);
	// мёрж должен быть прерван при разрушении объекта стакана
	BOOST_TEST( merge_interruption_thread.try_join_for(boost::chrono::seconds(1)) );
}

/////////////////////////////////////////////////////////////////////////////////////////////////
////// "Стресс" тесты
/////////////////////////////////////////////////////////////////////////////////////////////////
BOOST_AUTO_TEST_CASE(ALotOfOrdersMergeStressTest, *boost::unit_test::timeout(10))
{
	OrderBook book;

	auto constexpr summary_quantity = 5000;
	for (size_t i = 0; i < summary_quantity; ++i)
		book.post(std::make_unique<Order>(Order::Type::Ask, 4, summary_quantity - i));

	order_id_t last_bid_id;
	for (size_t i = 0; i < summary_quantity; ++i)
		last_bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, summary_quantity - i));

	// Ждём пока закончится мёрж
	while (true)
	{
		try
		{
			book.get_data(last_bid_id);
		}
		catch (const std::exception&)
		{
			// если последней заявки нет, то мёрж закончен.
			break;
		}
		// Даём время завершить мёрж.
		boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
	}

	// Все заявки должны быть удовлетворены.
	{
		auto const &snapshot = book.get_snapshot();

		auto &asks = snapshot->GetOrders()[Order::Type::Ask];
		BOOST_TEST(asks.empty());

		auto &bids = snapshot->GetOrders()[Order::Type::Bid];
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_CASE(ALotOfOrdersMergeStressTest_WithBidsQuantityReversedOrder, *boost::unit_test::timeout(10))
{
	OrderBook book;

	auto constexpr max_quantity = 5000;
	size_t summary_quantity = 0;
	for (size_t i = 0; i < max_quantity; ++i)
	{
		book.post(std::make_unique<Order>(Order::Type::Ask, 4, max_quantity - i));
		summary_quantity += max_quantity - i;
	}

	BOOST_TEST_PASSPOINT();
	
	order_id_t last_bid_id;
	for (size_t i = 0; i < max_quantity; ++i)
	{
		last_bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, i));
		summary_quantity -= i;
	}

	// Ждём пока закончится мёрж
	while (true)
	{
		try
		{
			book.get_data(last_bid_id);
		}
		catch (const std::exception&)
		{
			// если последней заявки нет, то мёрж закончен.
			break;
		}
		// Даём время завершить мёрж.
		boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
	}
	
	BOOST_TEST_PASSPOINT();

	// Суммарное quantity у ask`ов больше, чем у bid`ов
	{
		auto const &snapshot = book.get_snapshot();

		auto &asks = snapshot->GetOrders()[Order::Type::Ask];
		size_t acks_quantity_summary = 0;
		for (auto &ask : asks)
			acks_quantity_summary += ask.GetQuantity();

		BOOST_TEST(summary_quantity == acks_quantity_summary);

		auto &bids = snapshot->GetOrders()[Order::Type::Bid];
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_SUITE_END()

#endif // IS_CI_BUILD