#include "pch.h"

#define BOOST_TEST_MODULE OrderBookTests
#include <boost/test/included/unit_test.hpp>

#include "OrderBook.h"
#include "MarketDataSnapshot.h"

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
	OrderBook book;

	auto order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();

	auto const &order_data = book.get_data(order_id);
	BOOST_TEST(order_data->GetPrice() == 4);
	BOOST_TEST(order_data->GetQuantity() == 300);

	auto cancelled_order_data = book.cancel(order_id);
	BOOST_TEST((bool)cancelled_order_data);
	BOOST_TEST(cancelled_order_data->GetPrice() == 4);
	BOOST_TEST(cancelled_order_data->GetQuantity() == 300);

	BOOST_CHECK_THROW(book.get_data(order_id), std::exception);
}

BOOST_AUTO_TEST_CASE(SignleOrderSnapshotGetting)
{
	OrderBook book;

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

BOOST_AUTO_TEST_CASE(SignleOrderMerging)
{
	OrderBook book;

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
	BOOST_TEST(order_data->GetQuantity() == 1);

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

BOOST_AUTO_TEST_CASE(TopPriorityOrderMerging)
{
	OrderBook book;

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

BOOST_AUTO_TEST_CASE(TwoInARowOrdersMerging)
{
	OrderBook book;

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

BOOST_AUTO_TEST_CASE(OrderDataChangeWhenOrderChanged)
{
	OrderBook book;

	auto constexpr bid_quantity = 300;
	auto constexpr ask_quantity = 1;

	auto bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, bid_quantity));
	BOOST_TEST_PASSPOINT();

	auto const &bid_data = book.get_data(bid_id);
	BOOST_TEST_PASSPOINT();

	BOOST_TEST(bid_data->GetQuantity() == bid_quantity);

	book.post(std::make_unique<Order>(Order::Type::Ask, 4, ask_quantity));
	BOOST_TEST_PASSPOINT();

	BOOST_TEST((bid_data->GetQuantity() == (bid_quantity - ask_quantity)));
}

BOOST_AUTO_TEST_CASE(SnapshotDataDoNotChangeWhenOrderChanged)
{
	OrderBook book;

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

BOOST_AUTO_TEST_CASE(SnapshotDataAreInAscPriceOrder)
{
	OrderBook book;

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

BOOST_AUTO_TEST_CASE(CancellingDataOfSatisfactedOrder)
{
	OrderBook book;

	auto ask_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	auto bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, 300));
	BOOST_TEST_PASSPOINT();

	auto cancelled_ask = book.cancel(ask_id);
	BOOST_TEST((false == (bool)cancelled_ask));

	auto cancelled_bid = book.cancel(ask_id);
	BOOST_TEST((false == (bool)cancelled_bid));
}

BOOST_AUTO_TEST_CASE(GettingDataOfSatisfactedOrder)
{
	OrderBook book;

	auto ask_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	auto bid_id = book.post(std::make_unique<Order>(Order::Type::Bid, 4, 300));
	BOOST_TEST_PASSPOINT();

	BOOST_CHECK_THROW(book.get_data(ask_id), std::exception);
	BOOST_CHECK_THROW(book.get_data(bid_id), std::exception);
}

BOOST_AUTO_TEST_CASE(LookupDataOfSatisfactedOrderInSnapsot)
{
	OrderBook book;

	auto order_id = book.post(std::make_unique<Order>(Order::Type::Ask, 4, 300));
	BOOST_TEST_PASSPOINT();
	book.post(std::make_unique<Order>(Order::Type::Bid, 4, 300));
	BOOST_TEST_PASSPOINT();

	auto snapshot = book.get_snapshot();
	BOOST_TEST(snapshot->GetOrders()[Order::Type::Ask].empty());
	BOOST_TEST(snapshot->GetOrders()[Order::Type::Bid].empty());
}

BOOST_AUTO_TEST_SUITE_END()