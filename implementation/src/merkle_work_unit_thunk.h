#pragma once

#include "xdr/types.h"

namespace edce {

template<typename prefix_t>
struct AccumulateDeletedKeys {

	std::vector<std::pair<prefix_t, Offer>> deleted_keys;

	AccumulateDeletedKeys() : deleted_keys() {}

	void operator() (const prefix_t& key, const Offer& offer) {
		deleted_keys.push_back(std::make_pair(key, offer));
	}
};

template<typename MerkleTrieT>
struct WorkUnitLMDBCommitmentThunk {

	using prefix_t = typename MerkleTrieT::prefix_t;

	//Key equal to the offer that partially executes, if it exists.  0xFF... otherwise.

	prefix_t partial_exec_key;

	int64_t partial_exec_amount = -1;

	Offer preexecute_partial_exec_offer;

	std::vector<Offer> uncommitted_offers_vec;

	MerkleTrieT cleared_offers; // used only for the rollback

	AccumulateDeletedKeys<prefix_t> deleted_keys;

	bool exists_partial_exec;

	uint64_t current_block_number;

	WorkUnitLMDBCommitmentThunk(uint64_t current_block_number)
		: current_block_number(current_block_number) {}

	void set_no_partial_exec() {
		exists_partial_exec = false;
		partial_exec_key.set_max();
		//partial_exec_key.fill(0xFF);
	}

	const bool get_exists_partial_exec() {
		return exists_partial_exec;
	}

	void set_partial_exec(const prefix_t& buf, int64_t amount, Offer offer) {
		partial_exec_key = buf;
		partial_exec_amount = amount;
		preexecute_partial_exec_offer = offer;
		exists_partial_exec = true;
	}

	void reset_trie() {
		cleared_offers.clear_and_reset();
	}
};
} /* edce */
