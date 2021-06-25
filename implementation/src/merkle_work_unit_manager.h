#pragma once 

#include <vector>
#include <cstdint>
#include <mutex>

#include "merkle_trie.h"
#include "merkle_work_unit.h"
#include "work_unit_manager_utils.h"
#include "simple_debug.h"
#include "offer_clearing_params.h"

#include "xdr/block.h"

#include "account_modification_log.h"
#include "fixed_point_value.h"
#include "block_update_stats.h"

#include "lmdb_wrapper.h"

#include <random>

namespace edce {


class MerkleWorkUnitManager {

	std::vector<MerkleWorkUnit> work_units;

	//uint8_t smooth_mult;
	//uint8_t tax_rate;
	uint16_t num_assets;
	
	template<auto func, typename... Args>
	void generic_map(Args... args);

	template<auto func, typename... Args>
	void generic_map_serial(Args... args);

	//primarily useful for reducing fsanitize=thread false positives
	mutable std::mutex mtx;

public:

	using prefix_t = MerkleWorkUnit::MerkleTrieT::prefix_t;


	MerkleWorkUnitManager(
		//uint8_t smooth_mult,
		//uint8_t tax_rate,
		uint16_t num_new_assets)
		: work_units()
		//, smooth_mult(smooth_mult)
		//, tax_rate(tax_rate)
		, num_assets(0) {
		increase_num_traded_assets(num_new_assets);
		num_assets = num_new_assets;
	}

	//void change_approximation_parameters_(uint8_t smooth_mult_, uint8_t tax_rate_);// {
	//	smooth_mult = smooth_mult_;
	//	tax_rate = tax_rate_;

	//	generic_map_serial<&MerkleWorkUnit::change_approximation_parameters_>(smooth_mult, tax_rate);
	//}

	MerkleWorkUnitManager(const MerkleWorkUnitManager& other) = delete;
	MerkleWorkUnitManager(MerkleWorkUnitManager&& other) = delete;

	//std::lock_guard<std::mutex> get_lock() {
	//	mtx.lock();
	//	return {mtx, std::adopt_lock};
	//}

	void clear_();

	void add_offers(int idx, MerkleWorkUnit::MerkleTrieT&& trie) {
		work_units[idx].add_offers(std::move(trie));
	}

	std::optional<Offer> mark_for_deletion(int idx, const prefix_t& key) {
		return work_units[idx].mark_for_deletion(key);
	}

	void unmark_for_deletion(int idx, const prefix_t& key) {
		work_units[idx].unmark_for_deletion(key);
	}

	uint64_t get_persisted_round_number(int idx) {
		return work_units[idx].get_persisted_round_number();
	}

	uint64_t get_min_persisted_round_number() {
		uint64_t min = UINT64_MAX;
		for (const auto& work_unit : work_units) {
			min = std::min(min, work_unit.get_persisted_round_number());
		}
		return min;
	}

	uint64_t get_max_persisted_round_number() {
		uint64_t max = 0;
		for (const auto& work_unit : work_units) {
			max = std::max(max, work_unit.get_persisted_round_number());
		}
		return max;
	}
	void increase_num_traded_assets(uint16_t new_asset_count);

	std::vector<MerkleWorkUnit>& get_work_units() {
		return work_units;
	}

	long unsigned int get_num_work_units() const {
		//return work_units.size();
		return WorkUnitManagerUtils::get_num_work_units_by_asset_count(
			num_assets);
	}

	size_t get_work_unit_size(int idx) const {
		return work_units[idx].size();
	}

	int look_up_idx(const OfferCategory& id) const {
		return WorkUnitManagerUtils::category_to_idx(id, num_assets);
	}

	/*uint8_t get_tax_rate() const {
		return tax_rate;
	}

	uint8_t get_smooth_mult() const {
		return smooth_mult;
	}*/

	uint16_t get_num_assets() const {
		return num_assets;
	}

	size_t get_total_nnz() const;

	void clear_offers_for_production(
		const ClearingParams& params, 
		Price* prices, 
		MemoryDatabase& db, 
		AccountModificationLog& account_log, 
		WorkUnitStateCommitment& clearing_details_out,
		BlockStateUpdateStatsWrapper& state_update_stats);

	void tentative_commit_for_validation(uint64_t current_block_number);

	void commit_for_loading(uint64_t current_block_number) {
		auto num_work_units = work_units.size();

		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, num_work_units),
			[this, &current_block_number] (auto r) {
				for (auto i = r.begin(); i < r.end(); i++) {
					if (work_units[i].get_persisted_round_number() < current_block_number) {
						BLOCK_INFO("doing a tentative_commit_for_validation while loading");
						work_units[i].tentative_commit_for_validation(current_block_number);
					}
				}
			});
	}

	void rollback_validation();
	void finalize_validation();

	void finalize_for_loading(uint64_t current_block_number) {
		auto num_work_units = work_units.size();

		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, num_work_units),
			[this, &current_block_number] (auto r) {
				for (auto i = r.begin(); i < r.end(); i++) {
					if (work_units[i].get_persisted_round_number() < current_block_number) {
						BLOCK_INFO("doing a finalize_validation while loading");
						work_units[i].finalize_validation();
					}
				}
			});
	}

	void load_lmdb_contents_to_memory();

	bool tentative_clear_offers_for_validation(
		MemoryDatabase& db,
		AccountModificationLog& account_modification_log,
		ThreadsafeValidationStatistics& validation_statistics,
		const WorkUnitStateCommitmentChecker& clearing_commitment_log,
		BlockStateUpdateStatsWrapper& state_update_stats);

	bool clear_offers_for_data_loading(
		MemoryDatabase& db,
		AccountModificationLog& account_modification_log,
		ThreadsafeValidationStatistics& validation_statistics,
		const WorkUnitStateCommitmentChecker& clearing_commitment_log,
		const uint64_t current_block_number);

	void commit_for_production(uint64_t current_block_number);
	
	void rollback_thunks(uint64_t current_block_number);

	void generate_metadata_indices();

	void create_lmdb();
	void persist_lmdb(uint64_t current_block_number);

	void persist_lmdb_for_loading(uint64_t current_block_number) {
		auto num_work_units = work_units.size();

		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, num_work_units),
			[this, &current_block_number] (auto r) {
				for (auto i = r.begin(); i < r.end(); i++) {
					if (work_units[i].get_persisted_round_number() < current_block_number) {
						//std::printf("doing a persist lmdb\n");
						work_units[i].persist_lmdb(current_block_number);
					} 
				}
			});
	}
	void open_lmdb_env();
	void open_lmdb();

	void freeze_and_hash(WorkUnitStateCommitment& clearing_details) {
		
		std::lock_guard lock(mtx);

		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, work_units.size()),
			[&clearing_details, this] (auto r) {
				for (unsigned int i = r.begin(); i < r.end(); i++) {
					work_units[i].freeze_and_hash(clearing_details[i].rootHash);
				}
		});
	}

	uint8_t get_max_feasible_smooth_mult(const ClearingParams& clearing_params, Price* prices);
	size_t num_open_offers() const;
};

class LoadLMDBManagerView {

	const uint64_t current_block_number;
	MerkleWorkUnitManager& main_manager;

public:
	using prefix_t = MerkleWorkUnit::MerkleTrieT::prefix_t;

	LoadLMDBManagerView(uint64_t current_block_number, MerkleWorkUnitManager& main_manager)
	: current_block_number(current_block_number)
	, main_manager(main_manager) {

		//std::printf("creating loadlmdb manager view with current_block_number = %lu\n", current_block_number);
	}


	void add_offers(int idx, MerkleWorkUnit::MerkleTrieT&& trie) {
		if (main_manager.get_persisted_round_number(idx) < current_block_number) {
			//std::printf("doing offer insertion\n");
			main_manager.add_offers(idx, std::move(trie));
		}
	}

	std::optional<Offer> mark_for_deletion(int idx, const prefix_t& key) {
		if (main_manager.get_persisted_round_number(idx) < current_block_number) {
			return main_manager.mark_for_deletion(idx, key);
		}
		return Offer();//TODO will cause problem if workunit persisted but db not persisted.
	}

	void unmark_for_deletion (int idx, const prefix_t& key) {
		if (main_manager.get_persisted_round_number(idx) < current_block_number) {
			main_manager.unmark_for_deletion(idx, key);
		}
	}

	unsigned int get_num_work_units() const {
		return main_manager.get_num_work_units();
	}

	int get_num_assets() const {
		return main_manager.get_num_assets();
	}

	int look_up_idx(const OfferCategory& id) const {
		return main_manager.look_up_idx(id);
	}

};

template<typename ManagerType = MerkleWorkUnitManager>
class BaseSerialManager {

protected:
	using main_manager_t = typename std::conditional<std::is_same<LoadLMDBManagerView, ManagerType>::value, LoadLMDBManagerView, MerkleWorkUnitManager&>::type;
	main_manager_t main_manager;
	std::vector<MerkleWorkUnit::MerkleTrieT> new_offers;
	using prefix_t = MerkleWorkUnit::MerkleTrieT::prefix_t;

	prefix_t key_buf;

	void ensure_suffient_new_offers_sz(unsigned int idx) {
		while (idx >= new_offers.size()) {
			new_offers.emplace_back();
		}
	}

	template<typename... Args>
	BaseSerialManager(MerkleWorkUnitManager& main_manager, Args... args)
		: main_manager(args..., main_manager)
		, new_offers()
		, key_buf() {
			//key_buf.fill(0);
		};

public:

	void finish_merge(size_t offset = 0) {
		auto new_offers_sz = std::min<size_t>(new_offers.size(), main_manager.get_num_work_units());
		for (size_t i = 0; i < new_offers_sz; i++) {
			size_t idx = (i + offset) % new_offers_sz;
			TRACE("merging in %lu of %lu.",idx, new_offers_sz);
			main_manager.add_offers(idx, std::move(new_offers[idx]));
		}
		new_offers.clear();
	}

	void partial_finish(size_t idx) {
		if (new_offers.size() > idx) {
			main_manager.add_offers(idx, std::move(new_offers[idx]));
		}
	}
	void partial_finish_conclude() {
		new_offers.clear();
	}

	std::optional<Offer> delete_offer(
		const int idx, 
		const Price min_price, 
		const AccountID owner, 
		const uint64_t offer_id) {
		ensure_suffient_new_offers_sz(idx);
		MerkleWorkUnit::generate_key(min_price, owner, offer_id, key_buf);

		//can't delete an uncommitted offer, so we don't check
		//uncommitted buffer

		return main_manager.mark_for_deletion(idx, key_buf);
	}
	int look_up_idx(const OfferCategory& id) {

		return main_manager.look_up_idx(id);
	}

	const uint16_t get_num_assets() {
		return main_manager.get_num_assets();
	}

	void clear() {
		new_offers.clear();
	}



};

class ProcessingSerialManager : public BaseSerialManager<MerkleWorkUnitManager> {

public:

	ProcessingSerialManager(MerkleWorkUnitManager& manager) : BaseSerialManager(manager) {}

	constexpr static bool maintain_account_log = true;

	void undelete_offer(
		const int idx, 
		const Price min_price, 
		const AccountID owner, 
		const uint64_t offer_id) {
		MerkleWorkUnit::generate_key(min_price, owner, offer_id, BaseSerialManager::key_buf);
		BaseSerialManager<MerkleWorkUnitManager>::main_manager.unmark_for_deletion(idx, key_buf);
	}

	void unwind_add_offer(int idx, const Offer& offer) {
		MerkleWorkUnit::generate_key(&offer, key_buf);
		new_offers.at(idx).perform_deletion(key_buf);
	}

	template<typename OpMetadata, typename LogType>
	void add_offer(int idx, const Offer& offer, OpMetadata& metadata, LogType& log) {
		ensure_suffient_new_offers_sz(idx);
		MerkleWorkUnit::generate_key(&offer, key_buf);
		INFO("offer minPrice:%lu", offer.minPrice);
		INFO("key for insertion:%s", DebugUtils::__array_to_str(
			key_buf.data(), key_buf.size()).c_str());

		//This always succeeds because we have a guarantee on uniqueness
		// of offerId (from uniqueness of sequence numbers
		// and from sequential impl of offerId lowbits)

		//It could only fail if an offer with the same offerId already
		// existed.
		INFO("inserting into idx %d of %d", idx, new_offers.size());

		new_offers.at(idx).insert(key_buf, MerkleWorkUnit::TrieValueT(offer));
	}
};

template<typename ManagerType = MerkleWorkUnitManager>
class ValidatingSerialManager : public BaseSerialManager<ManagerType> {

	const WorkUnitStateCommitmentChecker& clearing_commitment;
	ValidationStatistics activated_supplies;
	ThreadsafeValidationStatistics& main_stats;
	
	void ensure_suffient_new_offers_sz(unsigned int idx) {

		INFO("ensuring minimum size of %lu", idx);
		activated_supplies.make_minimum_size(idx);
		BaseSerialManager<ManagerType>::ensure_suffient_new_offers_sz(idx);
	}

	using BaseSerialManager<ManagerType>::key_buf;
	using BaseSerialManager<ManagerType>::new_offers;

	//constexpr static bool load_lmdb = std::is_same<ManagerType, LoadLMDBManagerView>::value;

public:
	
	constexpr static bool maintain_account_log = std::is_same<ManagerType, MerkleWorkUnitManager>::value;

	template<typename ...Args>
	ValidatingSerialManager(
		MerkleWorkUnitManager& main_manager, 
		const WorkUnitStateCommitmentChecker& clearing_commitment, 
		ThreadsafeValidationStatistics& main_stats,
		 Args... args)
		: BaseSerialManager<ManagerType>(main_manager, args...)
		, clearing_commitment(clearing_commitment)
		, activated_supplies()
		, main_stats(main_stats) {}

	void undelete_offer(
		const int idx, 
		const Price min_price, 
		const AccountID owner, 
		const uint64_t offer_id) {
		//no op
	}

	void unwind_add_offer(int idx, const Offer& offer) {
		//no op
	}

	template<typename OpMetadata, typename LogType>
	void add_offer(int idx, const Offer& offer, OpMetadata& metadata, LogType& log) {
		ensure_suffient_new_offers_sz(idx);
		MerkleWorkUnit::generate_key(&offer, key_buf);
		INFO("validation add offer begin");

		if (clearing_commitment[idx].thresholdKeyIsNull == 1) {
			INFO("thresholdkey is nullptr, preemptively clearing");
			auto sellPrice = clearing_commitment.prices[offer.category.sellAsset];
			auto buyPrice = clearing_commitment.prices[offer.category.buyAsset];
			clear_offer_full(offer, sellPrice, buyPrice, clearing_commitment.tax_rate, metadata.db_view, metadata.source_account_idx);
			activated_supplies[idx].activated_supply += FractionalAsset::from_integral(offer.amount);
			log.log_self_modification(metadata.tx_metadata.sourceAccount, metadata.operation_id);
			return;
		}

		auto key_buf_bytes = key_buf.get_bytes_array();
		auto res = memcmp(clearing_commitment.at(idx).partialExecThresholdKey.data(), key_buf_bytes.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);

		if (res <= 0) {

			INFO("choosing to insert for validation");
			new_offers.at(idx).template insert<RollbackInsertFn> (key_buf, MerkleWorkUnit::TrieValueT(offer));
		}
		// do partial offer clearing later
		/* else if (res == 0) {
			int64_t sell_amount, buy_amount;

			auto sellPrice = clearing_commitment.prices[offer.category.sellAsset];
			auto buyPrice = clearing_commitment.prices[offer.category.buyAsset];

			uint128_t remaining_to_clear_raw;
			PriceUtils::read_unsigned_big_endian(clearing_commitment)

			auto remaining_to_clear = clearing_commitment[idx].partialExecOfferActivationAmount();

			clear_offer_partial(offer, sellPrice, buyPrice, clearing_commitment.tax_rate, remaining_to_clear, db, db_idx, sell_amount, buy_amount);
			new_offers
		} */else {
			INFO("validation preemptive clearing");
			auto sellPrice = clearing_commitment.prices.at(offer.category.sellAsset);
			auto buyPrice = clearing_commitment.prices.at(offer.category.buyAsset);
			clear_offer_full(offer, sellPrice, buyPrice, clearing_commitment.tax_rate, metadata.db_view, metadata.source_account_idx);
			activated_supplies.at(idx).activated_supply += FractionalAsset::from_integral(offer.amount);
			log.log_self_modification(metadata.tx_metadata.sourceAccount, metadata.operation_id);
		}
	}

	void finish_merge() {

		INFO_F(main_stats.log());
		INFO_F(activated_supplies.log());
		//std::printf("finish merge\n");
		main_stats += activated_supplies;
		//std::printf("done activated supplies\n");
		BaseSerialManager<ManagerType>::finish_merge();
	}

	void partial_finish_conclude() {
		main_stats += activated_supplies;
		BaseSerialManager<ManagerType>::partial_finish_conclude();
	}

	void merge_in_other_serial_log(ValidatingSerialManager& other) {
		ensure_suffient_new_offers_sz(other.new_offers.size());
		//std::printf("merging in new offers: my size %lu, other size %lu\n", new_offers.size(), other.new_offers.size());
		for (std::size_t i = 0; i < other.new_offers.size(); i++) {	
			//std::printf("merging in %d\n", i);
			//other.new_offers[i]._log("offers: ");
			new_offers.at(i).merge_in(std::move(other.new_offers.at(i)));
		}
		//std::printf("activated supplies.size() %lu, other size() %lu\n", activated_supplies.size(), other.activated_supplies.size());
		activated_supplies += other.activated_supplies;
		other.new_offers.clear();
	}
};

/*
class FrozenMerkleWorkUnitManager {
	std::vector<FrozenMerkleWorkUnit> work_unit_snapshots;
	int num_assets;
public:
	FrozenMerkleWorkUnitManager(int num_assets) : work_unit_snapshots(), num_assets(num_assets) {}

	void freeze(MerkleWorkUnitManager& manager, xdr::xvector<Hash, MAX_NUM_WORK_UNITS>& hashes) {

		auto& work_units = manager.get_work_units();
		auto size = work_units.size();
		for (unsigned int i = 0; i < size; i++) {
			work_unit_snapshots.emplace_back();
			hashes.emplace_back();
		}

		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, size),
			[&work_units, &hashes, this] (auto r) {
				for (unsigned int i = r.begin(); i < r.end(); i++) {
					work_unit_snapshots[i].construct_frozen_work_unit(work_units[i], hashes[i]);
				}
		});
		
	}
	FrozenMerkleWorkUnitManager(FrozenMerkleWorkUnitManager&& other) : work_unit_snapshots(std::move(other.work_unit_snapshots)), num_assets(other.num_assets) {}

	FrozenMerkleWorkUnitManager& operator= (FrozenMerkleWorkUnitManager&& other) {
		work_unit_snapshots = std::move(other.work_unit_snapshots);
		num_assets = other.num_assets;
		return *this;
	}

	FrozenMerkleWorkUnitManager(const FrozenMerkleWorkUnitManager& other) = delete;

	FrozenMerkleWorkUnit& get_work_unit_snapshot(const OfferCategory& category) {
		return work_unit_snapshots[WorkUnitManagerUtils::category_to_idx(category, num_assets)];
	}
};*/

}
