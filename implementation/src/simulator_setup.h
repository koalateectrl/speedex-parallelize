#pragma once

#include "database.h"

#include "block_processor.h"

#include "xdr/transaction.h"

namespace edce {

template <typename Database>
class SimulatorSetup {

	Database& db;
	BlockBuilder<Database>& builder;

	void generate_block_thread(int num_txs);

public:
	SimulatorSetup(Database& db, BlockBuilder<Database>& builder) 
		: db(db), builder(builder) {};

	void initialize_database(
		int num_accounts, int num_assets);

	xdr::xvector<Transaction>
	generate_random_valid_block(
		int num_txs, int num_accounts, int num_assets);
};



}