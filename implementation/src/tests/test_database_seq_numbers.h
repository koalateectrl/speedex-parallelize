#include <cxxtest/TestSuite.h>

#include <cstdint>
#include <cstdio>

#include "database.h"
#include "memory_database.h"

#include "xdr/transaction.h"

#include "simple_debug.h"

using namespace edce;

class DatabaseSeqNumberTestSuite : public CxxTest::TestSuite {

public:
	void test_simple_commit() {
		TEST_START();

		MemoryDatabase db;

		auto idx = db.add_account_to_db(1);
		db.commit(0);

		TS_ASSERT_EQUALS(TransactionProcessingStatus::SEQ_NUM_TOO_LOW, db.reserve_sequence_number(idx, 0));

		TS_ASSERT_EQUALS(TransactionProcessingStatus::SUCCESS, db.reserve_sequence_number(idx, 1<<8));
		TS_ASSERT_EQUALS(TransactionProcessingStatus::SUCCESS, db.reserve_sequence_number(idx, 10<<8));
		TS_ASSERT_EQUALS(TransactionProcessingStatus::SUCCESS, db.reserve_sequence_number(idx, 5<<8));
		TS_ASSERT_EQUALS(TransactionProcessingStatus::SEQ_NUM_TEMP_IN_USE, db.reserve_sequence_number(idx, 5<<8));

		db.commit_sequence_number(idx, 1<<8);
		db.commit_sequence_number(idx, 10<<8);
		db.commit_sequence_number(idx, 5<<8);

		//no longer a distinction between committed and reserved numbers
		//TS_ASSERT_EQUALS(TransactionProcessingStatus::INVALID_OPERATION, db.reserve_sequence_number(idx, 5<<8));

		db.commit(0);

		TS_ASSERT_EQUALS(TransactionProcessingStatus::SEQ_NUM_TOO_LOW, db.reserve_sequence_number(idx, 8<<8));
		TS_ASSERT_EQUALS(TransactionProcessingStatus::SEQ_NUM_TOO_LOW, db.reserve_sequence_number(idx, 10<<8));
		TS_ASSERT_EQUALS(TransactionProcessingStatus::SUCCESS, db.reserve_sequence_number(idx, 11<<8));
		TS_ASSERT_EQUALS(TransactionProcessingStatus::SEQ_NUM_TEMP_IN_USE, db.reserve_sequence_number(idx, 11<<8));
		db.rollback_values();
		TS_ASSERT_EQUALS(TransactionProcessingStatus::SUCCESS, db.reserve_sequence_number(idx, 11<<8));

	}
};
