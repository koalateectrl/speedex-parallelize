#include "database.h"
#include "memory_database.h"
#include "memory_database_view.h"
#include <thread>
#include "xdr/types.h"

#include <thread>

#include <chrono>

#include <cstdio>
using namespace edce;


using Database = MemoryDatabase;

void test_func(
	int num_trials, 
	std::vector<AccountID> accounts, 
	std::vector<AssetID> assets, 
	Database& db) {
	int num_accounts = accounts.size();
	int num_assets = assets.size();
	for (int i = 0; i < num_trials; i++) {
		account_db_idx db_idx = 0;
		db.lookup_user_id(accounts[i % num_accounts], &db_idx);
		db.transfer_available(db_idx, assets[i % num_assets], i);
		db.transfer_available(db_idx, assets[(i+1) % num_assets], -i);
		db.escrow(db_idx, assets[(i+2) % num_assets], i+3);
		db.escrow(db_idx, assets[(i+3) % num_assets], -i-4);
	}
}

int main(int argc, char** argv) {
	Database db;

	uint64_t num_accounts = 100000;
	uint64_t num_assets = 20;
	int num_trials = 10000000;
	int num_threads = 1;
	if (argc >= 2) {
		num_threads = std::stoi(argv[1]);
	}
	std::printf("Num threads:%d\n", num_threads);
	std::vector<AccountID> accounts;
	std::vector<AssetID> assets;
	for (uint64_t i = 0; i < num_assets; i++) {
		assets.push_back(i);
	}

	UnbufferedMemoryDatabaseView view(db);
	PublicKey pk;
	for (uint64_t i = 0; i < num_accounts; i++) {
		account_db_idx idx;
		auto status = view.create_new_account(i, pk, &idx);
		if (status != TransactionProcessingStatus::SUCCESS) {
			throw std::runtime_error("could not create account");
		}
		accounts.push_back(idx);

		for (uint64_t i = 0; i < num_assets; i++) {
			view.transfer_available(idx, assets[i], 1);
		}
	}

	auto commit_start = std::chrono::high_resolution_clock::now();
	view.commit();
	db.commit(0);
	auto commit_end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(commit_end - commit_start);

	printf("Commit duration(micros): %d\n", (int) duration.count());
	auto start = std::chrono::high_resolution_clock::now();

	std::vector<std::thread> threads;
	for (int i = 0; i < num_threads; i++) {
		threads.push_back(std::thread(
			[num_trials, accounts, assets] 
			(Database& db) {
				test_func(num_trials, accounts, assets, db);
			},
			std::ref(db)));
	}
	for (int i = 0; i < num_threads; i++) {
		threads[i].join();
	}
	db.commit_values();

	auto stop = std::chrono::high_resolution_clock::now();

	duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);


	printf("Total Duration: %d, Operations per Second: %f\n", (int) duration.count(), num_trials * num_threads * 1000000.0 / (double) duration.count());

}