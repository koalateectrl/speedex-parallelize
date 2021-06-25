#include "normal_underlying_prices_generator.h"

#include <cstdlib>
#include <iostream>
#include <random>


std::vector<SellOrder> NormalUnderlyingPricesGenerator::get_transaction_block(int num_txs) {
    std::vector<Offer> output;

    //prices generated uar from [min_ratio, max_ratio]

    float* prices = new float[num_assets];
    std::cout<<"PRICES"<<std::endl;
    for (int i = 0; i < num_assets; i++) {
        prices[i] = ((float) (rand()) / (float) (RAND_MAX)) * (max_ratio-min_ratio) + min_ratio;
    }

    std::default_random_engine generator;

    for (int i = 0; i< num_txs; i++) {
        int endowment = rand()% (max_endowment - min_endowment) + min_endowment;

        int buy_good = rand() % num_assets;
        int sell_good = buy_good;
        while (sell_good == buy_good) {
            sell_good = rand() % (num_assets);
        }
        //truncated gaussian

        std::normal_distribution<double> d_sell(prices[sell_good],2.0);
        float sell_val = min_ratio - 1;
        while (sell_val < min_ratio || sell_val > max_ratio) {

            sell_val = d_sell(generator);
        }
        std::normal_distribution<double> d_buy(prices[buy_good],2.0);
        float buy_val = min_ratio - 1;
        while (buy_val < min_ratio || buy_val > max_ratio) {

            buy_val = d_buy(generator);
        }


        SellOrder order = SellOrder(buy_good, sell_good, endowment, sell_val/buy_val);
        output.push_back(std::move(order));
    }
    delete[] prices;
    return output;
}