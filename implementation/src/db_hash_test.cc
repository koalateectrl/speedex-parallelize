#include "edce_management_structures.h"

#include "simple_debug.h"

using namespace edce;

int main(int argc, char const *argv[])
{
	MemoryDatabase db1;
	MemoryDatabase db2;

	uint64_t num_accounts = 10000000;

	for (AccountID i = 0; i < num_accounts; i++) {
		auto idx1 = db1.add_account_to_db(i);
		auto idx2 = db2.add_account_to_db(i);
	}

	db1.commit(0);
	db2.commit(0);
	for (AccountID i = 0; i < num_accounts; i++) {
		account_db_idx idx1, idx2;

		if (!db1.lookup_user_id(i, &idx1)) {
			throw std::runtime_error("sdifasd");
		}
		if (!db2.lookup_user_id(i, &idx2)) {
			throw std::runtime_error("sdfs");
		}

		for (AssetID j = 0; j < 10; j++) {
			db1.transfer_available(idx1, j, j* 10);
			db2.transfer_available(idx2, j, j* 10);
		}
	}

	db1.commit_values();
	db2.commit_values();

	db1.produce_state_commitment();
	db2.produce_state_commitment();

	std::printf("made setup\n");
	AccountModificationLog log;
	SerialAccountModificationLog serial_log(log);

	for (AccountID i = 0; i < num_accounts; i += 17) {
		account_db_idx idx;
		if (!db1.lookup_user_id(i, &idx)) {
			throw std::runtime_error("invalid lookup");
		}
		db1.transfer_available(idx, 0, 100);
		if (!db2.lookup_user_id(i, &idx)) {
			throw std::runtime_error("invalid lookup");
		}
		db2.transfer_available(idx, 0, 100);
		serial_log.log_self_modification(i, i);
	}

	serial_log.finish();

	Hash h1, h2;

	db1.produce_state_commitment(h1);
	db2.produce_state_commitment(h2, log);

	std::printf("h1 = %s\nh2 = %s\n", DebugUtils::__array_to_str(h1.data(), h1.size()).c_str(), DebugUtils::__array_to_str(h2.data(), h2.size()).c_str());

	if (memcmp(h1.data(), h2.data(), h1.size()) == 0) {
		std::printf("success!\n");
		return 0;
	}

	std::printf("failure\n");

	db1.log();
	db2.log();






}