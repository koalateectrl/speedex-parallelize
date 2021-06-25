#include "frozen_data_cache.h"

namespace edce {

void FrozenDataCache::add_block(FrozenDataBlock&& block) {
	std::lock_guard lock(mtx);
	last_cached_block = block.hashed_block.block.blockNumber;
	if (blocks.size() >= NUM_CACHED_BLOCKS) {
		blocks[oldest_idx] = std::move(block);
		oldest_idx = (oldest_idx + 1) % NUM_CACHED_BLOCKS;
		newest_idx = (newest_idx + 1) % NUM_CACHED_BLOCKS;
	} else {
		blocks.emplace_back(std::move(block));
		oldest_idx = 0;
		newest_idx++;
	}
}


int FrozenDataCache::get_cache_idx(uint64_t block_number) {
	if (oldest_idx == -1) return -1;
	if (block_number <= last_cached_block && last_cached_block - blocks.size() < block_number) {
		return (last_cached_block-block_number + oldest_idx) % NUM_CACHED_BLOCKS; 
	}
	if (block_number > last_cached_block) {
		return newest_idx;
	}
	return -1;
}

std::unique_ptr<StateQueryResponse> FrozenDataCache::get_account_proof(const AccountID& account, const uint64_t& block_number) {
	std::unique_ptr<StateQueryResponse> res(new StateQueryResponse);
	std::shared_lock lock(mtx);

	auto idx = get_cache_idx(block_number);

	if (idx == -1) {
		res -> body.status(QueryStatus::BLOCK_ID_TOO_OLD);
		return res;
	}

	res -> body.status(QueryStatus::PROOF_SUCCESS);

	FrozenDataBlock& block = blocks[idx];

	unsigned char key_buf[MemoryDatabase::TRIE_KEYLEN];

	MemoryDatabase::write_trie_key(key_buf, account);

	res -> body.result() = block.data_structures.db_snapshot.generate_proof(key_buf);

	res -> blockId = block.hashed_block.block.blockNumber;

	return res;
}

std::unique_ptr<StateQueryResponse> FrozenDataCache::get_offer_proof(
	const OfferCategory& category, const Price& min_price, const AccountID& owner, const uint64& offer_id, const uint64& block_number)
{
	std::unique_ptr<StateQueryResponse> res(new StateQueryResponse);
	std::shared_lock lock(mtx);

	auto idx = get_cache_idx(block_number);

	if (idx == -1) {
		res -> body.status(QueryStatus::BLOCK_ID_TOO_OLD);
		return res;
	}

	res -> body.status(QueryStatus::PROOF_SUCCESS);

	FrozenDataBlock& block = blocks[idx];

	unsigned char key_buf[MerkleWorkUnit::WORKUNIT_KEY_LEN];

	MerkleWorkUnit::generate_key(
		min_price, 
		owner, 
		offer_id,
		key_buf);

	res -> body.result() = block.data_structures.manager_snapshot.get_work_unit_snapshot(category).trie_snapshot->generate_proof(key_buf);

	res -> blockId = block.hashed_block.block.blockNumber;

	return res;
}


std::unique_ptr<StateQueryResponse> FrozenDataCache::get_transaction_proof(
	const AccountID& owner, const uint64& offer_id, const uint64& block_number) {

	throw std::runtime_error("todo unimplemented");


/*
	std::unique_ptr<StateQueryResponse> res(new StateQueryResponse);
	std::shared_lock lock(mtx);

	auto idx = get_cache_idx(block_number);

	if (idx == -1) {
		res -> body.status(QueryStatus::BLOCK_ID_TOO_OLD);
		return res;
	}

	res -> body.status(QueryStatus::PROOF_SUCCESS);

	FrozenDataBlock& block = blocks[idx];

	unsigned char key_buf[TransactionUtils::TX_KEY_LEN];

	TransactionUtils::write_tx_trie_key(key_buf, owner, offer_id);

	res -> body.result() = block.data_structures.frozen_tx_data.generate_proof(key_buf);

	res -> blockId = block.hashed_block.block.blockNumber;

	return res;*/
}

std::unique_ptr<HashedBlockRange> FrozenDataCache::get_block_header_range(const uint64& start, const uint64& end) {

	throw std::runtime_error("TODO unimplemented");
}


std::unique_ptr<StateQueryResponse> FrozenDataCache::get_block_header(const uint64& block_number) {

	std::unique_ptr<StateQueryResponse> res(new StateQueryResponse);
	std::shared_lock lock(mtx);

	auto idx = get_cache_idx(block_number);

	if (idx == -1) {
		res -> body.status(QueryStatus::BLOCK_ID_TOO_OLD);
		return res;
	}

	res -> body.status(QueryStatus::HEADER_SUCCESS);

	FrozenDataBlock& block = blocks[idx];

	res -> body.header() = block.hashed_block;

	return res;
}

} /* edce */