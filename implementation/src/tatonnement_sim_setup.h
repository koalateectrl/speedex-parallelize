#pragma once
#include "database.h"
#include "merkle_work_unit_manager.h"
#include "xdr/types.h"
#include "edce_management_structures.h"

#include "xdr/experiments.h"

namespace edce {

class TatonnementSimSetup {

	EdceManagementStructures& management_structures;

public:
	TatonnementSimSetup(
		EdceManagementStructures& management_structures)
	: management_structures(management_structures) {}

	void create_accounts(int num_accounts);

	double create_txs(int num_txs, int account_end_idx, int num_assets, Price* underlying_prices, int seed = 0, bool post_commit = true, int account_start_idx = 0);

	void create_cvxpy_comparison_txs(int num_accounts, int num_assets, Price* underlying_prices, int seed);

	void load_synthetic_txs(const ExperimentBlock& txs, size_t num_transactions_to_load, size_t commitment_round_number = 1);

	void set_all_account_balances(size_t num_accounts, size_t num_assets, int64_t balance);

};


}
