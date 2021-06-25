#pragma once
#include "database.h"
#include "xdr/types.h"
#include "xdr/transaction.h"

#include <cstdint>

namespace edce {

struct BlockProcessorTestUtils {
	template<typename Database>
	static void init_simple(Database& db) {

		AccountID first = 10001;
		AccountID second = 20002;

		auto first_idx = db.add_account_to_db(first);
		auto second_idx = db.add_account_to_db(second);
		db.commit(0);

		db.transfer_available(first_idx, 0, 1000);
		db.transfer_available(second_idx, 1, 1000);

		db.commit(0);
	}

	static Operation make_account_creation_op(AccountID new_id, int64_t starting_amount) {
		CreateAccountOp op;
		op.newAccountId = new_id;
		op.startingBalance = starting_amount;
		Operation op_out;
		op_out.body.type(OperationType::CREATE_ACCOUNT);
		op_out.body.createAccountOp()  = op;
		return op_out;
	}

	static TransactionMetadata make_metadata(AccountID source_account, uint64_t tx_number) {
		TransactionMetadata metadata;
		metadata.sourceAccount = source_account;
		metadata.sequenceNumber = tx_number<<8;
		return metadata;
	}
}; 
}