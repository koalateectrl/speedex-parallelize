#include "edce.h"
#include "work_unit_manager_utils.h"
#include <thread>

#include "simple_debug.h"

#include <xdrpp/marshal.h>

#include <openssl/sha.h>

#include <chrono>

#include "utils.h"

#include "block_validator.h"

#include "header_persistence_utils.h"

namespace edce {

/*
//Withdraws from tx_buffer_manager
void Edce::add_tx_processing_threads(int num_additional_threads) {

	for (int i = 0; i < num_additional_threads; i++) {
		block_builders.push_back(std::move(std::make_unique<BlockBuilder>(management_structures, tx_buffer_manager, block_builder_manager)));
		auto& new_block_builder = block_builders.back();

		std::thread([&new_block_builder] () {
			new_block_builder->run();
		}).detach();
	}
}

//withdraws from sig_buffer_manager, adds to tx_buffer_manager
void Edce::add_signature_check_threads(int num_additional_threads) {
	for (int i = 0; i < num_additional_threads; i++) {
		signature_checkers.push_back(std::move(std::make_unique<BufferedSignatureCheck>(management_structures.db, sig_buffer_manager, tx_buffer_manager)));
		auto& new_sig_check = signature_checkers.back();

		std::thread([&new_sig_check] () {
			new_sig_check->run();
		}).detach();
	}
}

void Edce::assemble_block_pieces(TransactionData& data) {
	for (unsigned int i = 0; i < block_builders.size(); i++) {
		auto [txs, results] = block_builders[i]->finish_intermediates();
		data.transactions.insert(data.transactions.end(), txs.begin(), txs.end());
		//TODO disabled results gathering
		//data.results.insert(data.results.end(), results.begin(), results.end());
	}
}*/

void edce_make_state_commitment(
	InternalHashes& hashes,
	EdceManagementStructures& management_structures,
	BlockProductionHashingMeasurements& measurements,
	const EdceOptions& options) {

	//auto& db = management_structures.db;
	//auto& work_unit_manager = management_structures.work_unit_manager;


	//auto th1 = std::thread([&management_structures, &hashes, &measurements] {
	{
		auto timestamp = init_time_measurement();
		//auto& dirty_accounts = management_structures.account_modification_log.get_dirty_accounts();

		management_structures.db.produce_state_commitment(hashes.dbHash, management_structures.account_modification_log);
		measurements.db_state_commitment_time = measure_time(timestamp);
	}//);

	//auto th2 = std::thread([&management_structures, &hashes, &measurements] {
	{
		auto timestamp = init_time_measurement();
		management_structures.work_unit_manager.freeze_and_hash(hashes.clearingDetails);
		measurements.work_unit_commitment_time = measure_time(timestamp);
	}//});


	//db.produce_state_commitment(hashes.dbHash, dirty_accounts);
	//work_unit_manager.freeze_and_hash(hashes.clearingDetails);
	

	//FrozenMerkleWorkUnitManager manager_snapshot(options.num_assets);
	//manager_snapshot.freeze(work_unit_manager, hashes.workUnitHashes);

	auto timestamp = init_time_measurement();
	management_structures.account_modification_log.freeze_and_hash(hashes.modificationLogHash);
	measurements.account_log_hash_time = measure_time(timestamp);

	management_structures.block_header_hash_map.freeze_and_hash(hashes.blockMapHash);
	//th1.join();
	//th2.join();

	//tx_trie.get_hash(hashes.trieHash.data());
	//return FrozenDataStructures(std::move(db_snapshot), std::move(manager_snapshot), std::move(mod_log));
}

//void Edce::make_internal_hashes(InternalHashes& hashes) {
//	BlockProductionHashingMeasurements measurements;
//	edce_make_state_commitment(hashes, management_structures, measurements, options);
//}

struct DatabaseAutoRollback {
	MemoryDatabase& db;
	uint64_t current_block_number = 0;

	bool do_rollback_for_validation = false;
	bool do_rollback_produce_state_commitment = false;

	const AccountModificationLog* rollback_log;



	DatabaseAutoRollback(MemoryDatabase& db, uint64_t current_block_number) 
		: db(db)
		, current_block_number(current_block_number) {}

	//obv can't have this called after ~AccountModificationLog
	~DatabaseAutoRollback() {
		if (do_rollback_for_validation) {
			db.rollback_new_accounts(current_block_number);
			db.rollback_values();
			//db.rollback_for_validation(num_new_accounts);
		}

		if (do_rollback_produce_state_commitment) {
			db.rollback_produce_state_commitment(*rollback_log);
		}
	}

	void tentative_commit_for_validation() {
		do_rollback_for_validation = true;
		db.commit_new_accounts(current_block_number);
		//num_new_accounts = db.tentative_commit_for_validation();
	}


	void tentative_produce_state_commitment(Hash& hash, const AccountModificationLog& dirty_accounts) {
		do_rollback_produce_state_commitment = true;
		db.tentative_produce_state_commitment(hash, dirty_accounts);
		rollback_log = &dirty_accounts;
	}
	
	/*
	void tentative_produce_state_commitment(Hash& hash, const std::vector<AccountID>& dirty_accounts) {
		do_rollback_produce_state_commitment = true;
		db.tentative_produce_state_commitment(hash, dirty_accounts);
	}*/

	void finalize_commit() {
		if ((!do_rollback_for_validation) || (!do_rollback_produce_state_commitment)) {
			throw std::runtime_error("committing from invalid state");
		}
		do_rollback_for_validation = false;
		do_rollback_produce_state_commitment = false;
		db.commit_values(*rollback_log);
		db.finalize_produce_state_commitment();
	}
};

struct WorkUnitManagerAutoRollback {
	MerkleWorkUnitManager& manager;
	const WorkUnitStateCommitmentChecker& clearing_log;

	bool do_rollback_for_validation = false;

	WorkUnitManagerAutoRollback(MerkleWorkUnitManager& manager, const WorkUnitStateCommitmentChecker& clearing_log)
		: manager(manager)
		, clearing_log(clearing_log) {}

	~WorkUnitManagerAutoRollback() {
		if (do_rollback_for_validation) {
			std::printf("manager.rollback_validation\n");
			manager.rollback_validation();
		}
	}

	bool tentative_clear_offers_for_validation(
		EdceManagementStructures& management_structures,
		ThreadsafeValidationStatistics& validation_stats,
		BlockStateUpdateStatsWrapper& state_update_stats) {
		do_rollback_for_validation = true;
		return manager.tentative_clear_offers_for_validation(
			management_structures.db,
			management_structures.account_modification_log,
			validation_stats,
			clearing_log,
			state_update_stats);
	}

	void tentative_commit_for_validation(uint64_t current_block_number) {
		do_rollback_for_validation = true;
		manager.tentative_commit_for_validation(current_block_number);
	}

	void finalize_commit() {
		do_rollback_for_validation = false;
		manager.finalize_validation();
	}
};

struct AccountModificationLogAutoRollback {
	AccountModificationLog& log;
	bool do_rollback = true;
	bool do_cancel_block_fd = true;

	AccountModificationLogAutoRollback(AccountModificationLog& log) : log(log) {}

	~AccountModificationLogAutoRollback() {
		if (do_rollback) {
			log.detached_clear();
		}
		if (do_cancel_block_fd) {
			log.cancel_prepare_block_fd();
		}
	}

	void finalize_commit() {
		do_rollback = false;
		do_cancel_block_fd = false;
	}
};

struct BlockHeaderHashMapAutoRollback {
	BlockHeaderHashMap& map;
	bool do_rollback = false;

	BlockHeaderHashMapAutoRollback(BlockHeaderHashMap& map) : map(map) {}

	~BlockHeaderHashMapAutoRollback() {
		if (do_rollback) {
			std::printf("map.rollback_validation\n");
			map.rollback_validation();
		}
	}

	bool tentative_insert_for_validation(uint64_t current_block_number, const Hash& hash) {
		do_rollback = true;
		return map.tentative_insert_for_validation(current_block_number, hash);
	}

	void finalize_commit(uint64_t finalized_block_number) {
		do_rollback = false;
		map.finalize_validation(finalized_block_number);
	}
};

/*
If successful, returns true and all state is committed to next block.
If fails, no-op.
*/
template bool edce_block_validation_logic( 
	EdceManagementStructures& management_structures,
	const EdceOptions& options,
	BlockValidationMeasurements& stats,
	BlockStateUpdateStatsWrapper& state_update_stats,
	const HashedBlock& prev_block,
	const HashedBlock& expected_next_block,
	const SerializedBlock& transactions);

template bool edce_block_validation_logic( 
	EdceManagementStructures& management_structures,
	const EdceOptions& options,
	BlockValidationMeasurements& stats,
	BlockStateUpdateStatsWrapper& state_update_stats,
	const HashedBlock& prev_block,
	const HashedBlock& expected_next_block,
	const TransactionData& transactions);

template bool edce_block_validation_logic( 
	EdceManagementStructures& management_structures,
	const EdceOptions& options,
	BlockValidationMeasurements& stats,
	BlockStateUpdateStatsWrapper& state_update_stats,
	const HashedBlock& prev_block,
	const HashedBlock& expected_next_block,
	const AccountModificationBlock& transactions);

template bool edce_block_validation_logic( 
	EdceManagementStructures& management_structures,
	const EdceOptions& options,
	BlockValidationMeasurements& stats,
	BlockStateUpdateStatsWrapper& state_update_stats,
	const HashedBlock& prev_block,
	const HashedBlock& expected_next_block,
	const SignedTransactionList& transactions);


template<typename TxLogType>
bool edce_block_validation_logic( 
	EdceManagementStructures& management_structures,
	const EdceOptions& options,
	BlockValidationMeasurements& stats,
	BlockStateUpdateStatsWrapper& state_update_stats,
	const HashedBlock& prev_block,
	const HashedBlock& expected_next_block,
	const TxLogType& transactions) {
	
	uint64_t current_block_number = prev_block.block.blockNumber + 1;
	BLOCK_INFO("starting block validation for block %lu", current_block_number);

	if (current_block_number != expected_next_block.block.blockNumber) {
		BLOCK_INFO("invalid block number");
		return false;
	}

	management_structures.account_modification_log.prepare_block_fd(current_block_number + 1000000);

	ThreadsafeValidationStatistics validation_stats(management_structures.work_unit_manager.get_num_work_units());

	std::vector<Price> prices;

	if (expected_next_block.block.prices.size() != options.num_assets) {
		BLOCK_INFO("incorrect number of prices in expected_next_block");
		return false;
	}

	if (expected_next_block.block.internalHashes.clearingDetails.size() != management_structures.work_unit_manager.get_num_work_units()) {
		BLOCK_INFO("invalid clearingdetails (size: %lu, expected %lu", 
			expected_next_block.block.internalHashes.clearingDetails.size(), management_structures.work_unit_manager.get_num_work_units());
		return false;
	}

	for (unsigned i = 0; i < options.num_assets; i++) {
		prices.push_back(expected_next_block.block.prices[i]);
	}

	if (expected_next_block.block.feeRate + 1 < management_structures.approx_params.tax_rate) {
		BLOCK_INFO("invalid fee rate (got %u, expected %u", expected_next_block.block.feeRate, management_structures.approx_params.tax_rate);
		return false;
	}

	if (memcmp(expected_next_block.block.prevBlockHash.data(), prev_block.hash.data(), 32) != 0) {
		BLOCK_INFO("next block doesn't point to prev block");
		return false;
	}

	WorkUnitStateCommitmentChecker commitment_checker(expected_next_block.block.internalHashes.clearingDetails, prices, expected_next_block.block.feeRate);
	INFO("commitment checker log:");
	INFO_F(commitment_checker.log());
	
	//has to be first, so it's destructor is called last
	//db's destructor uses mod log
	auto account_log_autorollback = AccountModificationLogAutoRollback(management_structures.account_modification_log);

	auto db_autorollback = DatabaseAutoRollback(management_structures.db, current_block_number);
	auto manager_autorollback = WorkUnitManagerAutoRollback(management_structures.work_unit_manager, commitment_checker);
	auto header_map_autorollback = BlockHeaderHashMapAutoRollback(management_structures.block_header_hash_map);

	auto timestamp = init_time_measurement();

	BLOCK_INFO("checking clearing params");
	auto clearing_param_res = WorkUnitManagerUtils::check_clearing_params(commitment_checker, options.num_assets);

	stats.clearing_param_check = measure_time(timestamp);

	if (!clearing_param_res) {

		BLOCK_INFO("clearing params invalid");
		return false;
	}

	auto res = validate_transaction_block(
		management_structures, 
		transactions, 
		commitment_checker, 
		validation_stats,
		stats,
		state_update_stats); // checks db in valid state.

	INFO_F(validation_stats.log());

	stats.tx_validation_time = measure_time(timestamp);
	BLOCK_INFO("block validation time: %lf",stats.tx_validation_time);

	if (!res) {

		BLOCK_INFO("validation error");
		return false;
	}

	db_autorollback.tentative_commit_for_validation();
	std::printf("done tentative commit for validation\n");
	manager_autorollback.tentative_commit_for_validation(current_block_number);
	std::printf("done tentative commit workunits\n");

	stats.tentative_commit_time = measure_time(timestamp);

	auto clearings_valid = manager_autorollback.tentative_clear_offers_for_validation(
		management_structures,
		validation_stats,
		state_update_stats);

	std::printf("done tentative clearing\n");

	stats.check_workunit_validation_time = measure_time(timestamp);

	if (!clearings_valid) {
		BLOCK_INFO("clearings invalid");
		return false;
	}

	if (!commitment_checker.check(validation_stats)) {
		BLOCK_INFO("clearing stats mismatch");
		return false;
	}

	//TODO check optimality of LP solution ?

	if (!header_map_autorollback.tentative_insert_for_validation(prev_block.block.blockNumber, prev_block.hash)) {
		BLOCK_INFO("couldn't insert block hash");
		return false;
	}

	//TODO currently ignroing header map mod times, which should be basically 0.
	measure_time(timestamp);

	Block comparison_next_block;

	comparison_next_block.prevBlockHash = prev_block.hash;
	comparison_next_block.blockNumber = current_block_number;
	comparison_next_block.prices = expected_next_block.block.prices;
	comparison_next_block.feeRate = expected_next_block.block.feeRate;

	//auto& dirty_accounts = management_structures.account_modification_log.get_dirty_accounts();

	management_structures.account_modification_log.sanity_check();

	stats.get_dirty_account_time = measure_time(timestamp);
	db_autorollback.tentative_produce_state_commitment(
		comparison_next_block.internalHashes.dbHash,
		management_structures.account_modification_log);
	//BLOCK_INFO("post tentative_produce_state_commitment sanity check start");
	//management_structures.account_modification_log.sanity_check();

	stats.db_tentative_commit_time = measure_time(timestamp);
	BLOCK_INFO("db tentative_commit_time = %lf", stats.db_tentative_commit_time);
	//copy expected clearing state, except we overwrite the hashes later.
	comparison_next_block.internalHashes.clearingDetails = expected_next_block.block.internalHashes.clearingDetails;
	management_structures.work_unit_manager.freeze_and_hash(comparison_next_block.internalHashes.clearingDetails);

	stats.workunit_hash_time = measure_time(timestamp);

	try {
		management_structures.account_modification_log.freeze_and_hash(comparison_next_block.internalHashes.modificationLogHash);
	} catch(...) {
		std::printf("error in freeze_and_hash account log!\n");
		management_structures.account_modification_log.log_trie();
		throw;
	}
	management_structures.block_header_hash_map.freeze_and_hash(comparison_next_block.internalHashes.blockMapHash);

	Hash final_hash;
	hash_xdr(comparison_next_block, final_hash);

	auto hash_result = memcmp(final_hash.data(), expected_next_block.hash.data(), 32);

	if (hash_result != 0) {
		BLOCK_INFO("incorrect hash");

		if (memcmp(comparison_next_block.prevBlockHash.data(), expected_next_block.block.prevBlockHash.data(), 32) != 0) {
			BLOCK_INFO("discrepancy in prevBlockHash");
		}
		if (comparison_next_block.blockNumber != expected_next_block.block.blockNumber) {
			BLOCK_INFO("discrepancy in blockNumber");
		}
		if (comparison_next_block.prices.size() != expected_next_block.block.prices.size()) {
			BLOCK_INFO("different numbers of prices");
		}
		for (unsigned int i = 0; i < comparison_next_block.prices.size(); i++) {
			if (comparison_next_block.prices[i] != expected_next_block.block.prices[i]) {
				BLOCK_INFO("discrepancy at price %u", i);
			}
		}
		if (comparison_next_block.feeRate != expected_next_block.block.feeRate) {
			BLOCK_INFO("discrepancy in feeRate");
		}

		if (memcmp(comparison_next_block.internalHashes.dbHash.data(), expected_next_block.block.internalHashes.dbHash.data(), 32) != 0) {
			BLOCK_INFO("discrepancy in dbHash");
			//management_structures.db.log();
			//management_structures.db.values_log();
		}

		for (unsigned int i = 0; i < expected_next_block.block.internalHashes.clearingDetails.size(); i++) {
			if (memcmp(comparison_next_block.internalHashes.clearingDetails[i].rootHash.data(), expected_next_block.block.internalHashes.clearingDetails[i].rootHash.data(), 32)) {
				BLOCK_INFO("discrepancy in work unit %lu", i);
			}
		}

		if (memcmp(comparison_next_block.internalHashes.modificationLogHash.data(), expected_next_block.block.internalHashes.modificationLogHash.data(), 32) != 0) {
			BLOCK_INFO("mod log discrepancy");
			management_structures.account_modification_log.diff_with_prev_log(current_block_number);
			management_structures.account_modification_log.persist_block(current_block_number + 1000000, false); // persist anyways for now, for comparison purposes later 
		}
		if (memcmp(comparison_next_block.internalHashes.blockMapHash.data(), expected_next_block.block.internalHashes.blockMapHash.data(), 32)!= 0) {
			BLOCK_INFO("header hash map discrepancy");
		}
		return false;
	} else {
		BLOCK_INFO("block hash match");
	}

	stats.hash_time = measure_time(timestamp);
	db_autorollback.finalize_commit();
	stats.db_finalization_time = measure_time(timestamp);
	manager_autorollback.finalize_commit();
	stats.workunit_finalization_time = measure_time(timestamp);
	account_log_autorollback.finalize_commit();
	stats.account_log_finalization_time = measure_time(timestamp);
	header_map_autorollback.finalize_commit(current_block_number);
	stats.header_map_finalization_time = measure_time(timestamp);
	return true;
}

void edce_block_creation_logic(Price* price_workspace, 
	EdceManagementStructures& management_structures,
	TatonnementManagementStructures& tatonnement,
	const EdceOptions& options,
	const Hash& prev_block_hash,
	const uint64_t prev_block_number,
	BlockCreationMeasurements& stats,
	WorkUnitStateCommitment& work_unit_clearing_details,
	uint8_t& fee_rate_out,
	BlockStateUpdateStatsWrapper& state_update_stats) {

	uint64_t current_block_number = prev_block_number + 1;

	BLOCK_INFO("starting block creation");
	management_structures.account_modification_log.prepare_block_fd(current_block_number);
	//management_structures.account_modification_log.sanity_check();

	auto timestamp = init_time_measurement();

	auto& db = management_structures.db;
	auto& work_unit_manager = management_structures.work_unit_manager;

	db.commit_new_accounts(current_block_number);
	stats.initial_account_db_commit_time = measure_time(timestamp);

	BLOCK_INFO("initial accountdb commit duration: %fs", stats.initial_account_db_commit_time);

	work_unit_manager.commit_for_production(current_block_number);

	stats.initial_offer_db_commit_time = measure_time(timestamp);

	BLOCK_INFO("initial offerdb commit duration: %fs", stats.initial_offer_db_commit_time);
	BLOCK_INFO("Database size:%lu", db.size());

	std::atomic<bool> tatonnement_timeout = false;
	std::atomic<bool> cancel_timeout = false;
	auto timeout_th = tatonnement.oracle.launch_timeout_thread(2000, tatonnement_timeout, cancel_timeout);

	auto tat_res = tatonnement.oracle.compute_prices_grid_search(price_workspace, management_structures.approx_params, tatonnement.rolling_averages.formatted_rolling_avgs);
	cancel_timeout = true;

	stats.tatonnement_time = measure_time(timestamp);
	BLOCK_INFO("price computation took %fs", stats.tatonnement_time);
	stats.tatonnement_rounds = tat_res.num_rounds;

	BLOCK_INFO("time per tat round:%lf microseconds", 1'000'000.0 * stats.tatonnement_time / tat_res.num_rounds);

	bool use_lower_bound = !tatonnement_timeout;

	auto lp_results = tatonnement.lp_solver.solve(
		price_workspace, management_structures.approx_params, use_lower_bound);
	
	stats.lp_time = measure_time(timestamp);
	BLOCK_INFO("lp solving took %fs", stats.lp_time);

	if (!use_lower_bound) {
		BLOCK_INFO("tat timed out!");
	}

	constexpr bool rerun_tatonnement = true;

	if (rerun_tatonnement && !use_lower_bound) {
		BLOCK_INFO("rerunning tatonnement");
		std::vector<Price> price_copy;
		price_copy.resize(options.num_assets);
		for (size_t i = 0; i < options.num_assets; i++) {
			price_copy[i] = price_workspace[i];
		}

		tatonnement.oracle.wait_for_all_tatonnement_threads();

		timeout_th.join();
		tatonnement_timeout = false;
		cancel_timeout = false;
			
		timeout_th = tatonnement.oracle.launch_timeout_thread(50'000, tatonnement_timeout, cancel_timeout); 

		auto tat_rerun_res = tatonnement.oracle.compute_prices_grid_search(price_copy.data(), management_structures.approx_params, tatonnement.rolling_averages.formatted_rolling_avgs);
		
		bool use_lower_bound_2 = !tatonnement_timeout;
		auto lp_res_2 = tatonnement.lp_solver.solve(
				price_copy.data(), management_structures.approx_params, use_lower_bound_2);
		WorkUnitManagerUtils::check_clearing_params(lp_res_2, price_copy.data(), options.num_assets);
		
		management_structures.work_unit_manager.get_max_feasible_smooth_mult(lp_res_2, price_copy.data());
		double vol_metric = WorkUnitManagerUtils::get_weighted_price_asymmetry_metric(
				lp_res_2, 
				management_structures.work_unit_manager.get_work_units(),
				price_copy.data());

		BLOCK_INFO("long run Tat vol metric: %lf", vol_metric);
		BLOCK_INFO("done rerunning");
	}

	//TODO could be removed later.  Good rn as a sanititimeouttheck.
	auto clearing_check = WorkUnitManagerUtils::check_clearing_params(
		lp_results, price_workspace, options.num_assets);

	stats.clearing_check_time = measure_time(timestamp);
	BLOCK_INFO("clearing sanity check took %fs", stats.clearing_check_time);

	double vol_metric = WorkUnitManagerUtils::get_weighted_price_asymmetry_metric(
			lp_results, 
			management_structures.work_unit_manager.get_work_units(),
			price_workspace);
	BLOCK_INFO("regular Tat vol metric: timeout %lu %lf", !use_lower_bound, vol_metric);

	if (!clearing_check) {
		std::fflush(stdout);
		throw std::runtime_error("The prices we computed did not result in clearing!!!");
	}

	size_t num_assets = management_structures.work_unit_manager.get_num_assets();

	uint16_t volumes[num_assets];
	WorkUnitManagerUtils::get_relative_volumes(lp_results, price_workspace, num_assets, volumes);
	tatonnement.rolling_averages.add_to_average(volumes);


	stats.num_open_offers = work_unit_manager.num_open_offers();
	BLOCK_INFO("num open offers is %lu", stats.num_open_offers);

	work_unit_manager.clear_offers_for_production(
		lp_results, price_workspace, db, management_structures.account_modification_log, work_unit_clearing_details, state_update_stats);

	stats.offer_clearing_time = measure_time(timestamp);
	BLOCK_INFO("clearing offers took %fs", stats.offer_clearing_time);

	if (!db.check_valid_state(management_structures.account_modification_log)) {
		throw std::runtime_error("DB left in invalid state!!!");
	}

	stats.db_validity_check_time = measure_time(timestamp);
	BLOCK_INFO("db validity check took %fs", stats.db_validity_check_time);

	db.commit_values(management_structures.account_modification_log);

	stats.final_commit_time = measure_time(timestamp);
	BLOCK_INFO("final commit took %fs", stats.final_commit_time);
	BLOCK_INFO("finished block creation");

	management_structures.block_header_hash_map.insert_for_production(prev_block_number, prev_block_hash);
	fee_rate_out = lp_results.tax_rate;
	stats.achieved_feerate = fee_rate_out;
	stats.achieved_smooth_mult = management_structures.work_unit_manager.get_max_feasible_smooth_mult(lp_results, price_workspace);
	stats.tat_timeout_happened = !use_lower_bound;//tatonnement_timeout.load(std::memory_order_relaxed);

	BLOCK_INFO("achieved approx params tax %lu smooth %lu", stats.achieved_feerate, stats.achieved_smooth_mult);

	if ((!stats.tat_timeout_happened) && stats.achieved_smooth_mult + 1 < management_structures.approx_params.smooth_mult) {
		BLOCK_INFO("lower bound dropped from numerical precision challenge in lp solving");
		//throw std::runtime_error("can't have successful tat run but invalid smooth mult!");
	}

	tatonnement.oracle.wait_for_all_tatonnement_threads();
	timeout_th.join();
}
/*
void Edce::block_creation_logic(Price* price_workspace, HashedBlock& new_block) {
	BlockCreationMeasurements stats;
	edce_block_creation_logic(
		price_workspace, management_structures, oracle, lp_solver, options, 
		prev_block.hash, //0 for first block
		prev_block.block.blockNumber, // will be 0 for first block
		stats,
		new_block.block.internalHashes.clearingDetails);
}

std::pair<HashedBlock, TransactionData> Edce::build_block(Price* price_workspace) {

	//stop underlying data modification while doing block clearing logic
	block_builder_manager.disable_block_building();

	HashedBlock hashed_block_out;
	Block& block_out = hashed_block_out.block;
	InternalHashes& data_structure_hashes = block_out.internalHashes;

	TransactionData tx_data;

	//TransactionUtils::FrozenTxDataTrieT frozen_tx_trie;


	//in parallel, assemble lists of transactions into one list/one trie
	// while doing price computation/clearing logic.
	//TODO fix
	throw std::runtime_error("unfixed");
	//std::thread th([this, &tx_data, &frozen_tx_trie] () {
	//	assemble_block_pieces(tx_data);
	//	frozen_tx_trie = TransactionUtils::make_tx_data_trie(tx_data.transactions);
	//});

	//th.join();

	block_creation_logic(price_workspace, hashed_block_out);

	//th.join();

	BLOCK_INFO("block contains %lu txs", tx_data.transactions.size());

	//hash/freeze the resulting data structures
	//edce_make_state_commitment(data_structure_hashes, management_structures, options);

	//re-enable block building (allows underlying data structure modification)
	block_builder_manager.enable_block_building();

	//build block

	edce_format_hashed_block(hashed_block_out, prev_block, options, price_workspace);

	//FrozenDataBlock frozen_block(std::move(frozen_data_structures), hashed_block_out);
	//block_cache.add_block(std::move(frozen_block));

	prev_block = hashed_block_out;

	return std::make_pair(hashed_block_out, tx_data);
}*/

void edce_format_hashed_block(
	HashedBlock& block_out,
	const HashedBlock& prev_block,
	const EdceOptions& options,
	const Price* price_workspace,
	const uint8_t tax_rate) {

	block_out.block.blockNumber = prev_block.block.blockNumber + 1;
	block_out.block.prevBlockHash = prev_block.hash;
	block_out.block.feeRate = tax_rate;


	for (unsigned int i = 0; i < options.num_assets; i++) {
		block_out.block.prices.push_back(price_workspace[i]);
	}

	hash_xdr(block_out.block, block_out.hash);
}

std::unique_ptr<AccountModificationBlock>
edce_persist_critical_round_data(
	EdceManagementStructures& management_structures,
	const HashedBlock& header,
	BlockDataPersistenceMeasurements& measurements,
	bool get_block,
	uint64_t log_offset) {

	auto timestamp = init_time_measurement();

	save_header(header);

	measurements.header_write_time = measure_time(timestamp);

	uint64_t current_block_number = header.block.blockNumber;

	auto block_out = management_structures.account_modification_log.persist_block(current_block_number + log_offset, get_block);
	BLOCK_INFO("done writing account log");
	measurements.account_log_write_time = measure_time(timestamp);

	management_structures.db.add_persistence_thunk(current_block_number, management_structures.account_modification_log);

	measurements.account_db_checkpoint_time = measure_time(timestamp);

	management_structures.account_modification_log.detached_clear();
	BLOCK_INFO("done persist critical round data");
	//offer db thunks, header hash map already updated
	return block_out;
}

void edce_persist_async(
	EdceManagementStructures& management_structures,
	const uint64_t current_block_number,
	BlockDataPersistenceMeasurements& measurements) {

	//auto current_block_number = header.block.blockNumber;
	BLOCK_INFO("starting async persistence");
	auto timestamp = init_time_measurement();

	management_structures.db.commit_persistence_thunks(current_block_number);
	BLOCK_INFO("done async db persistence\n");
	measurements.account_db_checkpoint_finish_time = measure_time(timestamp);

/*
	management_structures.work_unit_manager.persist_lmdb(current_block_number);
	BLOCK_INFO("done async offer persistence\n");
	measurements.offer_checkpoint_time = measure_time(timestamp);

	management_structures.block_header_hash_map.persist_lmdb(current_block_number);

	measurements.block_hash_map_checkpoint_time = measure_time(timestamp);
	BLOCK_INFO("done async total persistence\n");*/
}

void
edce_persist_async_phase2(
	EdceManagementStructures& management_structures,
	uint64_t current_block_number,
	BlockDataPersistenceMeasurements& measurements) {

	BLOCK_INFO("starting async persistence phase 2");
	auto timestamp = init_time_measurement();
//	std::thread th([&management_structures] () {
//		management_structures.db.force_sync();
//	});

	management_structures.db.force_sync();
//	th.join();
	BLOCK_INFO("done async db sync\n");
	measurements.account_db_checkpoint_sync_time = measure_time(timestamp);
}

void
edce_persist_async_phase3(
	EdceManagementStructures& management_structures,
	uint64_t current_block_number,
	BlockDataPersistenceMeasurements& measurements) {

	BLOCK_INFO("starting async persistence phase 3");
	auto timestamp = init_time_measurement();

	management_structures.work_unit_manager.persist_lmdb(current_block_number);
	BLOCK_INFO("done async offer persistence\n");
	measurements.offer_checkpoint_time = measure_time(timestamp);

	management_structures.block_header_hash_map.persist_lmdb(current_block_number);

	measurements.block_hash_map_checkpoint_time = measure_time(timestamp);
	BLOCK_INFO("done async total persistence\n");
}

/*
void edce_persist_data(
	EdceManagementStructures& management_structures,
	const HashedBlock& header,
	BlockDataPersistenceMeasurements& measurements) {

	auto current_block_number = header.block.blockNumber;

	auto timestamp = init_time_measurement();

	std::thread th ([&management_structures, &header, &measurements, &current_block_number] () {
		save_header(header);

		auto timestamp = init_time_measurement();

		management_structures.account_modification_log.persist_block(current_block_number);
		measurements.account_log_write_time = measure_time(timestamp);
	});


	auto wtx = *management_structures.db.persist_lmdb(current_block_number, management_structures.account_modification_log, true);
	measurements.account_db_checkpoint_time = measure_time(timestamp);

	th.join();

	measure_time(timestamp);
	management_structures.db.finish_persist_lmdb(std::move(wtx), current_block_number);
	measurements.account_db_checkpoint_finish_time = measure_time(timestamp);

	management_structures.work_unit_manager.persist_lmdb(current_block_number);
	measurements.offer_checkpoint_time = measure_time(timestamp);

	management_structures.block_header_hash_map.persist_lmdb(current_block_number);
	measurements.block_hash_map_checkpoint_time = measure_time(timestamp);

	management_structures.account_modification_log.detached_clear();
}
*/
uint64_t edce_load_persisted_data(
	EdceManagementStructures& management_structures) {

	std::printf("starting load persisted data\n");

	management_structures.db.load_lmdb_contents_to_memory();
	std::printf("loaded db\n");
	management_structures.work_unit_manager.load_lmdb_contents_to_memory();
	std::printf("loaded offers\n");
	management_structures.block_header_hash_map.load_lmdb_contents_to_memory();
	std::printf("loaded hashmap\n");

	auto db_round = management_structures.db.get_persisted_round_number();

	auto max_workunit_round = management_structures.work_unit_manager.get_max_persisted_round_number();

	if (max_workunit_round > db_round) {
		throw std::runtime_error("can't reload if workunit persists without db (bc of the cancel offers thing)");
	}

	std::printf("db round: %lu manager max round: %lu hashmap %lu\n", 
		db_round, management_structures.work_unit_manager.get_max_persisted_round_number(), management_structures.block_header_hash_map.get_persisted_round_number());

	auto start_round = std::min({db_round, management_structures.work_unit_manager.get_min_persisted_round_number(), management_structures.block_header_hash_map.get_persisted_round_number()});
	auto end_round = std::max({db_round, max_workunit_round, management_structures.block_header_hash_map.get_persisted_round_number()});

	std::printf("replaying rounds [%lu, %lu]\n", start_round, end_round);

	for (auto i = start_round; i <= end_round; i++) {
		edce_replay_trusted_round(management_structures, i);
	}
	management_structures.db.commit_values();
	return end_round;
}

void edce_replay_trusted_round(
	EdceManagementStructures& management_structures,
	const uint64_t round_number) {

	if (round_number == 0) {
		//There is no block number 0.
		return;
	}

	auto header = load_header(round_number);
	AccountModificationBlock tx_block;
	auto block_filename = tx_block_name(round_number);
	auto res = load_xdr_from_file(tx_block, block_filename.c_str());
	if (res != 0) {
		throw std::runtime_error((std::string("can't load tx block ") + block_filename).c_str());
	}

	BLOCK_INFO("starting to replay transactions of round %lu", round_number);
	replay_trusted_block(management_structures, tx_block, header);
	if (management_structures.db.get_persisted_round_number() < round_number) {
		management_structures.db.commit_new_accounts(round_number);
	}
	BLOCK_INFO("replayed txs in block %lu", round_number);

	//not actually used or checked.
	ThreadsafeValidationStatistics validation_stats(management_structures.work_unit_manager.get_num_work_units());
	std::vector<Price> prices;
	for (unsigned i = 0; i < header.block.prices.size(); i++) {
		prices.push_back(header.block.prices[i]);
	}
	WorkUnitStateCommitmentChecker commitment_checker(header.block.internalHashes.clearingDetails, prices, header.block.feeRate);

	management_structures.work_unit_manager.commit_for_loading(round_number);

	NullModificationLog no_op_modification_log{};

	management_structures.work_unit_manager.clear_offers_for_data_loading(
		management_structures.db, no_op_modification_log, validation_stats, commitment_checker, round_number);

	management_structures.work_unit_manager.finalize_for_loading(round_number);

	std::printf("creating header loading hash map\n");
	auto header_hash_map = LoadLMDBHeaderMap(round_number, management_structures.block_header_hash_map);
	header_hash_map.insert_for_loading(round_number, header.hash);

	if (management_structures.db.get_persisted_round_number() < round_number) {
		management_structures.db.persist_lmdb(round_number);
	}
	management_structures.work_unit_manager.persist_lmdb_for_loading(round_number);
	if (management_structures.block_header_hash_map.get_persisted_round_number() < round_number) {
		management_structures.block_header_hash_map.persist_lmdb(round_number);
	}
}

} /* edce */
