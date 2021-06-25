#include "end_to_end_simulator.h"

#include <thread>

#include <chrono>

namespace edce {


void run_generator(TxGenMock* gen, int num_txs) {
	gen -> stream_transactions(num_txs);
}

//template<typename Clock>
//double time_diff(Clock start, Clock end) {
//	return ((double)std::chrono::duration_cast<std::chrono::microseconds>(end-start).count()) / 1000000;
//}

void EndToEndSimulator::run_block(Price* price_workspace) {

	auto& manager = management_structures.work_unit_manager;
	auto& db = management_structures.db;

	std::vector<std::thread> threads;

	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < block_gen_threads; i++) {
		threads.push_back(std::thread(run_generator, &(tx_generators[i]), tx_per_thread));
	}
	for (int i = 0; i < block_gen_threads; i++) {
		threads[i].join();
	}

	auto end_tx_create = std::chrono::high_resolution_clock::now();

	std::printf("tx creation:%f\n", time_diff(start, end_tx_create));

	manager.commit();
	db.commit();

	auto end_commit = std::chrono::high_resolution_clock::now();
	std::printf("commit:%f\n", time_diff(end_tx_create, end_commit));

	for (int i = 0; i < manager.get_num_assets(); i++) {
		price_workspace[i] = PriceUtils::from_double(1);
	}


	oracle.compute_prices(price_workspace);

	auto end_price_oracle = std::chrono::high_resolution_clock::now();
	std::printf("price compute:%f\n", time_diff(end_commit, end_price_oracle));

	auto lp_results = lp_solver.solve(price_workspace);

	auto end_lp_solver = std::chrono::high_resolution_clock::now();
	std::printf("lp solve:%f\n", time_diff(end_price_oracle, end_lp_solver));

	auto clearing_check = WorkUnitManagerUtils::check_clearing_params(lp_results, price_workspace, manager.get_num_assets());
	if (!clearing_check) {
		std::printf("equilibrium check failed\n");
	}
	auto end_check_clearing = std::chrono::high_resolution_clock::now();

	std::printf("(unnecessary for blocks of own creation) equilibrium verification:%f\n", time_diff(end_lp_solver, end_check_clearing));

	manager.clear_offers(lp_results, price_workspace, db, management_structures.account_modification_log);
	
	auto end_clearing = std::chrono::high_resolution_clock::now();
	std::printf("clear offers:%f\n", time_diff(end_check_clearing, end_clearing));

	auto result = db.check_valid_state();

	auto end_validate = std::chrono::high_resolution_clock::now();

	std::printf("state validate:%f\n", time_diff(end_clearing, end_validate));

	if (!result) {
		throw std::runtime_error("ended in invalid state!!!");
	}

	manager.commit();
	db.commit();

	auto end_last_commit = std::chrono::high_resolution_clock::now();
	std::printf("last commit:%f\n", time_diff(end_validate, end_last_commit));

	std::printf("total block time:%f\n", time_diff(start, end_last_commit));
	auto total_tx = tx_per_thread * block_gen_threads;
	std::printf("tx: %u tx/s: %f\n", total_tx, total_tx / time_diff(start, end_last_commit));




}

void EndToEndSimulator::init(Price* underlying_prices) {
	
	auto& manager = management_structures.work_unit_manager;
	auto& db = management_structures.db;

	auto db_view = UnbufferedMemoryDatabaseView(db);

	used_sequence_numbers.reserve(num_accounts);

	for (int i = 0; i < num_accounts; i++) {
		account_db_idx temp;
		//TODO consider integrating sig checking into e2esim
		auto result = db_view.create_new_account(i, nullptr, &temp);
		if (result!= TransactionProcessingStatus::SUCCESS) {
			std::printf("what the fuck\n");
		}
		used_sequence_numbers.emplace_back(std::move(std::make_unique<std::atomic<uint64_t>>(1)));
	}
	db_view.commit();
	db.commit();

	for (int i = 0; i < block_gen_threads; i++) {

		tx_generators.emplace_back(
			manager.get_num_assets(), 
			num_accounts,
			underlying_prices,
			management_structures,
			used_sequence_numbers,
			i);
	}
}


}