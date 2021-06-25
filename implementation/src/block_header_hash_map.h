#pragma once

#include "xdr/types.h"

#include "merkle_trie.h"
#include "merkle_trie_utils.h"

#include "lmdb_wrapper.h"

#include "../config.h"

namespace edce {


struct BlockHeaderHashMapLMDB : public LMDBInstance {
	constexpr static auto DB_NAME = "header_hash_lmdb";

	BlockHeaderHashMapLMDB() : LMDBInstance() {}

	void open_env() {
		LMDBInstance::open_env(std::string(ROOT_DB_DIRECTORY) + std::string(HEADER_HASH_DB));
	}

	void create_db() {
		LMDBInstance::create_db(DB_NAME);
	}

	void open_db() {
		LMDBInstance::open_db(DB_NAME);
	}
};

struct BlockHeaderHashMap {
	using HashWrapper = XdrTypeWrapper<Hash>;
	constexpr static unsigned int KEY_LEN = sizeof(uint64_t);

	using ValueT = HashWrapper;
	using MetadataT = CombinedMetadata<SizeMixin>;

	using TrieT = MerkleTrie<KEY_LEN, ValueT, MetadataT>;
	using FrozenTrieT = FrozenMerkleTrie<KEY_LEN, ValueT, MetadataT>;

	TrieT block_map;

	BlockHeaderHashMapLMDB lmdb_instance;

	uint64_t last_committed_block_number;

public:

	BlockHeaderHashMap() : block_map(), lmdb_instance(), last_committed_block_number(0) {}

	void insert_for_production(uint64_t block_number, const Hash& block_hash);

	bool tentative_insert_for_validation(uint64_t block_number, const Hash& block_hash);
	void rollback_validation();
	void finalize_validation(uint64_t finalized_block_number);

	void freeze_and_hash(Hash& hash) {
		BLOCK_INFO("starting block map freeze_and_hash");
		block_map.freeze_and_hash(hash);
		//block_map._log("block map ");
	}

	void open_lmdb_env() {
		lmdb_instance.open_env();
	}
	void create_lmdb() {
		lmdb_instance.create_db();
	}
	void open_lmdb() {
		lmdb_instance.open_db();
	}

	void persist_lmdb(uint64_t current_block_number);

	uint64_t get_persisted_round_number() {
		return lmdb_instance.get_persisted_round_number();
	}

	void load_lmdb_contents_to_memory();
};

class LoadLMDBHeaderMap : public LMDBLoadingWrapper<BlockHeaderHashMap&> {

	using LMDBLoadingWrapper<BlockHeaderHashMap&> :: generic_do;
public:
	LoadLMDBHeaderMap(
		uint64_t current_block_number,
		BlockHeaderHashMap& main_db) : LMDBLoadingWrapper<BlockHeaderHashMap&>(current_block_number, main_db) {}

	void insert_for_loading(uint64_t block_number, const Hash& block_hash) {
		return generic_do<&BlockHeaderHashMap::insert_for_production>(block_number, block_hash);
	}
};



} /* edce */