#pragma once

#include "edce_management_structures.h"


#include "xdr/types.h"
namespace edce {

uint64_t init_management_structures_from_lmdb(EdceManagementStructures& management_structures) {

	management_structures.open_lmdb_env();
	management_structures.open_lmdb();
	auto start_blk = edce_load_persisted_data(management_structures);

	return start_blk + 1;
	//if (start_blk != 0) {
	//	throw std::runtime_error("invalid start to experiment");
	//}
}

void init_management_structures_no_lmdb(EdceManagementStructures& management_structures, AccountID num_accounts, int num_assets, uint64_t default_amount) {

	auto& db = management_structures.db;

	for (AccountID i = 0; i < num_accounts; i++) {
		db.add_account_to_db(i);
	}
	db.commit_new_accounts(0);
	std::printf("done first commit\n");

	tbb::parallel_for(
		tbb::blocked_range<AccountID>(0, num_accounts),
		[&db, &num_assets, &default_amount](auto r) {
			for (AccountID acct = r.begin(); acct < r.end(); acct++) {
				account_db_idx idx;
				if (!db.lookup_user_id(acct, &idx)) {
					throw std::runtime_error("invalid accountid");
				}

				for (auto i = 0; i < num_assets; i++) {
					db.transfer_available(idx, i, default_amount);
				}
			}
		});
	db.commit_values();

	db.produce_state_commitment();
}


}
