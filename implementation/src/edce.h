#pragma once

#include "edce_options.h"
#include "database.h"
#include "merkle_work_unit_manager.h"
#include "transaction_buffer_manager.h"
#include "tatonnement_oracle.h"
#include "lp_solver.h"
#include "block_builder.h"
#include "block_builder_manager.h"
#include "xdr/block.h"
#include "signature_check.h"
#include "async_worker.h"

#include "edce_management_structures.h"
#include "work_unit_state_commitment.h"
#include "block_update_stats.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>

namespace edce {

/*
class Edce {

	EdceOptions options;

	//transaction data management

	EdceManagementStructures management_structures;

	//block building data management

	TransactionBufferManager tx_buffer_manager;
	TransactionBufferManager sig_buffer_manager;

	BlockBuilderManager block_builder_manager;
	std::vector<std::unique_ptr<BlockBuilder>> block_builders;

	std::vector<std::unique_ptr<BufferedSignatureCheck>> signature_checkers;

	//FrozenDataCache block_cache;

	//solving and processing objects

	TatonnementOracle oracle;
	LPSolver lp_solver;

	//Track previous block header

	HashedBlock prev_block;

	void assemble_block_pieces(TransactionData& data);
	void block_creation_logic(Price* price_workspace, HashedBlock& new_block);

	void make_internal_hashes(InternalHashes& hashes);

public:
	Edce(EdceOptions options)
	: options(options), 
	management_structures{
		MemoryDatabase(), 
		MerkleWorkUnitManager(options.smooth_mult, options.tax_rate, options.num_assets), 
		AccountModificationLog{}},
	//db(), 
	//work_unit_manager(
//		options.smooth_mult,
//		options.tax_rate,
//		options.num_assets),
	tx_buffer_manager(),
	sig_buffer_manager(),
	block_builder_manager(),
	block_builders(),
	signature_checkers(),
	//block_cache(),
	oracle(management_structures.work_unit_manager, 0),
	lp_solver(management_structures.work_unit_manager){
		add_tx_processing_threads(options.num_tx_processing_threads);
		add_signature_check_threads(options.num_sig_check_threads);
	}

	//delete copy constructors, implicitly blocks move ctors
	Edce(const Edce&) = delete;
	Edce& operator=(const Edce&) = delete;

	//not reference in case we want to modify settings mid-run
	const EdceOptions get_current_options() {
		return options;
	}

	void add_tx_processing_threads(int num_additional_threads);

	void add_signature_check_threads(int num_additional_threads);

	std::pair<HashedBlock, TransactionData> build_block(Price* price_workspace);

	TransactionBufferManager& get_tx_buffer_manager() {
		return tx_buffer_manager;
	}

	TransactionBufferManager& get_sig_buffer_manager() {
		return sig_buffer_manager;
	}

	template<typename Database>
	Database& get_db() {
		return management_structures.db;
	}

	void initial_state_one_oligarch(PublicKey pk) {
		auto& db = management_structures.db;

		db.add_account_to_db(0, pk);
		db.commit();
		account_db_idx idx = 0;
		db.lookup_user_id(0, &idx);
		for (unsigned int i = 0; i < options.num_assets; i++) {
			db.transfer_available(idx, i, 1e18);
		}
		db.commit();
	}

};*/

/*

These should be called in order.
1. block_creation_logic does block production (after processing transactions, this fn computes prices/etc and clears transactions)
2. make_state_commitment hashes data structures
3. format_hashed_block takes data structures and puts them into a consistent format


4. persist_data pushes data to disk.


persist_data can be done in parallel with 2 and 3.  the only tricky bit is the block hash map.  We have to update this in (1) so its updated form is present for 2 and 4.
*/



void 
edce_block_creation_logic(
	Price* price_workspace, 
	EdceManagementStructures& management_structures,
	TatonnementManagementStructures& tatonnement,
	const EdceOptions& options,
	const Hash& prev_block_hash,
	const uint64_t prev_block_number,
	BlockCreationMeasurements& stats,
	WorkUnitStateCommitment& work_unit_clearing_details,
	uint8_t& fee_rate_out,
	BlockStateUpdateStatsWrapper& state_update_stats);

void 
edce_make_state_commitment(
	InternalHashes& hashes,
	EdceManagementStructures& management_structures,
	BlockProductionHashingMeasurements& measurements,
	const EdceOptions& options);

void 
edce_format_hashed_block(
	HashedBlock& block_out,
	const HashedBlock& prev_block,
	const EdceOptions& options,
	const Price* price_workspace,
	const uint8_t tax_rate);

//void 
//edce_persist_data(
//	EdceManagementStructures& management_structures,
//	const HashedBlock& header,
//	BlockDataPersistenceMeasurements& measurements);

std::unique_ptr<AccountModificationBlock>
edce_persist_critical_round_data(
	EdceManagementStructures& management_structures,
	const HashedBlock& header,
	BlockDataPersistenceMeasurements& measurements,
	bool get_block = false,
	uint64_t log_offset = 0);

void 
edce_persist_async(
	EdceManagementStructures& management_structures,
	const uint64_t current_block_number,
	BlockDataPersistenceMeasurements& measurements);


void
edce_persist_async_phase2(
	EdceManagementStructures& management_structures,
	uint64_t current_block_number,
	BlockDataPersistenceMeasurements& measurements);

void
edce_persist_async_phase3(
	EdceManagementStructures& management_structures,
	uint64_t current_block_number,
	BlockDataPersistenceMeasurements& measurements);

struct EdceAsyncPersisterPhase3 : public AsyncWorker {
	using AsyncWorker::mtx;
	using AsyncWorker::cv;

	EdceManagementStructures& management_structures;
	BlockDataPersistenceMeasurements* latest_measurements = nullptr;
	std::optional<uint64_t> current_block_number = std::nullopt;
	
	bool exists_work_to_do() override final {
		return latest_measurements != nullptr;
	}


	void run() {
		while(true) {
			std::unique_lock lock(mtx);
			if ((!done_flag) && (!exists_work_to_do())) {
				cv.wait(lock, [this] () { return done_flag || exists_work_to_do();});
			}
			if (done_flag) return;
			if (latest_measurements == nullptr) {
				throw std::runtime_error("invalid call to async_persist_phase3!");
			}
			edce_persist_async_phase3(management_structures, *current_block_number, *latest_measurements);
			latest_measurements = nullptr;
			current_block_number = std::nullopt;
			cv.notify_all();
		}
	}

	EdceAsyncPersisterPhase3(EdceManagementStructures& management_structures)
		: AsyncWorker()
		, management_structures(management_structures) {
			start_async_thread([this] {run();});
		}

	void wait_for_async_persist_phase3() {
		wait_for_async_task();
	}

	void do_async_persist_phase3(uint64_t current_block_number_caller, BlockDataPersistenceMeasurements* measurements) {
		wait_for_async_persist_phase3();

		std::lock_guard lock(mtx);
		latest_measurements = measurements;
		current_block_number = current_block_number_caller;
		cv.notify_one();
	}

	~EdceAsyncPersisterPhase3() {
		wait_for_async_persist_phase3();
		end_async_thread();
	}
};


/*
struct MultiThreadAsyncPhase2 : public AsyncWorker {
	using AsyncWorker::mtx;
	using AsyncWorker::cv;

	EdceManagementStructures& management_structures;
	BlockDataPersistenceMeasurements* latest_measurements = nullptr;
	std::optional<uint64_t> current_block_number = std::nullopt;


	EdceAsyncPersisterPhase3 phase3_persist;

	uint64_t synced_block_number = 0;

	bool exists_work_to_do() override final {
		return latest_measurements != nullptr;
	}

	void spawn_async_msync_thread() {
		uint64_t block_number = *current_block_number;
		BlockDataPersistenceMeasurements* measurements = latest_measurements;
		std::thread([this, block_number, measurements] () {
			edce_persist_async_phase2(management_structures, block_number, *measurements);

			std::lock_guard lock(mtx);
			if (synced_block_number < block_number) {
				synced_block_number = block_number;
			}
			cv.notify_all();
		}).detach();
	}

	void wait_for_msync(uint64_t round_number) {
		std::unique_lock lock(mtx);
		if (synced_block_number >= round_number) {
			return;
		}
		cv.wait(lock, [this, round_number] () {return synced_block_number >= round_number; });
	}

	void run() {
		std::unique_lock lock(mtx);
		while (true) {
			if ((!done_flag) && (!exists_work_to_do())) {
				cv.wait(lock, [this] () { return done_flag || exists_work_to_do();});
			}
			if (done_flag) return;
			if (latest_measurements == nullptr) {
				throw std::runtime_error("invalid call to async_persist_phase2!");
			}

			spawn_async_msync_thread();
			BlockDataPersistenceMeasurements* next_measurements = latest_measurements
			latest_measurements = nullptr;
			uint64_t wait_for_block = *current_block_number;
			current_block_number = std::nullopt;
			
			lock.unlock();
			wait_for_msync(wait_for_block);
			//edce_persist_async_phase2(management_structures, *current_block_number, *latest_measurements);
			
			phase3_persist.do_async_persist_phase3(wait_for_block, next_measurements)
			cv.notify_all();

			lock.lock();
		}
	}


	MultiThreadAsyncPhase2(EdceManagementStructures& management_structures)
		: AsyncWorker()
		, management_structures(management_structures)
		, phase3_persist(management_structures) {
			start_async_thread([this] {run();});
		}

	void wait_for_async_persist_phase2() {
		wait_for_async_task();
	}

	void do_async_persist_phase2(uint64_t current_block_number_caller, BlockDataPersistenceMeasurements* measurements) {
		wait_for_async_persist_phase2();

		std::lock_guard lock(mtx);
		latest_measurements = measurements;
		current_block_number = current_block_number_caller;
		cv.notify_all();
	}

	~MultiThreadAsyncPhase2() {
		wait_for_async_persist_phase2();
		end_async_thread();
	}
};

*/

struct BackgroundSyncer {
	std::mutex mtx;
	EdceManagementStructures& management_structures;


	std::atomic<bool> flag = false;

	bool running = false;

	BackgroundSyncer(EdceManagementStructures& management_structures)
		:management_structures(management_structures) {}

	bool is_running() {
		return running;//.load(std::memory_order_relaxed);
	}

	void run() {
		while(true) {
			if (flag.load(std::memory_order_relaxed)) {
				return;
			}
			management_structures.db.force_sync();
		}
	}

	void stop() {
		flag = true;
	}

	~BackgroundSyncer() {
		stop();
	}

	void start_run() {
		running = true;
		std::thread([this] () {
			run();
			}).detach();
	}
};

struct EdceAsyncPersisterPhase2 : public AsyncWorker {
	using AsyncWorker::mtx;
	using AsyncWorker::cv;

	EdceManagementStructures& management_structures;
	BlockDataPersistenceMeasurements* latest_measurements = nullptr;
	std::optional<uint64_t> current_block_number = std::nullopt;


	EdceAsyncPersisterPhase3 phase3_persist;

	BackgroundSyncer syncer;

	bool exists_work_to_do() override final {
		return latest_measurements != nullptr;
	}

	void run() {
		while(true) {
			std::unique_lock lock(mtx);
			if ((!done_flag) && (!exists_work_to_do())) {
				cv.wait(lock, [this] () { return done_flag || exists_work_to_do();});
			}
			if (done_flag) return;
			if (latest_measurements == nullptr) {
				throw std::runtime_error("invalid call to async_persist_phase2!");
			}
			//if (!syncer.is_running()) {
			//	syncer.start_run();
			//}

			edce_persist_async_phase2(management_structures, *current_block_number, *latest_measurements);
			
			phase3_persist.do_async_persist_phase3(*current_block_number, latest_measurements);

			latest_measurements = nullptr;
			current_block_number = std::nullopt;
			cv.notify_all();
		}
	}

	EdceAsyncPersisterPhase2(EdceManagementStructures& management_structures)
		: AsyncWorker()
		, management_structures(management_structures)
		, phase3_persist(management_structures)
       		, syncer(management_structures)	{
			start_async_thread([this] {run();});
		}

	void wait_for_async_persist_phase2() {
		wait_for_async_task();
	}

	void do_async_persist_phase2(uint64_t current_block_number_caller, BlockDataPersistenceMeasurements* measurements) {
		wait_for_async_persist_phase2();

		std::lock_guard lock(mtx);
		latest_measurements = measurements;
		current_block_number = current_block_number_caller;
		cv.notify_one();
	}

	~EdceAsyncPersisterPhase2() {
		syncer.stop();
		wait_for_async_persist_phase2();
		end_async_thread();
	}
};

struct EdceAsyncPersister {
	std::mutex mtx;
	std::condition_variable cv;

	bool done_flag = false;

	std::optional<uint64_t> block_number_to_persist;

	BlockDataPersistenceMeasurements* latest_measurements;

	EdceManagementStructures& management_structures;

	uint64_t highest_persisted_block;

	EdceAsyncPersisterPhase2 phase2_persist;

	EdceAsyncPersister(EdceManagementStructures& management_structures)
		: management_structures(management_structures)
		, phase2_persist(management_structures) {
			start_persist_thread();
		}


	void run() {
		while (true) {
			std::unique_lock lock(mtx);
			if ((!block_number_to_persist) && (!done_flag)) {
				cv.wait(lock, [this] { return ((bool) block_number_to_persist) || done_flag;});
			}
			if (done_flag) return;
			edce_persist_async(management_structures, *block_number_to_persist, *latest_measurements);

			phase2_persist.do_async_persist_phase2(*block_number_to_persist, latest_measurements);

			highest_persisted_block = *block_number_to_persist;

			block_number_to_persist = std::nullopt;
			latest_measurements = nullptr;
			cv.notify_one();
		}
	}

	uint64_t get_highest_persisted_block() {
		std::lock_guard lock(mtx);
		return highest_persisted_block;
	}

	void do_async_persist(const uint64_t persist_block_number, BlockDataPersistenceMeasurements& measurements) {
		
		auto timestamp = init_time_measurement();

		wait_for_async_persist_local();

		measurements.wait_for_persist_time = measure_time(timestamp);
		{
			std::lock_guard lock(mtx);
			if (block_number_to_persist) {
				throw std::runtime_error("can't start persist before last one finishes!");
			}
			block_number_to_persist = persist_block_number;
			latest_measurements = &measurements;
		}
		cv.notify_one();
	}

	void wait_for_async_persist_local() {
		std::unique_lock lock(mtx);
		if (!block_number_to_persist) return;
		cv.wait(lock, [this] { return !block_number_to_persist; });
	}

	void wait_for_async_persist() {
		//clears up all uses of measurements reference
		wait_for_async_persist_local();
		phase2_persist.wait_for_async_persist_phase2();
		phase2_persist.phase3_persist.wait_for_async_persist_phase3();
	}

	void start_persist_thread() {
		std::thread([this] () {
			run();
		}).detach();
	}

	void end_persist_thread() {
		std::lock_guard lock(mtx);
		done_flag = true;
		cv.notify_one();
	}

	~EdceAsyncPersister() {
		wait_for_async_persist();
		end_persist_thread();
	}
};



//runs entire block validation logic.
// Does not persist data, but if returns true, data is ready to be persisted.
template<typename TxLogType>
bool 
edce_block_validation_logic( 
	EdceManagementStructures& management_structures,
	const EdceOptions& options,
	BlockValidationMeasurements& stats,
	BlockStateUpdateStatsWrapper& state_update_stats,
	const HashedBlock& prev_block,
	const HashedBlock& expected_next_block,
	const TxLogType& transactions);


//loads persisted data, repairing lmdbs if necessary.
//at end, disk should be in a consistent state.
uint64_t 
edce_load_persisted_data(
	EdceManagementStructures& management_structures);

void 
edce_replay_trusted_round(
	EdceManagementStructures& management_structures,
	const uint64_t round_number);

} /* edce */
