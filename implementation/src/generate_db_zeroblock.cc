
#include "database.h"
#include "merkle_work_unit_manager.h"
#include "block_header_hash_map.h"
#include "utils.h"
#include "xdr/experiments.h"

#include "crypto_utils.h"

#include <cstdint>


using namespace edce;
//does not modify header logs or tx logs

int main(int argc, char const *argv[])
{
	if (argc != 3) {
		std::printf("usage: ./whatever <experiment_dir> <amount>\n");
		return -1;
	}

	std::string root_experiment_dir = std::string(argv[1]);
	std::string params_filename = root_experiment_dir + "/params";
	std::string accounts_filename = root_experiment_dir + "/accounts";

	ExperimentParameters params;
	AccountIDList account_list;

	std::printf("loading parameters file from %s\n", params_filename.c_str());
	if (load_xdr_from_file(params, params_filename.c_str())) {
		throw std::runtime_error("could not open zeroblock parameters file");
	}

	std::printf("loading account list from %s\n", accounts_filename.c_str());
	if (load_xdr_from_file(account_list, accounts_filename.c_str())) {
		throw std::runtime_error("could not open zeroblock account list file");
	}

	int64_t default_amount = std::stoll(argv[2]);

	std::printf("generating database with %u assets and default amount %ld and %lu accounts\n", params.num_assets, default_amount, account_list.size());

	auto db = MemoryDatabase();

	DeterministicKeyGenerator key_gen;

	std::vector<PublicKey> pks;
	pks.resize(account_list.size());
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, account_list.size()),
		[&key_gen, &account_list, &pks](auto r) {
			for (size_t i = r.begin(); i < r.end(); i++) {
				auto [_, pk] = key_gen.deterministic_key_gen(account_list[i]);
				pks[i] = pk;
			}
		});

	//for (AccountID id : account_list) {
	for (size_t i = 0; i < account_list.size(); i++) {
	//for (AccountID i = 0; i < params.num_accounts; i++) {
		//auto [ _, pk] = key_gen.deterministic_key_gen(id);
		//std::printf("adding account %lu\n", id);
		db.add_account_to_db(account_list[i], pks[i]);
	}
	db.commit(0);
	std::printf("done first commit\n");

	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, account_list.size()),
		[&db, &params, &default_amount, &account_list](auto r) {
			for (size_t acct_list_idx = r.begin(); acct_list_idx < r.end(); acct_list_idx++) {
				account_db_idx idx;
				if (!db.lookup_user_id(account_list[acct_list_idx], &idx)) {
					std::printf("failed to find account id %lu (idx %lu)\n", account_list[acct_list_idx], acct_list_idx);
					throw std::runtime_error("invalid accountid (probably an error in generate_db_zeroblock.cc or the synthetic data gen)");
				}

				for (auto i = 0; i < params.num_assets; i++) {
					db.transfer_available(idx, i, default_amount);
				}
			}
		});

//	for (AccountID i = 0; i < params.num_accounts; i++) {
//		account_db_idx idx;
//		if (!db.lookup_user_id(i, &idx)) {
//			throw std::runtime_error("invalid accountid");
//		}
//
//		for (auto i = 0; i < params.num_assets; i++) {
//			db.transfer_available(idx, i, default_amount);
//		}
//	}
	db.commit_values();

	std::printf("done second commit\n");

	db.open_lmdb_env();

	db.create_lmdb();
	db.persist_lmdb(0);

	std::printf("done persist_lmdb\n");

	MerkleWorkUnitManager manager(params.num_assets);
	
	manager.open_lmdb_env();
	manager.create_lmdb();
	manager.persist_lmdb(0);

	std::printf("done persist manager\n");

	BlockHeaderHashMap map;
	map.open_lmdb_env();
	map.create_lmdb();
	map.persist_lmdb(0);


	return 0;
}
