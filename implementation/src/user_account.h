#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <atomic>

#include "revertable_asset.h"

#include "xdr/types.h"
#include "xdr/transaction.h"
#include "xdr/database_commitments.h"

#include "simple_debug.h"
#include "merkle_trie.h"

#include <sodium.h>

#include "lmdb_wrapper.h"


namespace edce {


class UserAccount {

	static_assert(MAX_OPS_PER_TX == RESERVED_SEQUENCE_NUM_LOWBITS + 1, "ops mismatch");
	static_assert(__builtin_popcount(MAX_OPS_PER_TX) == 1, "should be power of two");

	using amount_t = typename RevertableAsset::amount_t;

	mutable std::mutex uncommitted_assets_mtx;
	//mutable std::mutex sequence_number_mtx;
	//std::unique_ptr<std::mutex> uncommitted_assets_mtx;
	//std::unique_ptr<std::mutex> sequence_number_mtx;

	//TODO using a map here really slows things down.
	std::vector<RevertableAsset> owned_assets;
	std::vector<RevertableAsset> uncommitted_assets;

	//std::unordered_map<int, RevertableAsset> owned_assets;
	//std::unordered_map<int, RevertableAsset> uncommitted_assets;

	std::atomic<uint64_t> sequence_number_vec;

	//std::set<uint64_t> current_reserved_ids;
	//std::set<uint64_t> current_committed_ids;

	uint64_t last_committed_id;
	//uint64_t cur_max_committed_id;

	template<typename return_type>
	return_type operate_on_asset(
		unsigned int asset, 
		amount_t amount, 
		return_type (*func)(RevertableAsset&, const amount_t&)) {
		unsigned int owned_assets_size = owned_assets.size();
		if (asset >= owned_assets_size) {
			std::lock_guard<std::mutex> lock(uncommitted_assets_mtx);
			while (asset >= owned_assets_size + uncommitted_assets.size()) {
				uncommitted_assets.emplace_back();
			}
			return func(uncommitted_assets[asset - owned_assets_size], amount);
		}
		return func(owned_assets[asset], amount);
	}

	void operate_on_asset(unsigned int asset, amount_t amount, void (*func)(RevertableAsset&, const amount_t&)) {
		unsigned int owned_assets_size = owned_assets.size();
		if (asset >= owned_assets_size) {
			std::lock_guard lock(uncommitted_assets_mtx);
			while (asset >= owned_assets_size + uncommitted_assets.size()) {
				uncommitted_assets.emplace_back();
			}
			func(uncommitted_assets[asset - owned_assets_size], amount);
			return;
		}
		func(owned_assets[asset], amount);
	}

	//bool _modified_since_last_commit_production;
	//bool _modified_since_last_checkpoint;

	AccountID owner;
	PublicKey pk;

	static_assert(sizeof(pk) == crypto_sign_PUBLICKEYBYTES, "pk size should be pkbytes");

public:

	UserAccount(AccountID owner, PublicKey public_key) :
		uncommitted_assets_mtx(),//std::make_unique<std::mutex>()),
	//	sequence_number_mtx(),//std::make_unique<std::mutex>()),
		last_committed_id(0),
		//cur_max_committed_id(0),
		//_modified_since_last_commit_production(true),
		//_modified_since_last_checkpoint(true),
		owner(owner)
		, pk(public_key)
	{}


	//We cannot move UserAccounts and concurrently modify them.
	UserAccount(UserAccount&& other)
		: uncommitted_assets_mtx(),
		//sequence_number_mtx(),
		owned_assets(std::move(other.owned_assets)),
		uncommitted_assets(std::move(other.uncommitted_assets)),

		sequence_number_vec(other.sequence_number_vec.load(std::memory_order_acquire)),
		//current_reserved_ids(other.current_reserved_ids),
		//current_committed_ids(other.current_committed_ids),
		last_committed_id(other.last_committed_id),
		//cur_max_committed_id(other.cur_max_committed_id),
		//_modified_since_last_commit_production(other._modified_since_last_commit_production),
		//_modified_since_last_checkpoint(other._modified_since_last_checkpoint),
		owner(other.owner)
		, pk(other.pk) {
			//memcpy(pk, other.pk, crypto_sign_PUBLICKEYBYTES);
		}


	//Needed only for vector.erase, for some dumb reason
	UserAccount& operator=(UserAccount&& other) {
		owned_assets = std::move(other.owned_assets);
		uncommitted_assets = std::move(other.uncommitted_assets);
		//current_reserved_ids = other.current_reserved_ids;
		//current_committed_ids = other.current_committed_ids;
		sequence_number_vec = other.sequence_number_vec.load(std::memory_order_acquire);
		last_committed_id = other.last_committed_id;
		//cur_max_committed_id = other.cur_max_committed_id;
		//_modified_since_last_checkpoint = other._modified_since_last_checkpoint;
		//_modified_since_last_commit_production = other._modified_since_last_commit_production;
		owner = other.owner;
		pk = other.pk;
//		memcpy(pk, other.pk, crypto_sign_PUBLICKEYBYTES);
		return *this;
	}

	UserAccount(const AccountCommitment& commitment) 
		: owned_assets()
		, uncommitted_assets()
		//, current_reserved_ids()
		//, current_committed_ids()
		, sequence_number_vec(0)
		, last_committed_id(commitment.last_committed_id)
		//, cur_max_committed_id(last_committed_id)
		//, _modified_since_last_commit_production(true)
		//, _modified_since_last_checkpoint(false)
		, owner(commitment.owner)
		, pk(commitment.pk) {

			for (unsigned int i = 0; i < commitment.assets.size(); i++) {
				if (commitment.assets[i].asset < owned_assets.size()) {
					throw std::runtime_error("assets in commitment should be sorted");
				}
				while (owned_assets.size() < commitment.assets[i].asset) {
					owned_assets.emplace_back(0);
				}
				owned_assets.emplace_back(commitment.assets[i].amount_available);
			}
			//memcpy(pk, commitment.pk.data(), crypto_sign_PUBLICKEYBYTES);
		}


	PublicKey get_pk() const {
		return pk;
	}

	AccountID get_owner() const {
		return owner;
	}

	void stringify() const {
		for (auto& asset : owned_assets) {
			asset.stringify();		}
	}

	void transfer_available(unsigned int asset, amount_t amount) {
		operate_on_asset(asset, amount, [] (RevertableAsset& asset, const amount_t& amount) {asset.transfer_available(amount);});
	}

	void transfer_escrow(unsigned int asset, amount_t amount) {
		operate_on_asset(asset, amount, [] (RevertableAsset& asset, const amount_t& amount) {asset.transfer_escrow(amount);});
	}

	void escrow(unsigned int asset, amount_t amount) {
		operate_on_asset(asset, amount, [] (RevertableAsset& asset, const amount_t& amount) {asset.escrow(amount);});
	}

	bool conditional_transfer_available(unsigned int asset, amount_t amount) {
		return operate_on_asset<bool>(asset, amount, [] (RevertableAsset& asset, const amount_t& amount) {return asset.conditional_transfer_available(amount);});
	}

	bool conditional_escrow(unsigned int asset, amount_t amount) {
		return operate_on_asset<bool>(asset, amount, [] (RevertableAsset& asset, const amount_t& amount) {return asset.conditional_escrow(amount);});
	}

	amount_t lookup_available_balance(unsigned int asset) {
		[[maybe_unused]]
		amount_t unused = 0;
		return operate_on_asset<amount_t>(asset, unused, [] (RevertableAsset& asset, [[maybe_unused]] const amount_t& amount) {return asset.lookup_available_balance();});
	}

	//not threadsafe with rollback/commit
	TransactionProcessingStatus reserve_sequence_number(
		uint64_t sequence_number);

	void release_sequence_number(
		uint64_t sequence_number);

	void commit_sequence_number(
		uint64_t sequence_number);

	void commit();
	void rollback();
	bool in_valid_state();
	
	AccountCommitment produce_commitment() const;
	AccountCommitment tentative_commitment() const;

	static dbval produce_lmdb_key(const AccountID& owner);
	static AccountID read_lmdb_key(const dbval& key);

/*
	bool modified_since_last_commit_production();
	void mark_unmodified_since_last_commit_production();

	bool modified_since_last_checkpoint();
	void mark_unmodified_since_last_checkpoint();
*/
	
	void log() {
		for (unsigned int i = 0; i < owned_assets.size(); i++) {
			std::printf ("%u=%ld ", i, owned_assets[i].lookup_available_balance());
		}
		std::printf("\n");
	}

};

}
