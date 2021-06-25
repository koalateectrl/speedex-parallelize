#include "merkle_work_unit_manager.h"

namespace edce {

void MerkleWorkUnitManager::increase_num_traded_assets(
	uint16_t new_asset_count) 
{
	if (new_asset_count < num_assets) {
		throw std::runtime_error("Cannot decrease number of assets");//TODO this is doable,
		//but not sure what the right semantics are.
	}
	std::vector<MerkleWorkUnit> new_work_units;
	int new_work_unit_count = WorkUnitManagerUtils::get_num_work_units_by_asset_count(new_asset_count);
	new_work_units.reserve(new_work_unit_count);
	for (int i = 0; i < new_work_unit_count; i++) {
		OfferCategory category = WorkUnitManagerUtils::category_from_idx(i, new_asset_count);
		if (category.buyAsset < num_assets && category.sellAsset < num_assets) {
			int old_idx = WorkUnitManagerUtils::category_to_idx(category, num_assets);
			new_work_units.push_back(std::move(work_units[old_idx]));
		} else {
			new_work_units.emplace_back(category);//, smooth_mult, tax_rate);
		}
	}
	work_units = std::move(new_work_units);
	num_assets = new_asset_count;
}

template<auto func, typename... Args>
void MerkleWorkUnitManager::generic_map(Args... args) {
	auto num_work_units = work_units.size();
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_work_units),
		[this, &args...] (auto r) {
			for (unsigned int i = r.begin(); i < r.end(); i++) {
				(work_units[i].*func)(args...);
			}
		});
}

template<auto func, typename... Args>
void MerkleWorkUnitManager::generic_map_serial(Args... args) {
	auto num_work_units = work_units.size();
	for (unsigned int i = 0; i < num_work_units; i++) {
		(work_units[i].*func)(args...);
	}
}

/*void 
MerkleWorkUnitManager::change_approximation_parameters_(uint8_t smooth_mult_, uint8_t tax_rate_) {
	smooth_mult = smooth_mult_;
	tax_rate = tax_rate_;

	generic_map_serial<&MerkleWorkUnit::change_approximation_parameters_>(smooth_mult, tax_rate);
}*/


/*


void MerkleWorkUnitManager::clear_offers_for_production(
	const ClearingParams& params, 
	Price* prices, 
	MemoryDatabase& db, 
	AccountModificationLog& account_log, 
	WorkUnitStateCommitment& clearing_details_out) {
	
	auto num_work_units = work_units.size();


	while (clearing_details_out.size() < num_work_units) {
		clearing_details_out.emplace_back();
	}

	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_work_units),
		[this, &params, &db, &account_log, prices, &clearing_details_out] (auto r) {

			SerialAccountModificationLog serial_account_log(account_log);
			for (unsigned int i = r.begin(); i < r.end(); i++) {
				std::printf("processing workunit %u\n", i);
				std::printf("%lu %lu\n", params.work_unit_params.size(), clearing_details_out.size());
				work_units[i].process_clear_offers(params.work_unit_params[i], prices, params.tax_rate, db, serial_account_log, clearing_details_out[i]);
			}
			serial_account_log.finish();
		});
}*/

void MerkleWorkUnitManager::clear_() {
	generic_map_serial<&MerkleWorkUnit::clear_>();
}

void MerkleWorkUnitManager::create_lmdb() {
	generic_map_serial<&MerkleWorkUnit::create_lmdb>();
}

void MerkleWorkUnitManager::open_lmdb() {
	generic_map_serial<&MerkleWorkUnit::open_lmdb>();
}

void MerkleWorkUnitManager::commit_for_production(uint64_t current_block_number) {
	std::lock_guard lock(mtx);
	generic_map<&MerkleWorkUnit::commit_for_production>(current_block_number);
}

void MerkleWorkUnitManager::rollback_thunks(uint64_t current_block_number) {
	std::lock_guard lock(mtx);
	generic_map<&MerkleWorkUnit::rollback_thunks>(current_block_number);
}

void MerkleWorkUnitManager::persist_lmdb(uint64_t current_block_number) {
	//workunits manage their own thunk threadsafety for persistence thunks
	generic_map_serial<&MerkleWorkUnit::persist_lmdb>(current_block_number);
}

void MerkleWorkUnitManager::open_lmdb_env() {
	generic_map_serial<&MerkleWorkUnit::open_lmdb_env>();
}

void MerkleWorkUnitManager::tentative_commit_for_validation(uint64_t current_block_number) {
	std::lock_guard lock(mtx);
	generic_map<&MerkleWorkUnit::tentative_commit_for_validation>(current_block_number);
}

void MerkleWorkUnitManager::finalize_validation() {
	std::lock_guard lock(mtx);
	generic_map<&MerkleWorkUnit::finalize_validation>();
}

void MerkleWorkUnitManager::rollback_validation() {
	std::lock_guard lock(mtx);
	generic_map<&MerkleWorkUnit::rollback_validation>();
}

void MerkleWorkUnitManager::load_lmdb_contents_to_memory() {
	generic_map<&MerkleWorkUnit::load_lmdb_contents_to_memory>();
}

void MerkleWorkUnitManager::generate_metadata_indices() {
	std::lock_guard lock(mtx);
	generic_map<&MerkleWorkUnit::generate_metadata_index>();
}

size_t MerkleWorkUnitManager::num_open_offers() const {
	std::lock_guard lock(mtx);

	std::atomic<size_t> num_offers = 0;
	auto num_work_units = work_units.size();
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_work_units),
		[this, &num_offers] (auto r) {
			for (unsigned int i = r.begin(); i < r.end(); i++) {
				num_offers.fetch_add(work_units[i].num_open_offers(), std::memory_order_relaxed);
			}
		});

	return num_offers.load(std::memory_order_relaxed);

}

struct ClearOffersForProductionData {
	const ClearingParams& params;
	Price* prices;
	MemoryDatabase& db;
	WorkUnitStateCommitment& clearing_details_out;

	void operator() (
		const tbb::blocked_range<std::size_t>& r, 
		std::vector<MerkleWorkUnit>& work_units, 
		SerialAccountModificationLog& local_log,
		BlockStateUpdateStatsWrapper& state_update_stats) {
		
		for (auto i = r.begin(); i < r.end(); i++) {
			work_units.at(i).process_clear_offers(params.work_unit_params.at(i), prices, params.tax_rate, db, local_log, clearing_details_out.at(i), state_update_stats);
		}
	}
};

struct TentativeClearOffersForValidationData {

	MemoryDatabase& db;
	ThreadsafeValidationStatistics& validation_statistics;
	const WorkUnitStateCommitmentChecker& clearing_commitment_log;
	std::atomic_flag& exists_failure;

	void operator() (
		const tbb::blocked_range<std::size_t>& r, 
		std::vector<MerkleWorkUnit>& work_units, 
		SerialAccountModificationLog& local_log,
		BlockStateUpdateStatsWrapper& state_update_stats) {


		for (auto i = r.begin(); i < r.end(); i++) {
			auto res = work_units[i].tentative_clear_offers_for_validation(
						db, 
						local_log, 
						validation_statistics[i], 
						clearing_commitment_log[i], 
						clearing_commitment_log,
						state_update_stats);
			if (!res) {
				std::printf("one unit failed\n");
				exists_failure.test_and_set();
			//	std::atomic_thread_fence(std::memory_order_release);
				return;
			}
		}
		//std::atomic_thread_fence(std::memory_order_release);
	}
};




template<typename ClearingData>
class ClearOffersReduce {

	AccountModificationLog& main_log;
	//SerialAccountModificationLog local_log;
	std::vector<MerkleWorkUnit>& work_units;
//	std::mutex mtx;
	ClearingData& func;

public:
	BlockStateUpdateStatsWrapper state_update_stats;
	//std::vector<SerialAccountModificationLog> accumulated_logs;

	void operator() (const tbb::blocked_range<std::size_t> r) {
		SerialAccountModificationLog local_log(main_log);
		func(r, work_units, local_log, state_update_stats);
	}

	ClearOffersReduce (ClearOffersReduce& x, [[maybe_unused]] tbb::split)
		: main_log(x.main_log)
	//	, local_log(main_log)
		, work_units(x.work_units)
	//	, mtx()
		, func(x.func)
		//, accumulated_logs() 
		{}

	ClearOffersReduce(AccountModificationLog& main_log, std::vector<MerkleWorkUnit>& work_units, ClearingData& func)
		: main_log(main_log)
	//	, local_log(main_log)
		, work_units(work_units)
	//	, mtx()
		, func(func)
		//, accumulated_logs()
		 {}

	//void operator() (const tbb::blocked_range<std::size_t> r) {
	//	std::lock_guard lock(mtx);
	//	for (size_t i = r.begin(); i < r.end(); i++) {
	//		work_units.at(i).process_clear_offers(params.work_unit_params.at(i), prices, params.tax_rate, db, local_log, clearing_details_out.at(i));
	//	}
	//}
	/*



	ClearOffersForProductionReduce(ClearOffersForProductionReduce& x, tbb::split)
		: params(x.params)
		, prices(x.prices)
		, db(x.db)
		, main_log(x.main_log)
		, local_log(main_log)
		, clearing_details_out(x.clearing_details_out)
		, work_units(x.work_units)
		, mtx()
		, accumulated_logs()
		{}

	ClearOffersForProductionReduce(
		const ClearingParams& params,
		Price* prices,
		MemoryDatabase& db,
		AccountModificationLog& main_log,
		WorkUnitStateCommitment& clearing_details_out,
		std::vector<MerkleWorkUnit>& work_units)
		: params(params)
		, prices(prices)
		, db(db)
		, main_log(main_log)
		, local_log(main_log)
		, clearing_details_out(clearing_details_out)
		, work_units(work_units)
		, mtx()
		, accumulated_logs() {}
	*/
	void join(ClearOffersReduce& other) {
	//	std::lock_guard lock(mtx);

		other.finish();

	//	std::lock_guard lock2(other.mtx);

	//	for (unsigned i = 0; i < other.accumulated_logs.size(); i++) {
	//		accumulated_logs.emplace_back(std::move(other.accumulated_logs[i]));
	//	}
//
//		other.accumulated_logs.clear();

		state_update_stats += other.state_update_stats;
	}

	void finish() {
	//	std::lock_guard lock(mtx);
//		accumulated_logs.emplace_back(std::move(local_log));
//		local_log.clear_and_reset();
	}

};

void MerkleWorkUnitManager::clear_offers_for_production(
	const ClearingParams& params, 
	Price* prices, 
	MemoryDatabase& db, 
	AccountModificationLog& account_log, 
	WorkUnitStateCommitment& clearing_details_out,
	BlockStateUpdateStatsWrapper& state_update_stats) {
	
	std::lock_guard lock(mtx);

	auto num_work_units = get_num_work_units();

	clearing_details_out.resize(num_work_units);

	//while (clearing_details_out.size() < num_work_units) {
	//	clearing_details_out.emplace_back();
	//}

	//account_log.sanity_check();

	/*SerialAccountModificationLog serial_account_log(account_log);
	for (size_t i = 0; i < num_work_units; i++) {
		std::printf("starting process %lu\n", i);
		work_units.at(i).process_clear_offers(params.work_unit_params.at(i), prices, params.tax_rate, db, serial_account_log, clearing_details_out.at(i));
		std::printf("done!\n");
	}
	std::printf("start finish\n");
	serial_account_log.finish();
	std::printf("done finish\n");
	return;*/
	const size_t work_units_per_batch = 3;

	/*tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_work_units, work_units_per_batch),
		[this, &params, &account_log, &db, prices, &clearing_details_out] (auto r) {
			SerialAccountModificationLog serial_account_log(account_log);
			try {
				for (auto i = r.begin(); i < r.end(); i++) {
				work_units.at(i).process_clear_offers(params.work_unit_params.at(i), prices, params.tax_rate, db, serial_account_log, clearing_details_out.at(i));
			}
			//std::printf("starting serial log finish\n");
			serial_account_log.finish();
			//std::printf("done serial long finish\n");
			} catch(...) {
				account_log.log_trie();
				std::fflush(stdout);
				throw;
			}
		});*/
	
	ClearOffersForProductionData data{params, prices, db, clearing_details_out};


	ClearOffersReduce<ClearOffersForProductionData> reduction(account_log, work_units, data);
	//ClearOffersForProductionReduce reduction(params, prices, db, account_log, clearing_details_out, work_units);

	//std::atomic_thread_fence(std::memory_order_release);
	tbb::parallel_reduce(tbb::blocked_range<std::size_t>(0, num_work_units, work_units_per_batch), reduction);
	//std::atomic_thread_fence(std::memory_order_acquire);

	reduction.finish();

	state_update_stats += reduction.state_update_stats;
	
	//std::printf("local logs size: %lu\n", reduction.accumulated_logs.size());
	account_log.merge_in_log_batch();//reduction.accumulated_logs);
}

bool 
MerkleWorkUnitManager::tentative_clear_offers_for_validation(
	MemoryDatabase& db,
	AccountModificationLog& account_modification_log,
	ThreadsafeValidationStatistics& validation_statistics,
	const WorkUnitStateCommitmentChecker& clearing_commitment_log,
	BlockStateUpdateStatsWrapper& state_update_stats) {

	std::lock_guard lock(mtx);

	auto num_work_units = work_units.size();

	std::atomic_flag exists_failure = ATOMIC_FLAG_INIT;
	
	validation_statistics.make_minimum_size(num_work_units);

	TentativeClearOffersForValidationData data{db, validation_statistics, clearing_commitment_log, exists_failure};

	ClearOffersReduce<TentativeClearOffersForValidationData> reduction(account_modification_log, work_units, data);
	
	const size_t work_units_per_batch = 5;

	//std::atomic_thread_fence(std::memory_order_release);
	tbb::parallel_reduce(tbb::blocked_range<std::size_t>(0, num_work_units, work_units_per_batch), reduction);
	//std::atomic_thread_fence(std::memory_order_acquire);

	reduction.finish();

	state_update_stats += reduction.state_update_stats;
	
	//std::printf("local logs size: %lu\n", reduction.accumulated_logs.size());
	account_modification_log.merge_in_log_batch();//reduction.accumulated_logs);

	return !exists_failure.test_and_set();
}

/*
void MerkleWorkUnitManager::clear_offers_for_production(
	const ClearingParams& params, 
	Price* prices, 
	MemoryDatabase& db, 
	AccountModificationLog& account_log, 
	WorkUnitStateCommitment& clearing_details_out) {
	
	auto num_work_units = work_units.size();


	while (clearing_details_out.size() < num_work_units) {
		clearing_details_out.emplace_back();
	}

	const size_t work_units_per_batch = 5;

	const auto num_batches = (num_work_units / work_units_per_batch) + 1;

	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_batches),
		[this, &params, &db, &account_log, prices, &clearing_details_out, work_units_per_batch, num_work_units] (auto r) {

			SerialAccountModificationLog serial_account_log(account_log);
			for (unsigned int i = r.begin(); i < r.end(); i++) {

				auto start_idx = i * work_units_per_batch;
				auto end_idx = std::min((i+1) * work_units_per_batch, num_work_units);
				INFO("processing workunit %u", i);
				for (auto j = start_idx; j < end_idx; j++) {
					work_units[j].process_clear_offers(params.work_unit_params[j], prices, params.tax_rate, db, serial_account_log, clearing_details_out[j]);
				}
			}
			serial_account_log.finish();
		});
}*/

size_t 
MerkleWorkUnitManager::get_total_nnz() const {
	size_t acc = 0;
	for (size_t i = 0; i < work_units.size(); i++) {
		acc += work_units[i].get_index_nnz();
	}
	return acc;
}

uint8_t 
MerkleWorkUnitManager::get_max_feasible_smooth_mult(const ClearingParams& clearing_params, Price* prices) {
	uint8_t max = UINT8_MAX;
	for (size_t i = 0; i < work_units.size(); i++) {
		auto& work_unit = work_units[i];
		uint8_t candidate = work_unit.max_feasible_smooth_mult(clearing_params.work_unit_params[i].supply_activated.ceil(), prices);
		auto category = WorkUnitManagerUtils::category_from_idx(i, num_assets);
		if (candidate < 7) {
			std::printf("sell %u buy %u mult %u sz %lu\n", category.sellAsset, category.buyAsset, candidate, work_unit.size());
		}
		max = std::min(max, candidate);
	}
	return max;
}

bool 
MerkleWorkUnitManager::clear_offers_for_data_loading(
	MemoryDatabase& db,
	AccountModificationLog& account_modification_log,
	ThreadsafeValidationStatistics& validation_statistics,
	const WorkUnitStateCommitmentChecker& clearing_commitment_log,
	const uint64_t current_block_number) {
	
	auto num_work_units = work_units.size();

	std::atomic_flag exists_failure = ATOMIC_FLAG_INIT;
	
	validation_statistics.make_minimum_size(num_work_units);

	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_work_units),
		[this, &clearing_commitment_log, &account_modification_log, &db, &validation_statistics, &exists_failure, &current_block_number] (auto r) {
			SerialAccountModificationLog serial_account_log(account_modification_log);
			BlockStateUpdateStatsWrapper stats;
			for (auto i = r.begin(); i < r.end(); i++) {

				if (work_units[i].get_persisted_round_number() < current_block_number) {
					BLOCK_INFO("doing a tentative_clear_offers_for_validation while loading");

					auto res = work_units[i].tentative_clear_offers_for_validation(
						db, 
						serial_account_log, 
						validation_statistics[i], 
						clearing_commitment_log[i], 
						clearing_commitment_log,
						stats);
					if (res) {
						exists_failure.test_and_set();
						return;
					}
				}
			}
			//serial_account_log.finish();
		});

	account_modification_log.merge_in_log_batch();

	return exists_failure.test_and_set();
}



} /* edce */
