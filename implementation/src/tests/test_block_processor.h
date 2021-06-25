#include <cxxtest/TestSuite.h>

#include <cstdint>
#include <cstdio>

#include "serial_transaction_processor.h"
#include "database.h"
#include "memory_database.h"
#include "merkle_work_unit_manager.h"
#include "simple_debug.h"
#include "edce_management_structures.h"
#include "tbb/global_control.h"


#include "xdr/transaction.h"

using namespace edce;

class SerialTransactionProcessorTestSuite : public CxxTest::TestSuite {

	SignedTransaction make_sell_tx(
		AccountID src, int sell, int buy, int amount, Price price, uint64_t seq_num) {
		SignedTransaction signed_tx;
		CreateSellOfferOp op;
		op.category.type = OfferType::SELL;
		op.category.sellAsset = sell;
		op.category.buyAsset = buy;
		op.amount = amount;
		op.minPrice = price;

		Operation operation;
		operation.body.type(OperationType::CREATE_SELL_OFFER);
		operation.body.createSellOfferOp() = op;

		auto& tx = signed_tx.transaction;

		tx.metadata.sourceAccount = src;
		tx.metadata.sequenceNumber = seq_num<<8;

		tx.operations.push_back(operation);
		return signed_tx;
	}

public:

	void test_create_offer_simple() {
		TEST_START();

		//tbb::global_control control(
		//	tbb::global_control::max_allowed_parallelism, 1);

		EdceManagementStructures management_structures(
			10,
			ApproximationParameters{1, 1});

		auto& db = management_structures.db;
		auto& manager = management_structures.work_unit_manager;
		manager.commit_for_production(1);
		//MemoryDatabase db;
		//MerkleWorkUnitManager manager(0, 0, 10);
		SerialTransactionProcessor tx_processor(management_structures);

		AccountID first = 10001;
		AccountID second = 20002;

		auto first_idx = db.add_account_to_db(first);
		auto second_idx = db.add_account_to_db(second);

		db.commit(0);

		db.transfer_available(first_idx, 0, 100);
		db.transfer_available(second_idx, 1, 100);

		db.commit(0);

		INFO("committed initial test funds");

		auto tx = make_sell_tx(first, 0, 1, 60, 100, 1);
		BlockStateUpdateStatsWrapper state_update_stats;

		TS_ASSERT_EQUALS(100, db.lookup_available_balance(first_idx, 0));

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(tx, state_update_stats));


		INFO("did the transaction");

		TS_ASSERT_EQUALS(40, db.lookup_available_balance(first_idx, 0));

		tx_processor.finish();
		db.rollback_values();
		manager.rollback_thunks(1);

		INFO("rolled back offer");

		TS_ASSERT_EQUALS(100, db.lookup_available_balance(first_idx, 0));

	}

	void test_manage_offer_sequence_nums() {
		TEST_START();

		EdceManagementStructures management_structures(
			10,
			ApproximationParameters{1, 1});

		auto& db = management_structures.db;
		auto& manager = management_structures.work_unit_manager;

		SerialTransactionProcessor tx_processor(management_structures);
		BlockStateUpdateStatsWrapper state_update_stats;

		AccountID first = 10001;
		AccountID second = 20002;

		auto first_idx = db.add_account_to_db(first);
		auto second_idx = db.add_account_to_db(second);
		db.commit(0);

		db.transfer_available(first_idx, 0, 100);
		db.transfer_available(second_idx, 0, 100);

		db.commit(0);

		auto tx = make_sell_tx(first, 0, 1, 60, 100, 1);

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(tx, state_update_stats));

		tx_processor.finish();

		db.commit(0);
		manager.commit_for_production(1);
	}

	void test_create_offer_commit() {
		TEST_START();
		EdceManagementStructures management_structures(
			10,
			ApproximationParameters{1, 1});


		auto& db = management_structures.db;
		auto& manager = management_structures.work_unit_manager;
		SerialTransactionProcessor tx_processor(management_structures);

		AccountID first = 10001;
		AccountID second = 20002;

		auto first_idx = db.add_account_to_db(first);
		auto second_idx = db.add_account_to_db(second);
		db.commit(0);

		db.transfer_available(first_idx, 0, 100);
		db.transfer_available(second_idx, 0, 100);

		db.commit(0);

		auto tx = make_sell_tx(first, 0, 1, 60, 100, 1);
		BlockStateUpdateStatsWrapper state_update_stats;

		auto tx2 = make_sell_tx(second, 0, 1, 50, 39, 1);
		INFO("starting first insert");

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(tx, state_update_stats));

		TS_ASSERT_EQUALS(40, db.lookup_available_balance(first_idx, 0));

		INFO("starting second insert");

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(tx2, state_update_stats));

		TS_ASSERT_EQUALS(50, db.lookup_available_balance(second_idx, 0));

		tx_processor.finish();
		db.commit(0);
		manager.commit_for_production(1);

		TS_ASSERT_EQUALS(40, db.lookup_available_balance(first_idx, 0));

		//manager.get_work_units().at(0).log();

		TS_ASSERT_LESS_THAN_EQUALS(1, manager.get_num_work_units());
		TS_ASSERT_EQUALS(manager.get_work_unit_size(0), 2);
	}

	/*void footest_offer_rollback() {
		INFO("STart test_offer_rollback");
		MemoryDatabase db;
		WorkUnitManager manager(0, 0, 10);
		AccountID first = 10001;
		AccountID second = 20002;
		SerialTransactionProcessor tx_processor(manager, db);


		auto first_idx = db.add_account_to_db(first);
		db.add_account_to_db(second);

		db.transfer_available(first_idx, 0, 100);
		db.commit();

		Transaction tx = make_sell_tx(first, 0, 1, 50, 100, 1);

		TransactionResult result;
		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(tx, result));

		tx_processor.finish();
		db.commit();
		manager.commit_and_preprocess();
		
		Transaction mod1 = make_modify_tx(first, 0, 1, 60, 1, 1, 2);
		Transaction mod2 = make_modify_tx(first, 0, 1, 40, 1, 1, 3);

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(mod1, result));

		TS_ASSERT_EQUALS(40, db.lookup_available_balance(first_idx, 0));

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(mod2, result));


		TS_ASSERT_EQUALS(60, db.lookup_available_balance(first_idx, 0));

		db.rollback();
		manager.rollback();

		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(mod1, result));
		TS_ASSERT_EQUALS(
			TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(mod2, result));

		tx_processor.finish();
		db.commit();
		manager.commit_and_preprocess();

		TS_ASSERT_EQUALS(60,
			db.lookup_available_balance(first_idx, 0));

	}*/

};

