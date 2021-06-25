#include "database.h"
#include "memory_database.h"

#include "simple_debug.h"

#include "simulator_setup.h"

#include "memory_database.h"
#include "block_processor.h"
#include "merkle_work_unit_manager.h"

#include "xdr/transaction.h"

#include <cstdio>
#include <thread>

#include <chrono>

using namespace edce;

using Database = MemoryDatabase;

struct TxSets {
	std::vector<xdr::xvector<Transaction>> tx_sets;
};

void parallel_tx_build(
	TxSets* sets,
	int idx, 
	BlockBuilder<Database>* main_builder) {

	//std::printf("running from %d to %d\n", start_idx, end_idx);

	SerialBlockBuilder<Database> builder(*main_builder);

	//xdr::xvector<Transaction> sublist;
	//sublist.insert(sublist.end(), txs.begin() + start_idx, txs.begin() + end_idx);

	std::printf("Starting add\n");
	auto start = std::chrono::high_resolution_clock::now();

	builder.add_new_txs(sets->tx_sets[idx]);

	std::printf("success:%d transient fail: %d\n", 
		builder.get_successful_count(),
		builder.get_transient_failure_count());


	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	std::printf("thread add_new_txs duration %d, \n", (int) duration.count());

	builder.close_to_new_txs_and_finish();
	std::printf("finished adding\n");

	start = end;
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	std::printf("thread close_to_new_txs_and_finish duration %d, \n", (int) duration.count());

}

int main(int argc, char** argv) {

	std::printf("hardware concurrency:%d\n", std::thread::hardware_concurrency());
	int num_threads = 1;
	if (argc >=2) {
		std::printf("%s", argv[1]);
		num_threads = std::stoi(argv[1]);
	}

	uint64_t num_accounts = 100000;
	uint64_t num_assets = 10;
	uint64_t num_txs = 500000;


	Database db;
	MerkleWorkUnitManager manager(0, 0, num_assets);

	BlockBuilder<Database> builder(manager, db);
	SimulatorSetup<Database> setup(db, builder);

	for (unsigned int i = 0; i < num_assets; i++) {
		for (unsigned int j = 0; j < num_assets; j++) {
			if (i != j) {
				OfferCategory category;
				category.type = OfferType::SELL;
				category.sellAsset = i;
				category.buyAsset = j;
				manager.look_up_idx(category);
			}
		}
	}
	manager.commit();

	std::printf("starting db init\n");

	setup.initialize_database(num_accounts, num_assets);

	std::printf("db initialized\n");

	auto tx_block = setup.generate_random_valid_block(
		num_txs, num_accounts, num_assets);

	std::printf("finished block\n");

	std::vector<std::thread> threads;


	TxSets *sets = new TxSets;

	for (int i = 0; i < num_threads; i++) {
		int start_idx = (num_txs * i) / num_threads;
		int end_idx = (num_txs * (i+1)) / num_threads;

		sets->tx_sets.emplace_back();

		sets->tx_sets[i].insert(sets->tx_sets[i].end(), tx_block.begin() + start_idx, tx_block.begin() + end_idx);

		
	}

	auto start = std::chrono::high_resolution_clock::now();

	for (int i = 0; i < num_threads; i++) {
		threads.push_back(
			std::thread(
				[sets, i]
				(BlockBuilder<Database>* builder)
				{
					parallel_tx_build(sets, i, builder);
				}, &builder));
	}
	for (int i = 0; i < num_threads; i++) {
		threads[i].join();
	}
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	std::printf("Total Duration: %d, Txs per Second: %f\n", (int) duration.count(), num_txs * 1000000 / (double) duration.count());

	start = end;

	manager.commit();

	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	std::printf("Manager Commit duration:%d\n", (int) duration.count());


	delete sets;

	return 0;


}
