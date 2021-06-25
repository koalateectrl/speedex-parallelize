#pragma once

#include "database.h"
#include "merkle_work_unit_manager.h"
#include "merkle_trie.h"
#include "merkle_trie_utils.h"
#include "account_modification_log.h"
#include "xdr/types.h"
#include "transaction_utils.h"
#include "xdr/block.h"
#include "xdr/trie_proof.h"
#include "xdr/state_query_api.h"


#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <memory>

namespace edce {



struct FrozenDataStructures {

	MemoryDatabase::FrozenDBStateCommitmentTrie db_snapshot;
	FrozenMerkleWorkUnitManager manager_snapshot;
	AccountModificationLog::FrozenTrieT modification_log_snapshot;
	//TransactionUtils::FrozenTxDataTrieT frozen_tx_data;

	FrozenDataStructures(
		MemoryDatabase::FrozenDBStateCommitmentTrie&& db_snapshot, 
		FrozenMerkleWorkUnitManager&& manager_snapshot,
		AccountModificationLog::FrozenTrieT&& modification_log_snapshot)
	: db_snapshot(std::move(db_snapshot)), 
	manager_snapshot(std::move(manager_snapshot)),
	modification_log_snapshot(std::move(modification_log_snapshot)) {}

	/*FrozenDataStructures& operator=(FrozenDataStructures&& other) {
		db_snapshot = std::move(other.db_snapshot);
		manager_snapshot = std::move(other.manager_snapshot);
		frozen_tx_data = std::move(other.frozen_tx_data);
		return *this;
	}*/
};

struct FrozenDataBlock {
	FrozenDataStructures data_structures;
	HashedBlock hashed_block;

	FrozenDataBlock(FrozenDataStructures&& data_structures, HashedBlock hashed_block)
	: data_structures(std::move(data_structures)), hashed_block(hashed_block) {}

	FrozenDataBlock& operator=(FrozenDataBlock&& other)  {
		data_structures = std::move(other.data_structures);
		hashed_block = other.hashed_block;
		return *this;
	}

	FrozenDataBlock(FrozenDataBlock&& other) 
	: data_structures(std::move(other.data_structures)),
	hashed_block(other.hashed_block) {}
};

class FrozenDataCache {

	uint64_t last_cached_block = UINT64_MAX;
	int oldest_idx = -1;
	int newest_idx = -1;
	static constexpr int NUM_CACHED_BLOCKS = 100;

	std::vector<FrozenDataBlock> blocks;

	std::shared_mutex mtx;

	int get_cache_idx(uint64_t block_number);

public:

	FrozenDataCache() : blocks(), mtx() {}

	void add_block(FrozenDataBlock&& block);

	uint64_t most_recent_block_number() {
		return last_cached_block;
	}

	std::unique_ptr<StateQueryResponse> get_account_proof(const AccountID& account, const uint64_t& block_number);

	std::unique_ptr<StateQueryResponse> get_offer_proof(
		const OfferCategory& category, const Price& min_price, const AccountID& owner, const uint64& offer_id, const uint64& block_number);

	std::unique_ptr<StateQueryResponse> get_transaction_proof(
		const AccountID& owner, const uint64& offer_id, const uint64& block_number);

	std::unique_ptr<HashedBlockRange> get_block_header_range(const uint64& start, const uint64& end);

	std::unique_ptr<StateQueryResponse> get_block_header(const uint64& block_number);
};

} /* edce */