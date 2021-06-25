#include <iostream>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <utility>

//#include "work_unit_manager.h"
#include "merkle_work_unit_manager.h"
#include "simple_synthetic_data_generator.h"
#include "xdr/types.h"
#include "price_utils.h"
#include "parallel_demand_oracle.h"

#include <chrono>
#include <atomic>

typedef unsigned __int128 uint128_t;

using namespace edce;

std::vector<Price> gen_random_prices(int num_assets) {
	double max_ratio = 1000.0;
	double min_ratio = 0.01;
	std::vector<Price> output;
	for (int j = 0; j< num_assets; j++) {
		double ratio = ((double) (rand()) / (double) (RAND_MAX)) * (max_ratio-min_ratio) + min_ratio;
		output.push_back(PriceUtils::from_double(ratio));
	}
	return output;
}

Price** gen_many_random_prices(int num_assets, int num_trials) {
	Price** output = new Price*[num_trials];
	double max_ratio = 1000.0;
	double min_ratio = 0.01;
	for (int i = 0; i < num_trials; i++) {
		
		output[i] = new Price[num_assets];
		for (int j =0; j< num_assets; j++) {
			double ratio = ((double) (rand()) / (double) (RAND_MAX)) * (max_ratio-min_ratio) + min_ratio;
			output[i][j] = PriceUtils::from_double(ratio);
		}
	}
	return output;
}

int main(int argc, char* argv[]) {

	bool small = false;

	uint64_t smooth_mult = 10;
	int num_assets = small?2:20;
	srand(0);//time(NULL));
	SimpleSyntheticDataGenerator gen(num_assets);
	
	std::cout<<"start"<<std::endl;

	auto underlying_prices = gen_random_prices(num_assets);
	int num_orders = small?1000:10000000;
	std::vector<Offer> offers = gen.normal_underlying_prices_sell(num_orders, 
						underlying_prices,
						1, 
						10000, 
						0.01,
						1000);
	MerkleWorkUnitManager m(num_assets);
	//WorkUnitManager m(smooth_mult, 8, num_assets);

	ProcessingSerialManager serial(m);
	//SerialManager serial(m);
	std::cout<<"Made Setup"<<std::endl;

	int unused = 0;
	for (int i = 0; i < num_orders; i++) {
		Offer offer = offers[i];
		int idx = m.look_up_idx(offer.category);
		serial.add_offer(idx, offer, unused, unused);
	}
	serial.finish_merge();
	//m.merge_in_serial_manager(std::move(serial));
	//m.commit();
	auto& workunits = m.get_work_units();
	
	auto start_preprocessing = std::chrono::high_resolution_clock::now();

	//m.commit_and_preprocess();
	m.commit_for_production(1);
	




	auto stop_preprocessing = std::chrono::high_resolution_clock::now();

	auto duration_preprocessing = std::chrono::duration_cast<std::chrono::microseconds>(stop_preprocessing - start_preprocessing); 
  
	std::cout << "Preprocessing (microseconds):"<<duration_preprocessing.count() << std::endl; 
	//int num_prices = 100;//00;
	int num_trials = 10000;

	auto test_prices = gen_many_random_prices(num_assets, num_trials);

	std::cout<<"starting test"<<std::endl;
	auto start = std::chrono::high_resolution_clock::now();

	int num_work_units = workunits.size();

	uint128_t* supplies = new uint128_t[num_assets];
	uint128_t* demands = new uint128_t[num_assets];

	CoroutineDemandOracle demand_oracle;
	demand_oracle.init(0, num_work_units);

	uint128_t* supplies2 = new uint128_t[num_assets];
	uint128_t* demands2 = new uint128_t[num_assets];

	for (int i = 0; i< num_trials; i++) {
		//std::printf("starting trial\n");
		/*for (int j = 0; j < num_assets; j++) {
			supplies[j] = 0;
			demands[j] = 0;
		}
		for (int j = 0; j < num_work_units; j++) {
			//workunits[j].calculate_supplies_and_demands(test_prices[i % num_trials], demands, supplies);
			workunits[j].calculate_demands_and_supplies(test_prices[i % num_trials], demands, supplies, smooth_mult);
		}*/
		for (int j = 0; j < num_assets; j++) {
			supplies2[j] = 0;
			demands2[j] = 0;
		}
		demand_oracle.get_supply_demand(test_prices[i % num_trials], supplies2, demands2, workunits, smooth_mult);
		
	/*	for (int j = 0; j < num_assets; j++) {
			//std::printf("s %lf %lf\n", PriceUtils::amount_to_double(supplies[j]), PriceUtils::amount_to_double(supplies2[j]));
			//std::printf("d %lf %lf\n", PriceUtils::amount_to_double(demands[j]), PriceUtils::amount_to_double(demands2[j]));
			if (supplies[j] != supplies2[j]) {
				//std::printf("%Lu %Lu\n", supplies[j], supplies2[j]);
				std::printf("%lf %lf\n", PriceUtils::amount_to_double(supplies[j]), PriceUtils::amount_to_double(supplies2[j]));
				//uint64_t amt = supplies
				throw std::runtime_error("supplies mismatch");
			}
			if (demands[j] != demands2[j]) {
				throw std::runtime_error("demands mismatch");
			}
		}*/


	}
	auto stop = std::chrono::high_resolution_clock::now();

	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start); 
  
	std::cout << "Unparallelized computation (microseconds):"<<duration.count() << std::endl; 
	std::cout << "Average time per demand query (microseconds):"<< (duration.count() / num_trials)<<std::endl;

	return 0;
}
