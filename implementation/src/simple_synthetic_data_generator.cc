#include "simple_synthetic_data_generator.h"
#include "price_utils.h"

#include <random>
#include <utility>
#include <iostream>
namespace edce {

std::vector<Offer> SimpleSyntheticDataGenerator::normal_underlying_prices_sell(int num_txs, 
						std::vector<Price>& prices, 
						uint32_t min_endowment, 
						uint32_t max_endowment, 
						double min_ratio,
						double max_ratio) {

	std::vector<Offer> output;

    std::default_random_engine generator;

    std::uniform_int_distribution<> asset_dist (0, num_assets-1);

    std::uniform_int_distribution<> endow_dist (min_endowment, max_endowment);
	for (int i = 0; i < num_txs; i++) {

		uint32_t endowment = endow_dist(generator);

        int buy_good = asset_dist(generator);
        int sell_good = buy_good;
        while (sell_good == buy_good) {
            sell_good = asset_dist(generator);
        }

        //truncated gaussian

        double sell_price = PriceUtils::to_double(prices[sell_good]);
        double buy_price = PriceUtils::to_double(prices[buy_good]);


        std::normal_distribution<double> d_sell(sell_price,2.0);
        double sell_val = min_ratio - 1;
        while (sell_val < min_ratio || sell_val > max_ratio) {

            sell_val = d_sell(generator);
        }

        std::normal_distribution<double> d_buy(buy_price,2.0);
        float buy_val = min_ratio - 1;
        while (buy_val < min_ratio || buy_val > max_ratio) {

            buy_val = d_buy(generator);
        }

        Offer offer;
        offer.category.type = OfferType::SELL;
        offer.category.sellAsset = sell_good;
        offer.category.buyAsset = buy_good;
        offer.amount = endowment;
        offer.owner = 0;
        offer.offerId = 0;
        offer.minPrice = (uint64_t)(sell_val * (((uint64_t) 1) << PriceUtils::PRICE_RADIX)/buy_val);
        //Order order(buy_good, sell_good, NormalizedDyadicPrice((uint64_t)((sell_val/buy_val) * ((uint64_t)1<<32))), endowment, OrderType::sell);
        output.push_back(offer);

	}
	return output;

}
}