#include "pch.h"

#define BOOST_TEST_MODULE OrderBookTests
#include <boost/chrono/ceil.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/thread/future.hpp>

#define IS_CI_BUILD // TODO: дефайнить это должна билд машина

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

BOOST_AUTO_TEST_SUITE(OrderBookEssentialOperationsTesets)

void post_and_wait_for_merge(OrderBook &book, std::unique_ptr<Order> order, boost::chrono::milliseconds wait_for_merge_timeout = boost::chrono::milliseconds(100))
{
	book.post(std::move(order));
	// даём время на мёрж 
	boost::this_thread::sleep_for(wait_for_merge_timeout);
}

BOOST_AUTO_TEST_CASE(SingleOrderPostingAndCancellingTest)
{
	OrderBook book;

	auto order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	
	auto const &order_data = book.get_data(order_id);
	BOOST_TEST(order_data.GetPrice() == 4);
	BOOST_TEST(order_data.order->quantity == 300);
	
	auto cancelled_order_data = book.cancel(order_id);
	BOOST_TEST(cancelled_order_data.is_initialized());
	BOOST_TEST(cancelled_order_data->GetPrice() == 4);
	BOOST_TEST(cancelled_order_data->order->quantity == 300);

	BOOST_CHECK_THROW(book.get_data(order_id), std::exception);
}

BOOST_AUTO_TEST_CASE(SingleOrderSnapshotGettingTest)
{
	OrderBook book;

	auto order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	
	auto const &snapshot = book.get_snapshot();

	auto &asks = snapshot->orders[Order::Type::Ask];
	BOOST_TEST(asks.size() == 1);
	
	auto &front_order = *asks.begin();
	
	BOOST_TEST(front_order.order_id == order_id);
	BOOST_TEST(front_order.GetPrice() == 4);
	BOOST_TEST(front_order.order->quantity == 300);
}

BOOST_AUTO_TEST_CASE(SingleOrderMergingTest, *boost::unit_test::timeout(1))
{
	OrderBook book;

	auto constexpr summary_quantity = 300;
	
	auto const ask_order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, summary_quantity));
	BOOST_TEST_PASSPOINT();
	
	post_and_wait_for_merge(book, std::make_unique<Order>(Order::Type::Bid, 4, summary_quantity - 1));
	BOOST_TEST_PASSPOINT();
	
	{
		auto const &snapshot = book.get_snapshot();

		auto &asks = snapshot->orders[Order::Type::Ask];
		// помещённый ask удовлетворён не полностью
		BOOST_TEST(asks.size() == 1);

		auto &bids = snapshot->orders[Order::Type::Bid];
		// а вот бида уже нет
		BOOST_TEST(bids.empty());
	}

	auto const &order_data = book.get_data(ask_order_id);
	// аск всё ещё не удовлетворён
	BOOST_TEST(order_data.order->quantity == 1);

	post_and_wait_for_merge(book, std::make_unique<Order>(Order::Type::Bid, 4, 1));
	// аск удовлетворён
	BOOST_CHECK_THROW(book.get_data(ask_order_id), std::exception);

	{
		auto const &snapshot = book.get_snapshot();

		auto &asks = snapshot->orders[Order::Type::Ask];
		// все вски удовлетворены
		BOOST_TEST(asks.empty());

		auto &bids = snapshot->orders[Order::Type::Bid];
		// все биды удовлетворены
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_CASE(TopPriorityOrderMergeTest, *boost::unit_test::timeout(1))
{
	OrderBook book;

	auto constexpr summary_quantity = 300;
	auto constexpr second_order_quantity = 1;
	auto constexpr top_priority_order_quantity = summary_quantity - second_order_quantity;
	
	auto const top_priority_order_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, top_priority_order_quantity));
	BOOST_TEST_PASSPOINT();
	auto const less_priority_order_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, second_order_quantity));
	BOOST_TEST_PASSPOINT();

	post_and_wait_for_merge(book, std::make_unique<Order>(Order::Type::Ask, 4, top_priority_order_quantity));
	// Должна быть слита самая приоритетная заявка ..
	BOOST_CHECK_THROW(book.get_data(top_priority_order_id), std::exception);
	// .. а менее приоритетная не должна.
	BOOST_CHECK_NO_THROW(book.get_data(less_priority_order_id));
}

BOOST_AUTO_TEST_CASE(TwoInARowOrdersMergingTest, *boost::unit_test::timeout(1))
{
	OrderBook book;

	auto constexpr summary_quantity = 300;
	auto constexpr second_order_quantity = 1;
	
	book.post(std::make_unique<Order>(Order::Type::Bid, 4, summary_quantity - second_order_quantity));
	BOOST_TEST_PASSPOINT();
	book.post(std::make_unique<Order>(Order::Type::Bid, 4, second_order_quantity));
	BOOST_TEST_PASSPOINT();
	post_and_wait_for_merge(book, std::make_unique<Order>(Order::Type::Ask, 4, summary_quantity));
	BOOST_TEST_PASSPOINT();

	{
		auto const &snapshot = book.get_snapshot();

		auto &asks = snapshot->orders[Order::Type::Ask];
		// помещённый ask удовлетворён полностью
		BOOST_TEST(asks.empty());

		auto &bids = snapshot->orders[Order::Type::Bid];
		// биды тоже все удовлетворены
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_SUITE_END()

#ifdef IS_CI_BUILD

BOOST_AUTO_TEST_SUITE(OrderBookOperationsDurationTesets)

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

		auto &asks = snapshot->orders[Order::Type::Ask];
		BOOST_TEST(asks.empty());

		auto &bids = snapshot->orders[Order::Type::Bid];
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

		auto &asks = snapshot->orders[Order::Type::Ask];
		size_t acks_quantity_summary = 0;
		for (auto &ask : asks)
			acks_quantity_summary += ask.order->quantity;

		BOOST_TEST(summary_quantity == acks_quantity_summary);

		auto &bids = snapshot->orders[Order::Type::Bid];
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_SUITE_END()

#endif // IS_CI_BUILD