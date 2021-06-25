#pragma once

#include "merkle_trie.h"
#include "price_utils.h"
#include "merkle_trie_utils.h"
#include "offer_clearing_params.h"
#include "database_types.h"

#include "simple_debug.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

#include <xdrpp/marshal.h>

#include "lmdb_wrapper.h"

#include "account_modification_log.h"

#include "work_unit_metadata.h"
#include "xdr/block.h"
#include "offer_clearing_logic.h"
#include "work_unit_state_commitment.h"

#include "../config.h"

#include "merkle_work_unit_thunk.h"
#include "merkle_work_unit_helpers.h"

#include "demand_calc_coroutine.h"
#include "block_update_stats.h"

namespace edce {

typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;

struct ObjectiveFunctionInputs {
	std::vector<std::vector<double>> values;

	ObjectiveFunctionInputs(size_t num_assets) {
		values.resize(num_assets);
		for (size_t i = 0; i < num_assets; i++) {
			values[i].resize(num_assets);
		}
	}
};

/*
struct EndowmentPredicate {
	using param_t = int64_t;
	static bool param_at_most_meta(WorkUnitMetadata meta, param_t cleared_endow) {
		return meta.endow <= cleared_endow;
	}
	static bool param_exceeds_meta(WorkUnitMetadata meta, param_t cleared_endow) {
		return meta.endow < cleared_endow;
	}
	static param_t subtract_meta(WorkUnitMetadata meta, param_t cleared_endow) {
		return cleared_endow - meta.endow;
	}
}; */

template<typename Database>
class CompleteClearingFunc {
	const Price sellPrice;
	const Price buyPrice;
	const uint8_t tax_rate;
	Database& db;
	SerialAccountModificationLog& serial_account_log;
public:
	CompleteClearingFunc(Price sellPrice, Price buyPrice, uint8_t tax_rate, Database& db, SerialAccountModificationLog& serial_account_log) 
	: sellPrice(sellPrice), buyPrice(buyPrice), tax_rate(tax_rate), db(db), serial_account_log(serial_account_log) {}

	void operator() (const Offer& offer) {

		if (PriceUtils::a_over_b_lt_c(sellPrice, buyPrice, offer.minPrice)) {
			std::printf("%f %f\n", PriceUtils::to_double(sellPrice)/PriceUtils::to_double(buyPrice), PriceUtils::to_double(offer.minPrice));
			std::fflush(stdout);
			throw std::runtime_error("Trying to clear an offer with bad price");
		}
		if (offer.amount == 0) {
			throw std::runtime_error("offer amount was 0!");
		}
		account_db_idx idx;
		auto result = db.lookup_user_id(offer.owner, &idx);
		if (!result) {
			throw std::runtime_error("Offer in manager from nonexistent account");
		}

		//int64_t sell_amount = -offer.amount;
		//int64_t buy_amount;// = PriceUtils::wide_multiply_val_by_a_over_b(
		//	offer.amount, 
		//	sellPrice,
		//	buyPrice);

		//db.transfer_escrow(idx, offer.category.sellAsset, sell_amount);
		//db.transfer_available(idx, offer.category.buyAsset, buy_amount);

		clear_offer_full(offer, sellPrice, buyPrice, tax_rate, db, idx);

		serial_account_log.log_self_modification(offer.owner, offer.offerId);
	}
};


using OfferWrapper = XdrTypeWrapper<Offer>;
constexpr static int _WORKUNIT_KEY_LEN = PriceUtils::PRICE_BYTES + sizeof(AccountID) + sizeof(uint64_t);
using WorkUnit_TrieValueT = OfferWrapper;
using WorkUnit_TrieMetadataT = CombinedMetadata<DeletableMixin, SizeMixin, RollbackMixin, WorkUnitMetadata>;

using WorkUnit_MerkleTrieT = MerkleTrie<_WORKUNIT_KEY_LEN, WorkUnit_TrieValueT, WorkUnit_TrieMetadataT, false>; // no locks on individual trie nodes


class WorkUnitLMDB : public LMDBInstance {
	using LMDBCommitmentThunk = WorkUnitLMDBCommitmentThunk<WorkUnit_MerkleTrieT>;//<std::array<unsigned char, _WORKUNIT_KEY_LEN>>;

	std::vector<LMDBCommitmentThunk> thunks;

	using MerkleTrieT = WorkUnit_MerkleTrieT;

public:
	
	//The point of this lock is to make sure that during normal processing/validation,
	// async flushes to lmdb don't conflict with operations of new blocks.
	std::unique_ptr<std::mutex> mtx;

	WorkUnitLMDB() 
		: LMDBInstance{0x40000000}
		, mtx(std::make_unique<std::mutex>()) {} //mapsize = 2^20 = 1 million (dropped from 1 trillion)

	void write_thunks(dbenv::wtxn& wtx, const uint64_t current_block_number, bool debug = false);
	void clear_thunks(uint64_t current_block_number);

	//used for testing only, particularly wrt tatonnement_sim
	void clear_() {
		thunks.clear();
	}

	std::vector<LMDBCommitmentThunk>& get_thunks_ref() {
		return thunks;
	}

	//should lock before using reference
	LMDBCommitmentThunk& add_new_thunk(uint64_t current_block_number) {
		if (thunks.size()) {
			if (thunks.back().current_block_number + 1 != current_block_number) {
				throw std::runtime_error("thunks in the wrong order!");
			}
		}
		thunks.emplace_back(current_block_number);
		return thunks.back();
	}
	//should lock before using reference
	LMDBCommitmentThunk& get_top_thunk() {
		if (thunks.size() == 0) {
			throw std::runtime_error("can't get top thunk when thunks.size() == 0");
		}
		return thunks.back();
	}

	void pop_top_thunk() {
		std::lock_guard lock(*mtx);
		pop_top_thunk_nolock();
	}

	void pop_top_thunk_nolock() {

		if (thunks.size() == 0) {
			throw std::runtime_error("can't pop_back from empty thunks list");
		}
		thunks.pop_back();
	}
};

class MerkleWorkUnit {

	const OfferCategory category;
	//uint8_t smooth_mult;
	//uint8_t tax_rate;

	//constexpr static int PRICE_BYTES = PriceUtils::PRICE_BYTES;
	
public:
	constexpr static int WORKUNIT_KEY_LEN = _WORKUNIT_KEY_LEN;//PRICE_BYTES + sizeof(AccountID) + sizeof(uint64_t);

	using TrieValueT = WorkUnit_TrieValueT;//OfferWrapper;
	using TrieMetadataT = WorkUnit_TrieMetadataT;//CombinedMetadata<DeletableMixin, SizeMixin, RollbackMixin, WorkUnitMetadata>;
	using MerkleTrieT = WorkUnit_MerkleTrieT;//MerkleTrie<WORKUNIT_KEY_LEN, TrieValueT, TrieMetadataT, false>; // no locks on individual trie nodes
	using FrozenMerkleTrieT = typename MerkleTrieT::FrozenT;
private:

	MerkleTrieT committed_offers;

	MerkleTrieT uncommitted_offers;
	WorkUnitLMDB lmdb_instance;

	struct FuncWrapper {
		static Price eval(const MerkleTrieT::prefix_t& buf) {
			return PriceUtils::read_price_big_endian(buf);
		}
	};

	using IndexType = IndexedMetadata
					<
						EndowAccumulator,
						Price,
						FuncWrapper
					>;

	std::vector<IndexType> indexed_metadata;

	uint64_t get_persisted_round_number() const {
		return lmdb_instance.get_persisted_round_number();
	}

	void tentative_commit_for_validation(uint64_t current_block_number); //creates thunk
	void commit_for_production(uint64_t current_block_number); // creates lmdb thunk

	void generate_metadata_index();

//	void change_approximation_parameters_(uint8_t smooth_mult_, uint8_t tax_rate_) {
//		smooth_mult = smooth_mult_;
//		tax_rate = tax_rate_;
//	}


	void undo_thunk(WorkUnitLMDBCommitmentThunk<MerkleTrieT>& thunk);

	void persist_lmdb(uint64_t current_block_number);

	void add_offers(MerkleTrieT&& offers) {
		INFO("merging in to \"%d %d\"", category.sellAsset, category.buyAsset);
		uncommitted_offers.merge_in(std::move(offers));
		//uncommitted_additional_offers.emplace_back(std::move(offers));
	}

	std::optional<Offer> mark_for_deletion(const MerkleTrieT::prefix_t key) {
		return committed_offers.mark_for_deletion(key);
	}

	std::optional<Offer> unmark_for_deletion(const MerkleTrieT::prefix_t key) {
		return committed_offers.unmark_for_deletion(key);
	}

	friend class MerkleWorkUnitManager;

	void rollback_validation();
	void finalize_validation();

	void rollback_thunks(uint64_t current_block_number);

	std::string get_lmdb_env_name() {
		return std::string(ROOT_DB_DIRECTORY) + std::string(OFFER_DB) + std::to_string(category.sellAsset) + "_" + std::to_string(category.buyAsset) + std::string("/");
	}
	std::string get_lmdb_db_name() {
		return "offers";
	}

	void load_lmdb_contents_to_memory();

public:
	MerkleWorkUnit(OfferCategory category) //, uint8_t smooth_mult, uint8_t tax_rate)
	: category(category), 
	//  smooth_mult(smooth_mult), 
	//  tax_rate(tax_rate),
	  committed_offers(),
	  uncommitted_offers(),
	  lmdb_instance(), 
	  indexed_metadata() {

	  	//sanity check
		if (OFFER_KEY_LEN_BYTES != MerkleWorkUnit::WORKUNIT_KEY_LEN) {
			throw std::runtime_error("key len in xdr must equal key len in memory");
		}

	}

	void clear_() {
		uncommitted_offers.clear();
		committed_offers.clear();
		indexed_metadata.clear();
		lmdb_instance.clear_();
	}

	void log() {
		committed_offers._log("committed_offers: ");
	}


	void process_clear_offers(
		const WorkUnitClearingParams& params, 
		const Price* prices, 
		const uint8_t& tax_rate, 
		MemoryDatabase& db,
		SerialAccountModificationLog& serial_account_log,
		SingleWorkUnitStateCommitment& clearing_commitment_log,
		BlockStateUpdateStatsWrapper& state_update_stats);
	
	//make the inserted things marked with some kind of metadata, which is deletable
	bool tentative_clear_offers_for_validation(
		MemoryDatabase& db,
		SerialAccountModificationLog& serial_account_log,
		SingleValidationStatistics& validation_statistics,
		const SingleWorkUnitStateCommitmentChecker& local_clearing_log,
		const WorkUnitStateCommitmentChecker& clearing_commitment_log,
		BlockStateUpdateStatsWrapper& state_update_stats);

	void open_lmdb_env() {
		lmdb_instance.open_env(get_lmdb_env_name());
	}

	void create_lmdb() {
		auto name = get_lmdb_db_name();
		lmdb_instance.create_db(name.c_str());
	}

	void open_lmdb() {
		auto name = get_lmdb_db_name();
		lmdb_instance.open_db(name.c_str());
	}

	void freeze_and_hash(Hash& hash_buf) {
		committed_offers.freeze_and_hash(hash_buf);
	}

	std::pair<Price, Price> get_execution_prices(const Price* prices, const uint8_t smooth_mult) const;
	std::pair<Price, Price> get_execution_prices(Price sell_price, Price buy_price, const uint8_t smooth_mult) const;

	EndowAccumulator get_metadata(Price p) const;
	GetMetadataTask coro_get_metadata(Price p, EndowAccumulator& endow_out, DemandCalcScheduler& scheduler) const;

	void calculate_demands_and_supplies(
		const Price* prices, 
		uint128_t* demands_workspace, 
		uint128_t* supplies_workspace,
		const uint8_t smooth_mult);

	void calculate_demands_and_supplies_from_metadata(
		const Price* prices, 
		uint128_t* demands_workspace,
		uint128_t* supplies_workspace,
		const uint8_t smooth_mult, 
		const EndowAccumulator& metadata_partial,
		const EndowAccumulator& metadata_full);

	uint8_t max_feasible_smooth_mult(int64_t amount, const Price* prices) const;
	double max_feasible_smooth_mult_double(int64_t amount, const Price* prices) const;

	size_t num_open_offers() const;

	std::pair<uint64_t, uint64_t> get_supply_bounds(const Price* prices, const uint8_t smooth_mult) const;
	std::pair<uint64_t, uint64_t> get_supply_bounds(Price sell_price, Price buy_price, const uint8_t smooth_mult) const;

	const std::vector<IndexType>& get_indexed_metadata() const {
		return indexed_metadata;
	}

	size_t get_index_nnz() const {
		return indexed_metadata.size() - 1; // first entry of index is 0, so ignore
	}

	OfferCategory get_category() const {
		return category;
	}

	size_t size() const {
		return committed_offers.size();
	}

	static void generate_key(const Offer* offer, MerkleTrieT::prefix_t& buf) {
		generate_key(offer -> minPrice, offer -> owner, offer -> offerId, buf);
	}
	static void generate_key(const Offer& offer, MerkleTrieT::prefix_t& buf) {
		generate_key(&offer, buf);
	}

	static void generate_key(
		const Price minPrice, 
		const AccountID owner, 
		const uint64_t offer_id,
		MerkleTrieT::prefix_t& buf) {
		size_t offset = 0;
		PriceUtils::write_price_big_endian(buf, minPrice);
		offset += PriceUtils::PRICE_BYTES;
		PriceUtils::write_unsigned_big_endian(buf, owner, offset);
		offset += sizeof(owner);
		PriceUtils::write_unsigned_big_endian(buf, offer_id, offset);
	}
};


/*struct FrozenMerkleWorkUnit {
	OfferCategory category;
	std::optional<MerkleWorkUnit::FrozenMerkleTrieT> trie_snapshot;

	void construct_frozen_work_unit(MerkleWorkUnit& work_unit, Hash& hash) {
		auto frozen_work_unit = work_unit.freeze(hash.data());
		trie_snapshot = std::move(frozen_work_unit);
	}

	FrozenMerkleWorkUnit() : category(), trie_snapshot() {}
	FrozenMerkleWorkUnit(FrozenMerkleWorkUnit&& other) : category(other.category), trie_snapshot(std::move(other.trie_snapshot)) {}


};*/
/*
struct LMDBDelSideEffectFn {

	dbenv::wtxn& txn;
	MDB_dbi dbi;

	using prefix_t = MerkleWorkUnit::MerkleTrie::prefix_t;

	void operator() (const prefix_t& prefix) {
		//unsigned char prefix_copy [MerkleWorkUnit::WORKUNIT_KEY_LEN];

		//memcpy(prefix_copy, prefix, MerkleWorkUnit::WORKUNIT_KEY_LEN);

		prefix_t prefix_copy = prefix;

		dbval key_to_delete(prefix_copy.data(), prefix_copy.size());

		auto deleted = txn.del(dbi, key_to_delete);

		if (!deleted) {
			throw std::runtime_error("tried to delete a key not in lmdb");
		}
	}
};*/

} /* namespace edce */
