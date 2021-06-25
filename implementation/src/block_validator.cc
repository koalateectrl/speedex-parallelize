#include "block_validator.h"

#include "simple_debug.h"

#include "database.h"

#include "work_unit_state_commitment.h"

#include <xdrpp/marshal.h>

#include <mutex>
#include <atomic>

#include "utils.h"

namespace edce {

const unsigned int VALIDATION_BATCH_SIZE = 1000;

struct TransactionDataWrapper {
	const TransactionData& data;

	template<typename ValidatorType>
	bool operator() (ValidatorType& tx_validator, size_t i, BlockStateUpdateStatsWrapper& stats, SerialAccountModificationLog& serial_account_log) const {
		return tx_validator.validate_transaction(data.transactions[i], stats, serial_account_log);
	}

	size_t size() const {
		return data.transactions.size();
	}
};

struct SignedTransactionListWrapper {
	const SignedTransactionList& data;

	template<typename ValidatorType>
	bool operator() (ValidatorType& tx_validator, size_t i, BlockStateUpdateStatsWrapper& stats, SerialAccountModificationLog& serial_account_log) const {
		return tx_validator.validate_transaction(data[i], stats, serial_account_log);
	}

	size_t size() const {
		return data.size();
	}
};


struct AccountModificationBlockWrapper {
	const AccountModificationBlock& data;

	template<typename ValidatorType>
	bool operator() (ValidatorType& tx_validator, size_t i, BlockStateUpdateStatsWrapper& stats, SerialAccountModificationLog& serial_account_log) const {
		bool success = true;
		auto& txs = data[i].new_transactions_self;
		for (size_t j = 0; j < txs.size(); j++) {
			success &= tx_validator.validate_transaction(txs[j], stats, serial_account_log);
		}
		return success;
//		return tx_validator.validate_transaction(data.transactions[i]);
	}

	size_t size() const {
		return data.size();
	}
};

template<typename TxListOp>
class ParallelValidate {
	const TxListOp& txs;
	EdceManagementStructures& management_structures;

	const WorkUnitStateCommitmentChecker& clearing_commitment;
	ThreadsafeValidationStatistics& main_stats;
	SerialTransactionValidator<MerkleWorkUnitManager> tx_validator;
//	std::mutex mtx;

public:

	BlockStateUpdateStatsWrapper state_update_stats;

	//std::atomic<uint8_t> valid; //atomic<bool> has no fetch_and, for some reason
	bool valid = true;

	std::vector<SerialTransactionValidator<MerkleWorkUnitManager>> accumulated_validators;
	void operator() (const tbb::blocked_range<std::size_t> r) {
		//std::atomic_thread_fence(std::memory_order_acquire);
		if (!valid) return;  //.load(std::memory_order_relaxed)) return;

		bool temp_valid = true;
		SerialAccountModificationLog serial_account_log(management_structures.account_modification_log);

		for (size_t i = r.begin(); i < r.end(); i++) {
			if (!txs(tx_validator, i, state_update_stats, serial_account_log)) {
				temp_valid = false;
				std::printf("transaction %lu failed\n", i);
				break;
			}
		}
		valid = valid && temp_valid;//.fetch_and(temp_valid, std::memory_order_relaxed);

		//std::atomic_thread_fence(std::memory_order_release);
	}

	//split constructor can be concurrent with operator()
	ParallelValidate(ParallelValidate& x, tbb::split)
		: txs(x.txs)
		, management_structures(x.management_structures)
		, clearing_commitment(x.clearing_commitment)
		, main_stats(x.main_stats)
		, tx_validator(management_structures, clearing_commitment, main_stats)
		//, mtx()
		//, valid(x.valid.load(std::memory_order_relaxed))
		, accumulated_validators()
			{};

	void join(ParallelValidate& other) {

		//std::atomic_thread_fence(std::memory_order_acquire);
		//std::lock_guard lock(mtx);
		//{
		//	std::lock_guard lock2(other.mtx);
		valid = valid && other.valid;//valid.fetch_and(other.valid.load(std::memory_order_relaxed), std::memory_order_relaxed);// = other.valid && valid;

		//}
		if (valid) {//.load(std::memory_order_relaxed)) {
			other.finish();
			
		//	std::lock_guard lock2(other.mtx);

			for (size_t i = 0; i < other.accumulated_validators.size(); i++) {
				accumulated_validators.emplace_back(std::move(other.accumulated_validators[i]));
			}

			other.accumulated_validators.clear();

			state_update_stats += other.state_update_stats;

			//accumulated_validators.emplace_back(std)

			//tx_validator.merge_in_other_serial_log(other.tx_validator);
		}
	}

	void finish() {
		//std::lock_guard lock(mtx);
		if (valid) {//.load(std::memory_order_relaxed)) {
			accumulated_validators.emplace_back(std::move(tx_validator));
			tx_validator.clear();
	//		tx_validator.finish();
		}
	}

	ParallelValidate(
		const TxListOp& txs,
		EdceManagementStructures& management_structures,
		const WorkUnitStateCommitmentChecker& clearing_commitment,
		ThreadsafeValidationStatistics& main_stats)
		: txs(txs)
		, management_structures(management_structures)
		, clearing_commitment(clearing_commitment)
		, main_stats(main_stats)
		, tx_validator(management_structures, clearing_commitment, main_stats)
	//	, mtx()
		, valid(true) 
		, accumulated_validators()
		{}
};

template<typename WrappedType>
bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const WrappedType& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& stats) {

	auto validator = ParallelValidate(transactions, management_structures, clearing_commitment, main_stats);

	BLOCK_INFO("starting to validate %lu txs", transactions.size());

	//management_structures.account_modification_log.sanity_check();
	if (management_structures.account_modification_log.size() != 0) {
		throw std::runtime_error("forgot to clear mod log");
	}

	auto timestamp = init_time_measurement();

	//std::atomic_thread_fence(std::memory_order_release);
	tbb::parallel_reduce(tbb::blocked_range<std::size_t>(0, transactions.size(), VALIDATION_BATCH_SIZE), validator);
	//std::atomic_thread_fence(std::memory_order_acquire);
	BLOCK_INFO("done validating");

	if (!validator.valid) {
		BLOCK_INFO("transaction returned as invalid");
		return false;
	}

	validator.finish();

	stats += validator.state_update_stats;

	measurements.tx_validation_processing_time = measure_time(timestamp);

	//std::vector<SerialAccountModificationLog> account_logs;


	size_t sz = validator.accumulated_validators.size();
	std::printf("validator sz: %lu\n", sz);
	/*for (size_t i = 0; i < sz; i++) {
	//for (auto& iter : validator.accumulated_validators) {
		account_logs.emplace_back(std::move(validator.accumulated_validators.at(i).extract_account_log()));
	//	account_logs.back().sanity_check();
	}
	if (validator.accumulated_validators.size() != sz) {
		throw std::runtime_error("nonsensical resize!!!");
	}


	std::thread th([&management_structures, &account_logs] () {


		std::printf("num account logs = %lu\n", account_logs.size());
		if (account_logs.size() > 0) {

			std::printf("back sz: %lu\n", account_logs.back().size());
			account_logs.back().finish();
			account_logs.pop_back();
			std::printf("mod log size: %lu\n", management_structures.account_modification_log.size());

			//management_structures.account_modification_log.sanity_check();

			management_structures.account_modification_log.merge_in_log_batch(account_logs);
			BLOCK_INFO("done merge_in_log_batch");
		}
		std::printf("post: account_logs.size() = %lu\n", account_logs.size());
	}); */

	std::thread th([&management_structures] () {
		management_structures.account_modification_log.merge_in_log_batch();
	});


	auto offer_timestamp = init_time_measurement();

	/*tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, validator.accumulated_validators.size(), 5),
		[&validator] (auto r) {
			for (auto i = r.begin(); i < r.end(); i++) {
				validator.accumulated_validators.at(i).extract_manager_view().finish_merge();
			}
		});*/
	size_t num_work_units = management_structures.work_unit_manager.get_num_work_units();

	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_work_units),
		[&validator, num_work_units] (auto r) {
			for (auto i = r.begin(); i < r.end(); i++) {
				auto& validators = validator.accumulated_validators;
				size_t validators_sz = validators.size();
				for (size_t j = 0; j < validators_sz; j++) {
					validators[j].extract_manager_view().partial_finish(i);
				}
			}
		});
	for (auto& v :  validator.accumulated_validators) {
		v.extract_manager_view().partial_finish_conclude();
	}

	measurements.tx_validation_offer_merge_time = measure_time(offer_timestamp);

	BLOCK_INFO("waiting for merge_in_log_batch join");

	th.join();

	measurements.tx_validation_trie_merge_time = measure_time(timestamp);

	BLOCK_INFO("tx validation success, checking db state");
	auto res = management_structures.db.check_valid_state(management_structures.account_modification_log);
	BLOCK_INFO("done checking db state = %d", res);
	return res;
}


bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const AccountModificationBlock& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& stats) {
	
	AccountModificationBlockWrapper wrapper{transactions};

	return validate_transaction_block(management_structures, wrapper, clearing_commitment, main_stats, measurements, stats);
}


bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const TransactionData& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& stats) {

	TransactionDataWrapper wrapper{transactions};

	return validate_transaction_block(management_structures, wrapper, clearing_commitment, main_stats, measurements, stats);
}

bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const SignedTransactionList& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& stats) {

	SignedTransactionListWrapper wrapper{transactions};

	return validate_transaction_block(management_structures, wrapper, clearing_commitment, main_stats, measurements, stats);
}

bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const SerializedBlock& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& stats) {

	SignedTransactionList txs;

	xdr::xdr_from_opaque(transactions, txs);

	SignedTransactionListWrapper wrapper{txs};

	return validate_transaction_block(management_structures, wrapper, clearing_commitment, main_stats, measurements, stats);
}

class ParallelTrustedReplay {
	const AccountModificationBlock& txs;
	EdceManagementStructures& management_structures;
	const WorkUnitStateCommitmentChecker& clearing_commitment;
	ThreadsafeValidationStatistics& main_stats;
	const uint64_t current_round_number;

public:

	void operator() (const tbb::blocked_range<std::size_t> r) {
		BlockStateUpdateStatsWrapper stats;

		SerialTransactionValidator<LoadLMDBManagerView> tx_validator(management_structures, clearing_commitment, main_stats, current_round_number);
		SerialAccountModificationLog serial_account_log(management_structures.account_modification_log);
		for (size_t i = r.begin(); i < r.end(); i++) {
			for (size_t j = 0; j < txs[i].new_transactions_self.size(); j++) {

				tx_validator.validate_transaction(txs[i].new_transactions_self[j], stats, serial_account_log, current_round_number);
			}
		}

		tx_validator.finish();
	}

	ParallelTrustedReplay(ParallelTrustedReplay& x, tbb::split)
		: txs(x.txs)
		, management_structures(x.management_structures)
		, clearing_commitment(x.clearing_commitment)
		, main_stats(x.main_stats)
		, current_round_number(x.current_round_number) {};

	void join(const ParallelTrustedReplay& other) {}

	ParallelTrustedReplay(
		const AccountModificationBlock& txs,
		EdceManagementStructures& management_structures,
		const WorkUnitStateCommitmentChecker& clearing_commitment,
		ThreadsafeValidationStatistics& main_stats,
		const uint64_t current_round_number)
		: txs(txs)
		, management_structures(management_structures)
		, clearing_commitment(clearing_commitment)
		, main_stats(main_stats)
		, current_round_number(current_round_number) {}
};

void replay_trusted_block(
	EdceManagementStructures& management_structures,
	const AccountModificationBlock& block,
	const HashedBlock& header) {

	ThreadsafeValidationStatistics validation_stats(management_structures.work_unit_manager.get_num_work_units());

	std::vector<Price> prices;

	for (unsigned i = 0; i < header.block.prices.size(); i++) {
		prices.push_back(header.block.prices[i]);
	}

	WorkUnitStateCommitmentChecker commitment_checker(header.block.internalHashes.clearingDetails, prices, header.block.feeRate);
		

	auto replayer = ParallelTrustedReplay(block, management_structures, commitment_checker, validation_stats, header.block.blockNumber);

	tbb::parallel_reduce(tbb::blocked_range<std::size_t>(0, block.size()), replayer);
}

/*
bool BlockValidator::validate_transaction_block(
	const xdr::xvector<SignedTransaction>& txs,
	const xdr::xvector<TransactionResult>& results) {

	ParallelValidate validator = ParallelValidate(txs, results, management_structures, sig_check);

	tbb::parallel_reduce(tbb::blocked_range<std::size_t>(0, txs.size()), validator);
	if (!validator.valid) {
		return false;
	}
	return management_structures.db.check_valid_state();
}*/

}
