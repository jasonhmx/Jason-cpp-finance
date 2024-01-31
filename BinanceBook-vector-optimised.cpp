/*

A book for Binance, or any other similar protocol.
We get updates which are up to the maximum 20 levels every 100ms, in canonical order,
ie bids highest->lowest, asks lowest->highest.
Then we get incremental best bid/offer (BBO) updates, so we need to update top of book and uncross.
Terminology: "bid" is an order to buy at that price, "offer" or "ask" is an order to sell.

API docs for reference - note you can assume this data has been fetched and parsed into some appropriate data structures
like std::vector.
https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#partial-book-depth-streams
https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#individual-symbol-book-ticker-streams

Incoming JSON for reference is:
Full book update:
{"lastUpdateId":34698491742,"bids":[["20078.54000000","0.00431000"],["20078.39000000","0.00100000"],
["20078.27000000","0.00070000"],["20078.21000000","0.00066000"],["20077.91000000","0.03781000"],
["20077.90000000","0.00110000"],["20077.86000000","0.00070000"],["20077.80000000","0.00650000"],
["20077.73000000","0.00055000"],["20077.71000000","0.00100000"],["20077.69000000","0.00984000"],
["20077.66000000","0.00066000"],["20077.61000000","0.04000000"],["20077.60000000","0.02484000"],
["20077.56000000","0.05481000"],["20077.52000000","0.28882000"],["20077.51000000","0.00064000"],
["20077.45000000","0.00070000"],["20077.43000000","0.05181000"],["20077.39000000","0.00689000"]],
"asks":[["20078.91000000","0.03437000"],["20078.95000000","0.00100000"],["20078.99000000","0.00498000"],
["20079.01000000","0.04981000"],["20079.09000000","0.00070000"],["20079.15000000","0.24902000"],
["20079.30000000","0.04110000"],["20079.31000000","0.00066000"],["20079.35000000","0.00864000"],
["20079.42000000","0.00100000"],["20079.44000000","0.09402000"],["20079.46000000","0.09402000"],
["20079.49000000","0.00100000"],["20079.50000000","0.00070000"],["20079.51000000","0.17430000"],
["20079.53000000","0.09602000"],["20079.60000000","0.00100000"],["20079.61000000","0.22853000"],
["20079.62000000","0.08741000"],["20079.66000000","0.00400000"]]}

BBO update:
{"u":34698491814,"s":"BTCUSDT","b":"20078.54000000","B":"0.00431000","a":"20078.91000000","A":"0.03497000"}

Note the "lastUpdateId"/"u" field we ignore here.
Assume the JSON has been parsed and ends up something like:

struct BookDepth
{
    std::vector<PriceQuantity> bids, asks; // or could be e.g. boost::static_vector/small_vector
};

struct BookTicker
{
    Price bestBidPrice{};
    Quantity bestBidQty{};
    Price bestAskPrice{};
    Quantity bestAskQty{};
};

Printing the book would look something like:
[ 1] [ 0.00431] 20078.540 | 20078.910 [0.03497 ]
[ 2] [   0.001] 20078.390 | 20078.950 [0.001   ]
[ 3] [  0.0007] 20078.270 | 20078.990 [0.00498 ]
[ 4] [ 0.00066] 20078.210 | 20079.010 [0.04981 ]
[ 5] [ 0.03781] 20077.910 | 20079.090 [0.0007  ]
[ 6] [  0.0011] 20077.900 | 20079.150 [0.24902 ]
[ 7] [  0.0007] 20077.860 | 20079.300 [0.0411  ]
[ 8] [  0.0065] 20077.800 | 20079.310 [0.00066 ]
[ 9] [ 0.00055] 20077.730 | 20079.350 [0.00864 ]
[10] [   0.001] 20077.710 | 20079.420 [0.001   ]
[11] [ 0.00984] 20077.690 | 20079.440 [0.09402 ]
[12] [ 0.00066] 20077.660 | 20079.460 [0.09402 ]
[13] [    0.04] 20077.610 | 20079.490 [0.001   ]
[14] [ 0.02484] 20077.600 | 20079.500 [0.0007  ]
[15] [ 0.05481] 20077.560 | 20079.510 [0.1743  ]
[16] [ 0.28882] 20077.520 | 20079.530 [0.09602 ]
[17] [ 0.00064] 20077.510 | 20079.600 [0.001   ]
[18] [  0.0007] 20077.450 | 20079.610 [0.22853 ]
[19] [ 0.05181] 20077.430 | 20079.620 [0.08741 ]
[20] [ 0.00689] 20077.390 | 20079.660 [0.004   ]

Price levels in the book are unique.
If you see an update with the same price that should replace current entry.
You may assume there is no issue with floating-point comparisons in this context.
There is no need to sanitize incoming data, you may assume it's well-formed and in range.
The book must be maintained in a valid state, ie the invariant that each price is unique, and each side is on order,
ie top bid is the highest and decreases, top offer is the lowest and increases, and best bid < best offer.

One tricky thing is is that when we get a BookTicker (BBO) update, that only tells us the new top of book. To maintain
the invariant we may have to infer that some entries no longer exist ("uncrossing"),
e.g. if we had prices (ignoring quantities for now):
99  | 101
97  | 103
95  | 105
And we got a BBO update of 100|102, we would have:
100 | 102
99  | 103
97  | 105
95  |  -
So 100 is new best bid, and just others move down. 102 is new best ask, and we have inferred 101 was removed.

You can (and probably should) use >= C++20, however it's not a requirement.
Correctness is the main thing, however book building (except to_string) is on the hot path so efficiency is a concern
also. Comment your code appropriately, and explain any non-trivial logic, particular choices you made any why, etc.

*/

#include <iostream>
#include <vector>
#include <list>
#include <algorithm>
#include <cassert>

using Price = double;
using Quantity = double;

struct PriceQuantity
{
    Price price{};
    Quantity quantity{};

    auto operator<=>(const PriceQuantity&) const = default; // if C++20 available.
};

struct BookTicker
{
    Price bestBidPrice{};
    Quantity bestBidQty{};
    Price bestAskPrice{};
    Quantity bestAskQty{};
};

// PriceQuantity container concept
template <typename T>
concept PQContainer = requires(T cont){
    std::begin(cont);
    std::end(cont);
    requires std::is_same_v<typename T::value_type, PriceQuantity>;
};

// templated capacity for initialising the internal buffers
template<size_t capacity>
class BinanceBook
{
public:
    // preallocate memory for the bids and asks vectors
    BinanceBook() {
        bidsInternal.reserve(capacity);
        asksInternal.reserve(capacity);
    }

    void clear(){
        bidsInternal.clear();
        asksInternal.clear();
    }

    bool is_empty(){
        return bidsInternal.empty() && asksInternal.empty();
    }

    // const lvalue overload
    void replace(const std::vector<PriceQuantity>& bids, const std::vector<PriceQuantity>& asks){
        // the internal buffer previously allocated for the internal vectors can be reused.
        bidsInternal = bids;
        asksInternal = asks;
        std::reverse(bidsInternal.begin(), bidsInternal.end());
        std::reverse(asksInternal.begin(), asksInternal.end());
    }

    // rvalue overload
    void replace(std::vector<PriceQuantity>&& bids, std::vector<PriceQuantity>&& asks){
        // use moved vectors' heap allocation directly
        bidsInternal = std::move(bids);
        asksInternal = std::move(asks);
        std::reverse(bidsInternal.begin(), bidsInternal.end());
        std::reverse(asksInternal.begin(), asksInternal.end());
    }


    // another overload to handle any container type other than vector
    void replace(const PQContainer auto& bids, const PQContainer auto& asks){
        // clear() does not get rid of vector's capacity, so the previously
        // allocated space can still be used.
        clear();
        for(auto& [price, quantity] : bids){
            bidsInternal.emplace_back(price, quantity);
        }
        for(auto& [price, quantity] : asks){
            asksInternal.emplace_back(price, quantity);
        }
        std::reverse(bidsInternal.begin(), bidsInternal.end());
        std::reverse(asksInternal.begin(), asksInternal.end());
    }

    void update_bbo(BookTicker& ticker){
        // check if new best bid is higher than current, if so directly push back to bids vector
        if (bidsInternal.empty() || bidsInternal.back().price < ticker.bestBidPrice){
            bidsInternal.emplace_back(ticker.bestBidPrice, ticker.bestBidQty);
        } else { // if not, find from the back where to insert; erase the values until the end, and pushback
            insertBid(ticker.bestBidPrice, ticker.bestBidQty);
        }

        if (asksInternal.empty() || asksInternal.back().price > ticker.bestAskPrice){
            asksInternal.emplace_back(ticker.bestAskPrice, ticker.bestAskQty);
        } else {
            insertAsk(ticker.bestAskPrice, ticker.bestAskQty);
        }
    }

    std::vector<std::vector<PriceQuantity>> extract() const{
        return {{bidsInternal.rbegin(), bidsInternal.rend()}, {asksInternal.rbegin(), asksInternal.rend()}};
    }

    std::string to_string() {
        std::string result;
        int level = 1;
        auto localBidIt = bidsInternal.rbegin();
        auto localAskIt = asksInternal.rbegin();

        while(localBidIt != bidsInternal.rend() && localAskIt != asksInternal.rend()){
            result += "[" + std::to_string(level) + "] [" + std::to_string(localBidIt->quantity) + "] " + std::to_string(localBidIt->price) + " | ";
            result += std::to_string(localAskIt->price) + " [" + std::to_string(localAskIt->quantity) + "]" + "\n";
            localBidIt++;
            localAskIt++;
            level++;
        }
        if(localBidIt == bidsInternal.rend()){
            while(localAskIt != asksInternal.rend()){
                result += "[" + std::to_string(level) + "] [----] ---- | ";
                result += std::to_string(localAskIt->price) + " [" + std::to_string(localAskIt->quantity) + "]" + "\n";
                localAskIt++;
                level++;
            }
        } else {
            while(localBidIt != bidsInternal.rend()){
                result += "[" + std::to_string(level) + "] [" + std::to_string(localBidIt->quantity) + "] " + std::to_string(localBidIt->price) + " | ";
                result += std::string("---- [----]") + "\n";
                localBidIt++;
                level++;
            }
        }
        return result;
    }

private:
    std::vector<PriceQuantity> bidsInternal; // ascending order, best bid at the end
    std::vector<PriceQuantity> asksInternal; // descending order, best ask at the end
    void insertBid(Price& price, Quantity& quantity){
        // binary search to find the index of the position for insertion
        // finds the index of the first price >= new price
        int lo = 0;
        int hi = bidsInternal.size()-1;
        int mid;
        while(lo < hi){
            mid = lo + (hi - lo)/2;
            if(bidsInternal[mid].price > price){
                hi = mid;
            } else if (bidsInternal[mid].price < price){
                lo = mid+1;
            } else {
                lo = mid;
                break;
            }
        }
        auto bidIt = bidsInternal.begin() + lo;
        bidsInternal.erase(bidIt, bidsInternal.end());
        bidsInternal.emplace_back(price, quantity);
    }
    void insertAsk(Price& price, Quantity& quantity){
        // binary search to find the index of the position for insertion
        // finds the index of the first price <= new price
        int lo = 0;
        int hi = asksInternal.size()-1;
        int mid;
        while(lo < hi){
            mid = lo + (hi - lo)/2;
            if(asksInternal[mid].price > price){
                lo = mid+1;
            } else if (asksInternal[mid].price < price){
                hi = mid;
            } else {
                lo = mid;
                break;
            }
        }
        auto askIt = asksInternal.begin() + lo;
        asksInternal.erase(askIt, asksInternal.end());
        asksInternal.emplace_back(price, quantity);
    }
};


int main() {
    // Create a BinanceBook instance
    BinanceBook<30> book;

    // Test `is_empty` function
    assert(book.is_empty());

    // Test `replace` function: both a vector and a list are used to demonstrate container flexibility
    std::vector<PriceQuantity> bids = { {100.0, 1.0}, {99.0, 2.0}, {98.0, 3.0} };
    std::list<PriceQuantity> asks = { {101.0, 1.0}, {102.0, 2.0}, {103.0, 3.0} };
    book.replace(bids, std::move(asks));
    std::cout << "Replaced with well-formed data\n";
    std::cout << book.to_string() << "\n";

    // Test `extract` function
    auto extractedData = book.extract();
    assert(extractedData.size() == 2);
    assert(extractedData[0].size() == 3);
    assert(extractedData[1].size() == 3);
    assert(extractedData[0][0].price == 100.0);
    assert(extractedData[0][1].price == 99.0);
    assert(extractedData[0][2].price == 98.0);
    assert(extractedData[1][0].price == 101.0);
    assert(extractedData[1][1].price == 102.0);
    assert(extractedData[1][2].price == 103.0);

    // Test `update_bbo` function
    BookTicker ticker1 = { 99.0, 3.0, 102.0, 3.0 };
    book.update_bbo(ticker1);
    extractedData = book.extract();
    assert(extractedData[0].size() == 2);
    assert(extractedData[1].size() == 2);
    assert(extractedData[0][0].price == 99.0);
    assert(extractedData[0][0].quantity == 3.0);
    assert(extractedData[1][0].price == 102.0);
    assert(extractedData[1][0].quantity == 3.0);
    std::cout << "Updated with new ticker, existing values inferred, duplicate value changed\n";
    std::cout << book.to_string() << "\n";

    BookTicker ticker2 = { 105.0, 5.0, 120.0, 5.0 };
    book.update_bbo(ticker2);
    extractedData = book.extract();
    assert(extractedData[0][1].price == 99.0);
    assert(extractedData[0][1].quantity == 3.0);
    assert(extractedData[0][0].price == 105.0);
    assert(extractedData[0][0].quantity == 5.0);
    std::cout << "Updated with new ticker, existing values inferred\n";
    std::cout << book.to_string() << "\n";

    // Test `clear` function
    book.clear();
    assert(book.is_empty());

    std::cout << "All tests passed!\n";

    return 0;
}
