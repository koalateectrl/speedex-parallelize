#include <cxxtest/TestSuite.h>

#include <cstdint>
#include <cstdio>

#include "serial_transaction_processor.h"
#include "database.h"
#include "memory_database.h"
#include "merkle_work_unit_manager.h"
#include "simple_debug.h"

#include "xdr/transaction.h"

#include "tests/block_processor_test_utils.h"

using namespace edce;

class AccountCreationTestSuite : public CxxTest::TestSuite {

public:
	void test_good_account_creation() {
		TEST_START();
		EdceManagementStructures management_structures(
			10,
			ApproximationParameters{0, 0});


		auto& db = management_structures.db;
		auto& manager = management_structures.work_unit_manager;

		SerialTransactionProcessor tx_processor(management_structures);

		BlockProcessorTestUtils::init_simple(db);

		SignedTransaction signed_tx;
		auto& tx =  signed_tx.transaction;
		tx.metadata = BlockProcessorTestUtils::make_metadata(10001, 1);

		tx.operations.push_back(BlockProcessorTestUtils::make_account_creation_op(10002, 200));
		BlockStateUpdateStatsWrapper state_update_stats;

		//check tx
		auto status = tx_processor.process_transaction(signed_tx, state_update_stats);
		TS_ASSERT_EQUALS(status, TransactionProcessingStatus::SUCCESS);

		TS_ASSERT_EQUALS(state_update_stats.new_account_count, 1);

		tx_processor.finish();

		db.commit(0);
		manager.commit_for_production(1);
		INFO("finish");

		account_db_idx new_idx;
		account_db_idx creator_idx;
		INFO("db size: %d", db.size());

		TS_ASSERT(db.lookup_user_id(10002, &new_idx));
		TS_ASSERT(db.lookup_user_id(10001, &creator_idx));

		TS_ASSERT_EQUALS(db.lookup_available_balance(new_idx, 0), 200);
		TS_ASSERT_EQUALS(db.lookup_available_balance(creator_idx, 0), 800);
	}

	void test_bad_account_creation() {
		TEST_START();
		EdceManagementStructures management_structures(
			10,
			ApproximationParameters{0, 0});

		auto& db = management_structures.db;
		auto& manager = management_structures.work_unit_manager;
		
		SerialTransactionProcessor tx_processor(management_structures);

		BlockProcessorTestUtils::init_simple(db);

		SignedTransaction signed_tx;
		auto& tx = signed_tx.transaction;
		tx.metadata = BlockProcessorTestUtils::make_metadata(10001, 1);
		tx.operations.push_back(BlockProcessorTestUtils::make_account_creation_op(10001, 200));
		BlockStateUpdateStatsWrapper state_update_stats;

		TS_ASSERT_EQUALS(TransactionProcessingStatus::NEW_ACCOUNT_ALREADY_EXISTS,
			tx_processor.process_transaction(signed_tx, state_update_stats));

		tx.operations.clear();
		tx.operations.push_back(BlockProcessorTestUtils::make_account_creation_op(20002, 200));
		TS_ASSERT_EQUALS(TransactionProcessingStatus::NEW_ACCOUNT_ALREADY_EXISTS,
			tx_processor.process_transaction(signed_tx, state_update_stats));

		tx.operations.clear();
		tx.operations.push_back(BlockProcessorTestUtils::make_account_creation_op(10004, 200));
		TS_ASSERT_EQUALS(TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(signed_tx, state_update_stats));

		tx.operations.clear();
		tx.operations.push_back(BlockProcessorTestUtils::make_account_creation_op(10002, 200));
		tx.operations.push_back(BlockProcessorTestUtils::make_account_creation_op(10003, 1000));
		TS_ASSERT_DIFFERS(TransactionProcessingStatus::SUCCESS,
			tx_processor.process_transaction(signed_tx, state_update_stats));

		tx_processor.finish();
		db.commit(0);
		manager.commit_for_production(2);

		account_db_idx temp;

		TS_ASSERT(!db.lookup_user_id(10002, &temp));
		TS_ASSERT(!db.lookup_user_id(10003, &temp));
		TS_ASSERT(db.lookup_user_id(10004, &temp));
	}

};