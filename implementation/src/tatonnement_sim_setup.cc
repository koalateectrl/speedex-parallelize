#include "tatonnement_sim_setup.h"
#include "tx_type_utils.h"
#include "memory_database_view.h"
#include "serial_transaction_processor.h"
#include "price_utils.h"
#include "utils.h"

#include "xdr/types.h"
#include "xdr/transaction.h"
#include "xdr/block.h"
#include "xdr/experiments.h"

#include <cstdio>
#include <random>
#include <cstdint>

#include <tbb/parallel_for.h>

// TRANSACTIONS HERE ARE NOT SIGNED YMMV

namespace edce {

void TatonnementSimSetup::create_accounts(int num_accounts) {
	auto& db = management_structures.db;
	auto db_view = UnbufferedMemoryDatabaseView(db);

	for (int i = 0; i < num_accounts; i++) {
		account_db_idx temp;
		PublicKey pk;
		pk.fill(0);
		auto result = db_view.create_new_account(i, pk, &temp);
		if (result!= TransactionProcessingStatus::SUCCESS) {
			std::printf("what the fuck\n");
		}
	}
	db_view.commit();
	db.commit(0);
}

void TatonnementSimSetup::set_all_account_balances(size_t num_accounts, size_t num_assets, int64_t balance) {
	auto& db = management_structures.db;

	for (size_t i = 0; i < num_accounts; i++) {
		account_db_idx temp;
		
		if (!db.lookup_user_id(i, &temp)) {
			throw std::runtime_error("couldn't lookup account!");
		}
		for (AssetID j = 0; j < num_assets; j++) {
			db.transfer_available(temp, j, balance);
		}
	}
	db.commit(0);
}

template<typename Generator>
Price gen_normal_price(double mean_valuation, Generator& gen) {
		std::normal_distribution<> dist(mean_valuation, 1);
		double price = -1;
		while (price <= 0) {
			price = dist(gen);
		}
		return PriceUtils::from_double(price);
}

void TatonnementSimSetup::load_synthetic_txs(const ExperimentBlock& txs, size_t num_transactions_to_load, size_t commit_round_number) {
	//SerialTransactionProcessor tx_processor(management_structures);

	if (num_transactions_to_load > txs.size()) {
		throw std::runtime_error("not enough transactions");
	}

	std::atomic_flag failure;
	failure.clear();

	tbb::parallel_for(tbb::blocked_range<std::size_t>(0, num_transactions_to_load, 10000),
		[this, &txs, &failure] (auto r) {
			ProcessingSerialManager manager (management_structures.work_unit_manager);
//			SerialTransactionProcessor tx_processor(management_structures);
			for (auto idx = r.begin(); idx < r.end(); idx++) {
			
				auto create_offer = txs[idx].transaction.operations[0].body.createSellOfferOp();

				auto& metadata = txs[idx].transaction.metadata;

				Offer offer;
				offer.category = create_offer.category;
				offer.offerId = metadata.sequenceNumber + 1;
				offer.owner = metadata.sourceAccount;
				offer.amount = create_offer.amount;
				offer.minPrice = create_offer.minPrice;

				auto i = management_structures.work_unit_manager.look_up_idx(offer.category);
				size_t null_bits = 0;
				manager.add_offer(i, offer, null_bits, null_bits);
				//auto status = tx_processor.process_transaction(txs.at(idx));
				//if (status != TransactionProcessingStatus::SUCCESS) {
				//	failure.test_and_set();
			//		return;
			//	}
			}
			manager.finish_merge();
		});

	if (failure.test_and_set()) {
		std::printf("got bad status from a tx processing thread\n");
		throw std::runtime_error("failed tx load");
	}
	/*for (size_t i = 0; i < num_transactions_to_load; i++) {
		auto tx = txs[i];
		
		auto status = tx_processor.process_transaction(tx);
			if (status != TransactionProcessingStatus::SUCCESS) {
				///std::printf("something dumb happened %lu %d\n", account, status);
				throw std::runtime_error("couldn't load txs");
			}
		}*/
	//tx_processor.finish();
	management_structures.db.commit(0);
	management_structures.work_unit_manager.commit_for_production(commit_round_number);
}

void TatonnementSimSetup::create_cvxpy_comparison_txs(int num_accounts, int num_assets, Price* underlying_prices,  int seed) {
	std::uniform_int_distribution<> asset_dist (0, num_assets-1);
	std::uniform_int_distribution<> endow_dist (1, 100);

	std::minstd_rand gen(seed);

	SerialAccountModificationLog log(management_structures.account_modification_log, 0);

	SerialTransactionProcessor tx_processor(management_structures);

	std::uniform_real_distribution<> ratio_dist (0.01, 100); 
	auto& db = management_structures.db;

	BlockStateUpdateStatsWrapper stats;

	for (int i = 0; i < num_accounts; i++) {
		int order_sz = endow_dist(gen);
		int sell_asset = asset_dist(gen);
		int buy_asset = sell_asset;
		while (buy_asset == sell_asset) {
			buy_asset = asset_dist(gen);
		}
		AccountID account = (AccountID) i;

		account_db_idx idx;
		db.lookup_user_id(account, &idx);

		db.transfer_available(idx, sell_asset, order_sz);

		SignedTransaction signed_tx;
		auto& tx = signed_tx.transaction;
		tx.metadata.sourceAccount = account;
		tx.metadata.sequenceNumber = (1)<<8;

		CreateSellOfferOp op;
		op.category = TxTypeUtils::make_category(sell_asset, buy_asset, OfferType::SELL);

		op.amount = order_sz;
		
		double mean_valuation = PriceUtils::to_double(underlying_prices[sell_asset])/PriceUtils::to_double(underlying_prices[buy_asset]);

		op.minPrice = gen_normal_price(mean_valuation, gen);

		//op.minPrice = PriceUtils::from_double(1.0/ratio_dist(gen)); //cvxpy impl had ratio as the utility, not minprice

		tx.operations.push_back(TxTypeUtils::make_operation(op));

		//TransactionResult result;

		auto status = tx_processor.process_transaction(signed_tx, stats, log);
		if (status != TransactionProcessingStatus::SUCCESS) {
			std::printf("something dumb happened %lu %d\n", account, status);
		}
	}
	tx_processor.finish();
	db.commit(0);
	management_structures.work_unit_manager.commit_for_production(1);
}

double TatonnementSimSetup::create_txs(
	int num_txs, int account_end_idx, int num_assets, Price* underlying_prices, int seed, bool post_commit, int account_start_idx) {
	std::uniform_int_distribution<> asset_dist (0, num_assets-1);
	std::uniform_int_distribution<> endow_dist (1, 100);
	std::uniform_int_distribution<> account_dist (account_start_idx, account_end_idx-1);

	std::mt19937 gen(seed);
	
	SerialAccountModificationLog log(management_structures.account_modification_log, 0);

	SerialTransactionProcessor tx_processor(management_structures);

	std::unordered_map<AccountID, uint64_t> used_sequence_numbers;
	BlockStateUpdateStatsWrapper stats;

	auto& db = management_structures.db;

	for (int i = 0; i < num_txs; i++) {
		int order_sz = endow_dist(gen);
		int sell_asset = asset_dist(gen);
		int buy_asset = sell_asset;
		while (buy_asset == sell_asset) {
			buy_asset = asset_dist(gen);
		}
		AccountID account = account_dist(gen);

		account_db_idx idx;
		db.lookup_user_id(account, &idx);

		db.transfer_available(idx, sell_asset, order_sz);

		SignedTransaction signed_tx;
		auto& tx = signed_tx.transaction;
		tx.metadata.sourceAccount = account;
		tx.metadata.sequenceNumber = (used_sequence_numbers[account] += 1)<<8;

		CreateSellOfferOp op;
		op.category = TxTypeUtils::make_category(sell_asset, buy_asset, OfferType::SELL);

		op.amount = order_sz;

		double mean_valuation = PriceUtils::to_double(underlying_prices[sell_asset])/PriceUtils::to_double(underlying_prices[buy_asset]);

		std::normal_distribution<> dist(mean_valuation, 1);
		double price = -1;
		while (price <= 0) {
			price = dist(gen);
		}
		op.minPrice = PriceUtils::from_double(price);

		tx.operations.push_back(TxTypeUtils::make_operation(op));

		//TransactionResult result;

		auto status = tx_processor.process_transaction(signed_tx, stats, log);
		if (status != TransactionProcessingStatus::SUCCESS) {
			std::printf("something dumb happened %lu %d\n", account, status);
		}
	}

	auto finish_start = init_time_measurement();
	tx_processor.finish();
	double finish_duration = measure_time(finish_start);
	if (post_commit) {
		db.commit(0);
		management_structures.work_unit_manager.commit_for_production(1);
	}
	return finish_duration;
}


}
