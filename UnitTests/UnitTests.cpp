#include "pch.h"

#define BOOST_TEST_MODULE NumberTests
#include <boost/test/included/unit_test.hpp>

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

BOOST_AUTO_TEST_CASE(SingleOrderPlacementAndCancellingTest)
{
	OrderBook book;

	auto order_id = book.place(std::make_unique<Order>(OrderType::Ask, 4, 300));
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

	auto order_id = book.place(std::make_unique<Order>(OrderType::Ask, 4, 300));
	auto const &snapshot = book.get_snapshot();

	auto &ascks = snapshot->orders[(uint8_t)OrderType::Ask];
	BOOST_TEST(ascks.size() == 1);
	
	auto &front_order = *ascks.begin();
	
	BOOST_TEST(front_order.order_id == order_id);
	BOOST_TEST(front_order.GetPrice() == 4);
	BOOST_TEST(front_order.order->quantity == 300);
}

BOOST_AUTO_TEST_CASE(SingleOrderMergingTest)
{
	OrderBook book;

	auto const ask_order_id = book.place(std::make_unique<Order>(OrderType::Ask, 4, 300));
	book.place(std::make_unique<Order>(OrderType::Bid, 4, 299));
	
	{
		auto const &snapshot = book.get_snapshot();

		auto &ascks = snapshot->orders[(uint8_t)OrderType::Ask];
		// помещённый ask удовлетворён не полностью
		BOOST_TEST(ascks.size() == 1);

		auto &bids = snapshot->orders[(uint8_t)OrderType::Bid];
		// а вот бида уже нет
		BOOST_TEST(bids.empty());
	}

	auto const &order_data = book.get_data(ask_order_id);
	// аск всё ещё не удовлетворён
	BOOST_TEST(order_data.order->quantity == 1);

	book.place(std::make_unique<Order>(OrderType::Bid, 4, 1));
	// аск удовлетворён
	BOOST_CHECK_THROW(book.get_data(ask_order_id), std::exception);

	{
		auto const &snapshot = book.get_snapshot();

		auto &ascks = snapshot->orders[(uint8_t)OrderType::Ask];
		// все вски удовлетворены
		BOOST_TEST(ascks.empty());

		auto &bids = snapshot->orders[(uint8_t)OrderType::Bid];
		// все биды удовлетворены
		BOOST_TEST(bids.empty());
	}
}