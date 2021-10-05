#include "pch.h"

#define BOOST_TEST_MODULE OrderBookTests
#include <boost/chrono/ceil.hpp>
#include <boost/test/included/unit_test.hpp>

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

void place_for_merge(OrderBook &book, std::unique_ptr<Order> order, boost::chrono::milliseconds wait_for_merge_timeout = boost::chrono::milliseconds(100))
{
	book.place(std::move(order));
	// даём время на мёрж 
	boost::this_thread::sleep_for(wait_for_merge_timeout);
}

BOOST_AUTO_TEST_CASE(SingleOrderPlacementAndCancellingTest)
{
	OrderBook book;

	auto order_id = book.place(std::make_unique<Order>(Order::Type::Ask, 4, 300));
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

	auto order_id = book.place(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	auto const &snapshot = book.get_snapshot();

	auto &ascks = snapshot->orders[Order::Type::Ask];
	BOOST_TEST(ascks.size() == 1);
	
	auto &front_order = *ascks.begin();
	
	BOOST_TEST(front_order.order_id == order_id);
	BOOST_TEST(front_order.GetPrice() == 4);
	BOOST_TEST(front_order.order->quantity == 300);
}

BOOST_AUTO_TEST_CASE(SingleOrderMergingTest)
{
	OrderBook book;

	auto constexpr summary_quantity = 300;
	
	auto const ask_order_id = book.place(std::make_unique<Order>(Order::Type::Ask, 4, summary_quantity));
	place_for_merge(book, std::make_unique<Order>(Order::Type::Bid, 4, summary_quantity - 1));
	
	{
		auto const &snapshot = book.get_snapshot();

		auto &ascks = snapshot->orders[Order::Type::Ask];
		// помещённый ask удовлетворён не полностью
		BOOST_TEST(ascks.size() == 1);

		auto &bids = snapshot->orders[Order::Type::Bid];
		// а вот бида уже нет
		BOOST_TEST(bids.empty());
	}

	auto const &order_data = book.get_data(ask_order_id);
	// аск всё ещё не удовлетворён
	BOOST_TEST(order_data.order->quantity == 1);

	place_for_merge(book, std::make_unique<Order>(Order::Type::Bid, 4, 1));
	// аск удовлетворён
	BOOST_CHECK_THROW(book.get_data(ask_order_id), std::exception);

	{
		auto const &snapshot = book.get_snapshot();

		auto &ascks = snapshot->orders[Order::Type::Ask];
		// все вски удовлетворены
		BOOST_TEST(ascks.empty());

		auto &bids = snapshot->orders[Order::Type::Bid];
		// все биды удовлетворены
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_CASE(TopPriorityOrderMergeTest)
{
	OrderBook book;

	auto constexpr summary_quantity = 300;
	auto constexpr second_order_quantity = 1;
	auto constexpr top_priority_order_quantity = summary_quantity - second_order_quantity;
	
	auto const top_priority_order_id = book.place(std::make_unique<Order>(Order::Type::Bid, 4, top_priority_order_quantity));
	auto const less_priority_order_id = book.place(std::make_unique<Order>(Order::Type::Bid, 4, second_order_quantity));

	place_for_merge(book, std::make_unique<Order>(Order::Type::Ask, 4, top_priority_order_quantity));
	// Должна быть слита самая приоритетная заявка ..
	BOOST_CHECK_THROW(book.get_data(top_priority_order_id), std::exception);
	// .. а менее приоритетная не должна.
	BOOST_CHECK_NO_THROW(book.get_data(less_priority_order_id));
}

BOOST_AUTO_TEST_CASE(TwoInARowOrdersMergingTest)
{
	OrderBook book;

	auto constexpr summary_quantity = 300;
	auto constexpr second_order_quantity = 1;
	
	book.place(std::make_unique<Order>(Order::Type::Bid, 4, summary_quantity - second_order_quantity));
	book.place(std::make_unique<Order>(Order::Type::Bid, 4, second_order_quantity));
	place_for_merge(book, std::make_unique<Order>(Order::Type::Ask, 4, summary_quantity));

	{
		auto const &snapshot = book.get_snapshot();

		auto &ascks = snapshot->orders[Order::Type::Ask];
		// помещённый ask удовлетворён полностью
		BOOST_TEST(ascks.empty());

		auto &bids = snapshot->orders[Order::Type::Bid];
		// биды тоже все удовлетворены
		BOOST_TEST(bids.empty());
	}
}

BOOST_AUTO_TEST_CASE(StressTest)
{
	OrderBook book;

	auto constexpr summary_quantity = 100000;
	for(size_t i = 0; i < summary_quantity; ++i)
		book.place(std::make_unique<Order>(Order::Type::Ask, 4, summary_quantity - i));
	
	for (size_t i = 0; i < summary_quantity; ++i)
		book.place(std::make_unique<Order>(Order::Type::Bid, 4, summary_quantity - i));

	boost::this_thread::sleep_for(boost::chrono::seconds(5));
}