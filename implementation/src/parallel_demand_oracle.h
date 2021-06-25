#pragma once

#include "async_worker.h"
#include "merkle_work_unit.h"
#include "merkle_work_unit_helpers.h"
#include "demand_calc_coroutine.h"

using uint128_t = __uint128_t;

namespace edce {

class CoroutineDemandOracle {

	constexpr static uint8_t num_coroutines = 10;
	std::vector<EndowAccumulator> metadatas_full;
	std::vector<EndowAccumulator> metadatas_partial;

	size_t starting_idx, ending_idx;

	DemandCalcThrottler throttler;

public:

	CoroutineDemandOracle()
		: throttler(num_coroutines) {}
	
	void init(size_t starting_idx_, size_t ending_idx_) {
		starting_idx = starting_idx_;
		ending_idx = ending_idx_;
		size_t num_units = ending_idx - starting_idx;
		metadatas_partial.resize(num_units);
		metadatas_full.resize(num_units);
	}

	void get_supply_demand(
		Price* active_prices,
		uint128_t* supplies, 
		uint128_t* demands, 
		std::vector<MerkleWorkUnit>& work_units,
		const uint8_t smooth_mult) {

		for (size_t i = starting_idx; i < ending_idx; i++) {
			auto [full_exec_p, partial_exec_p] = work_units[i].get_execution_prices(active_prices, smooth_mult);
			//std::printf("%lu full p %lu partial p%lu\n", i, full_exec_p, partial_exec_p);
			
			//metadatas_partial[i - starting_idx] = work_units[i].get_metadata(full_exec_p);
			//metadatas_full[i - starting_idx] = work_units[i].get_metadata(partial_exec_p);
			throttler.spawn(work_units[i].coro_get_metadata(partial_exec_p, metadatas_partial[i - starting_idx], throttler.scheduler));
			throttler.spawn(work_units[i].coro_get_metadata(full_exec_p, metadatas_full[i - starting_idx], throttler.scheduler));
		}

		throttler.join();

		for (size_t i = starting_idx; i < ending_idx; i++) {
			work_units[i].calculate_demands_and_supplies_from_metadata(
				active_prices,
				demands,
				supplies, 
				smooth_mult,
				metadatas_partial[i - starting_idx],
				metadatas_full[i - starting_idx]);
		}
	}

};


class DemandOracleWorker : public AsyncWorker {
	using AsyncWorker::cv;
	using AsyncWorker::mtx;
	
	unsigned int num_assets;

	size_t starting_work_unit;
	size_t ending_work_unit;

	uint128_t* supplies;
	uint128_t* demands;

	bool round_start = false;
	
	std::atomic<bool> tatonnement_round_flag = false;
	std::atomic<bool> sleep_flag = false;
	std::atomic<bool> round_done_flag = false;


	Price* query_prices;
	std::vector<MerkleWorkUnit>* query_work_units;
	uint8_t query_smooth_mult;

	CoroutineDemandOracle coro_oracle;

	bool exists_work_to_do() {
		return round_start;
	}

	//return true if thread should go to sleep
	bool spinlock() {
		while(true) {
			bool next_round = tatonnement_round_flag.load(std::memory_order_relaxed);
			if (next_round) {
				tatonnement_round_flag.store(false, std::memory_order_relaxed);
				std::atomic_thread_fence(std::memory_order_acquire);
				return false;
			}
			bool sleep = sleep_flag.load(std::memory_order_relaxed);
			if (sleep) {
				sleep_flag.store(false, std::memory_order_relaxed);
				return true;
			}
			__builtin_ia32_pause();
		}
	}
	void get_supply_demand(
		Price* active_prices,
		uint128_t* supplies, 
		uint128_t* demands, 
		std::vector<MerkleWorkUnit>& work_units,
		const uint8_t smooth_mult) {
			
			for (size_t i = starting_work_unit; i < ending_work_unit; i++) {
				work_units[i].calculate_demands_and_supplies(active_prices, demands, supplies, smooth_mult);
			}
	}


	void run() {
		std::unique_lock lock(mtx);

		while(true) {

			if ((!done_flag) && (!exists_work_to_do())) {
				cv.wait(lock, [this] () { return done_flag || exists_work_to_do();});
			}
			if (done_flag) return;
			if (round_start) {
				round_start = false;
				
				while(!spinlock()) {
					for (size_t i = 0; i < num_assets; i++) {
						supplies[i] = 0;
						demands[i] = 0;
					}

					//coro_oracle.
						get_supply_demand(query_prices, supplies, demands, *query_work_units, query_smooth_mult);
					signal_round_compute_done();
				}
				
			}
			cv.notify_all();
		}
	}

	void signal_round_compute_done() {
		std::atomic_thread_fence(std::memory_order_release);
		round_done_flag.store(true, std::memory_order_relaxed);
	}
public:

	void init(unsigned int num_assets_, size_t starting_work_unit_, size_t ending_work_unit_) {
		num_assets = num_assets_;
		starting_work_unit = starting_work_unit_;
		ending_work_unit = ending_work_unit_;
		supplies = new uint128_t[num_assets];
		demands = new uint128_t[num_assets];
		coro_oracle.init(starting_work_unit, ending_work_unit);
		start_async_thread([this] {run();});
	}

	~DemandOracleWorker() {
		wait_for_async_task();
		end_async_thread();
		delete[] demands;
		delete[] supplies;
	}

	void wait_for_compute_done_and_get_results(uint128_t* demands_out, uint128_t* supplies_out) {
		while(true) {
			bool res = round_done_flag.load(std::memory_order_relaxed);
			if (res) {
				round_done_flag.store(false, std::memory_order_relaxed);
				std::atomic_thread_fence(std::memory_order_acquire);

				for (size_t i = 0; i < num_assets; i++) {
					demands_out[i] += demands[i];
					supplies_out[i] += supplies[i];
				}
				return;
			}
			__builtin_ia32_pause();
		}
	}

	void signal_round_start(Price* prices, std::vector<MerkleWorkUnit>* work_units, uint8_t smooth_mult) {
		query_prices = prices;
		query_work_units = work_units;
		query_smooth_mult = smooth_mult;
		std::atomic_thread_fence(std::memory_order_release);
		tatonnement_round_flag.store(true, std::memory_order_relaxed);
	}

	void activate_worker() {
		std::lock_guard lock(mtx);
		round_start = true;
		cv.notify_one();
	}
	void deactivate_worker() {
		sleep_flag.store(true, std::memory_order_relaxed);
	}
};


template<unsigned int NUM_WORKERS>
class ParallelDemandOracle {

	size_t num_work_units;

	unsigned int num_assets;

	constexpr static size_t main_thread_start_idx = 0;
	size_t main_thread_end_idx = 0;

	DemandOracleWorker workers[NUM_WORKERS];

	CoroutineDemandOracle coro_oracle;

public:
	ParallelDemandOracle(size_t num_work_units, size_t num_assets)
		: num_work_units(num_work_units)
		, num_assets(num_assets)
	{

		size_t num_shares = NUM_WORKERS + 1;
		main_thread_end_idx = num_work_units / (num_shares);

		for (size_t i = 0; i < NUM_WORKERS; i++) {
			size_t start_idx = (num_work_units * (i+1)) / num_shares;
			size_t end_idx = (num_work_units * (i+2)) / num_shares;
		//	std::printf("worker %d: start %lu end %lu total %lu\n", i, start_idx, end_idx, num_work_units);
			workers[i].init(num_assets, start_idx, end_idx);
		}
		coro_oracle.init(main_thread_start_idx, main_thread_end_idx);
	}

	void get_supply_demand(
		Price* active_prices,
		uint128_t* supplies, 
		uint128_t* demands, 
		std::vector<MerkleWorkUnit>& work_units,
		const uint8_t smooth_mult) {
		//std::printf("starting demand query\n");
		for (size_t i = 0; i < NUM_WORKERS; i++) {
			workers[i].signal_round_start(active_prices, &work_units, smooth_mult);
		}
		//std::printf("signaled round start\n");

		//coro_oracle.get_supply_demand(active_prices, supplies, demands, work_units, smooth_mult);
		for (size_t i = main_thread_start_idx; i < main_thread_end_idx; i++) {
			work_units[i].calculate_demands_and_supplies(active_prices, demands, supplies, smooth_mult);
		}
		//std::printf("starting wait for compute done\n");
		for (size_t i = 0; i < NUM_WORKERS; i++) {
			workers[i].wait_for_compute_done_and_get_results(demands, supplies);
		}
		//std::printf("done demand query\n");
	}

	void activate_oracle() {
		for (size_t i = 0; i < NUM_WORKERS; i++) {
			workers[i].activate_worker();
		}
	}

	void deactivate_oracle() {
		for (size_t i = 0; i < NUM_WORKERS; i++) {
			workers[i].deactivate_worker();
		}
	}

};

} /* edce */
