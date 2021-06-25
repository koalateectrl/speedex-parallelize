#pragma once

#include <unordered_map>
#include <vector>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <utility>
#include <atomic>
#include <cstdio>
#include <execution>
#include <tbb/parallel_for.h>
#include <thread>

#include "xdr/types.h"
#include "xdr/transaction.h"
#include "work_unit.h"
#include "committable_side_effect.h"
#include "operation_metadata.h"

#include "simple_debug.h"

#include "work_unit_manager_utils.h"


/*

The idea for these classes is that there's one WorkUnitManager, and when processing txs (massively parallel), we record orders
(and other state updates) in a SerialManager (one per thread).  These are then handed to the WorkUnitManager
for processing/relaying batches to individual workunits.  

This extra layer could be avoided if std::vector supported concurrent pushbacks (the standard seems to say concurrent writes are 
undefined behavior).

*/

namespace edce {

//Provides utility methods for dealing with a list of workunits
class WorkUnitManager {

/*

	// This section is broken because modifying a vector could cause a reallocation,
	// which might result in move or copy constructors getting called moving WorkUnits around.
	// If an object gets moved while we have a handle on it, we might access invalid memory?
	// i.e. in the extremely weird scenario where we try to access some method, but before we
	// can acquire a mutex, the move gets called and the mutex gets moved away, so we wind update
	// dereferencing an invalid pointer. 

	struct OfferCategoryHash {
		std::size_t operator()(const OfferCategory& w) const noexcept {
	    	//buy_asset never equals sell_asset, so xoring hashes isn't terrible
	        std::size_t h1 = std::hash<uint64_t>{}(w.buyAsset);
	        std::size_t h2 = std::hash<uint64_t>{}(w.sellAsset);
			std::size_t h3 = std::hash<uint64_t>{}(static_cast<uint64_t> (w.type));
			return (h1 ^ h2) ^ (h3 << 1);
	    }
	};

	struct OfferCategoryEquality {
		bool operator()(const OfferCategory& a, const OfferCategory& b) const noexcept {
			return (a.buyAsset == b.buyAsset) && (a.sellAsset == b.sellAsset) && (a.type == b.type);
		}
	};

	std::unordered_map<OfferCategory, int, OfferCategoryHash, OfferCategoryEquality> work_unit_idx_map;
	std::unordered_map<OfferCategory, int, OfferCategoryHash, OfferCategoryEquality> uncommitted_map;

*/

	//int work_unit_committed_size;
	std::vector<WorkUnit> work_units;

//	std::unique_ptr<std::shared_mutex> map_mtx;


	uint8_t smooth_mult;
	uint8_t tax_rate;
	uint16_t num_assets;

	void add_new_offers(int idx, std::vector<Offer>& offers) {
		work_units[idx].add_offers(offers);
	}
	void add_new_offers(int idx, Offer offer) {
		work_units[idx].add_offers(offer);
	}

	template <typename Database>
	bool validate_edit_existing_offer(
		int idx,
		const OperationMetadata<Database>& metadata,
		const Offer& new_offer,
		int64_t* endow_change) {
		return work_units[idx].validate_edit_existing_offer(
			metadata, new_offer, endow_change);
	}

	template<typename Database>
	TransactionProcessingStatus process_edit_existing_offer(
		int idx, 
		const OperationMetadata<Database>& metadata,
		const Offer& new_offer, 
		int64_t* endow_change,
		std::vector<std::unique_ptr<CommittableSideEffect<Database>>>& effects) {

		return work_units[idx].process_edit_existing_offer(
			metadata, new_offer, endow_change, effects);
	}

	template<typename Database>
	void unwind_edit_existing_offer(
		int idx,
		const OperationMetadata<Database>& metadata,
		const Offer& new_offer,
		int64_t* endow_change) {
		work_units[idx].unwind_edit_existing_offer(
			metadata, new_offer, endow_change);
	}

	friend class SerialManager;


public:

	WorkUnitManager(uint8_t smooth_mult, uint8_t tax_rate, uint16_t new_num_assets) 
		: smooth_mult(smooth_mult), tax_rate(tax_rate), num_assets(0) {
			//map_mtx = std::make_unique<std::shared_mutex>();
			increase_num_traded_assets(new_num_assets);
			num_assets = new_num_assets;
		};


	//Not threadsafe with anything.
	void increase_num_traded_assets(uint16_t new_asset_count) {
		if (new_asset_count < num_assets) {
			throw std::runtime_error("Cannot decrease number of assets");//TODO this is doable,
			//but not sure what the right semantics are.
		}
		std::vector<WorkUnit> new_work_units;
		int new_work_unit_count = WorkUnitManagerUtils::get_num_work_units_by_asset_count(new_asset_count);
		new_work_units.reserve(new_work_unit_count);
		for (int i = 0; i < new_work_unit_count; i++) {
			OfferCategory category = WorkUnitManagerUtils::category_from_idx(i, new_asset_count);
			if (category.buyAsset < num_assets && category.sellAsset < num_assets) {
				int old_idx = WorkUnitManagerUtils::category_to_idx(category, num_assets);
				new_work_units.push_back(std::move(work_units[old_idx]));
			} else {
				new_work_units.emplace_back(category, smooth_mult, tax_rate);
			}
		}
		work_units = std::move(new_work_units);
		num_assets = new_asset_count;
	}


	//Calling rollback or look_up_idx (with a new market) causes invalidation of this vector.
	std::vector<WorkUnit>& get_work_units() {
		return work_units;
	}

	int get_num_work_units() {
		//return work_units.size();
		return WorkUnitManagerUtils::get_num_work_units_by_asset_count(num_assets);
	}

	int get_work_unit_size(int index) {
		return work_units[index].size();
	}


	void rollback() {
		TRACE("rollback main_manager");
	//	std::lock_guard<std::shared_mutex> lock(*map_mtx);
	//	TRACE("work_unit_committed_size:%ld", work_unit_committed_size);
	//	work_units.erase(work_units.begin() + work_unit_committed_size, work_units.end());
		for (int i = 0; i < get_num_work_units(); i++) {
			work_units[i].rollback();
		}

//		uncommitted_map.clear();
		TRACE("remaining work_units:%ld", work_units.size());
	}

	void commit_and_preprocess() {
//		std::lock_guard<std::shared_mutex> lock(*map_mtx);
//		work_unit_idx_map.insert(uncommitted_map.begin(), uncommitted_map.end());
//		uncommitted_map.clear();
	//	work_unit_committed_size = work_units.size();
		
		//also 3.0s? what is going on?
		/*int num_threads = 8;
		std::vector<std::thread> threads;
		auto *ptr = &work_units;
		for (int i = 0; i < num_threads; i++) {
			int start = (work_units.size() * i) / num_threads;
			int end = (work_units.size() * (i+1))/num_threads;
			threads.push_back(
				std::thread([start, end, ptr] () {
					for (int j = start; j < end; j++) {
						(*ptr)[j].commit();
						(*ptr)[j].do_preprocessing();
					}
				}));
		}
		for (int i = 0; i < num_threads; i++) {
			threads[i].join();
		}*/

		//3.0s
		/*auto *ptr = &work_units;
		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, work_units.size()),
			[ptr] (auto r) {
				for (int i = r.begin(); i < r.end(); i++) {
					(*ptr)[i].commit();
					(*ptr)[i].do_preprocessing();
				}
			});
		*/
		//3.0s
		/*std::for_each(
			std::execution::par_unseq, 
			work_units.begin(),
			work_units.end(),
			[] (WorkUnit& unit) {
				unit.commit();
				unit.do_preprocessing();
			});*/
		//4.4s
		for (int i = 0; i < get_num_work_units(); i++) {
			work_units[i].commit();
			work_units[i].do_preprocessing();
		}

		//TODO multithread this
	}

	//int look_up_idx(AssetID buy_asset, AssetID sell_asset, OfferType type);
	int look_up_idx(OfferCategory id) {
		return WorkUnitManagerUtils::category_to_idx(id, num_assets);
	}
};

//Single thread only here
class SerialManager {
	WorkUnitManager& main_manager;
	std::vector<std::vector<Offer>> new_offers;

	bool matches_offer(const Offer& offer, const uint64_t candidate_id, const AccountID owner) {
		return offer.offerId == candidate_id
			&& offer.owner == owner;
	}

public:

	SerialManager(WorkUnitManager& main_manager) : main_manager(main_manager) {};

	int look_up_idx(OfferCategory id) {
		return main_manager.look_up_idx(id);
	}

	template<typename Database>
	TransactionProcessingStatus process_create_sell_offer(
		int idx,
		const OperationMetadata<Database>& metadata,
		const CreateSellOfferOp& new_offer) {

		while (idx >= (int) new_offers.size()) {
			new_offers.push_back(std::vector<Offer>());
		}
		if (new_offer.amount < 0) {
			return TransactionProcessingStatus::INVALID_OPERATION;
		}
		INFO("new offer id:%lu", metadata.operation_id);
		//start at sequence number 0
		Offer offer(
			new_offer.category, 
			metadata.operation_id, 
			metadata.tx_metadata.sourceAccount, 
			new_offer.amount, 
			new_offer.minPrice);
		new_offers[idx].emplace_back(offer);
		return TransactionProcessingStatus::SUCCESS;
	}

	template<typename Database>
	bool validate_create_sell_offer(
		int idx, 
		const OperationMetadata<Database>& metadata,
		const CreateSellOfferOp& new_offer) {

		while (idx >= (int) new_offers.size()) {
			new_offers.push_back(std::vector<Offer>());
		}
		if (new_offer.amount < 0) {
			return false;
		}
		Offer offer(
			new_offer.category, 
			metadata.operation_id,
			metadata.tx_metadata.sourceAccount, 
			new_offer.amount, 
			new_offer.minPrice);

		new_offers[idx].emplace_back(offer);
		return true;
	}

	template<typename Database>
	void unwind_create_sell_offer(
		int idx,
		const OperationMetadata<Database>& metadata,
		const CreateSellOfferOp& new_offer) {

		int new_offers_size = new_offers.size();
		if (idx >= new_offers_size) {
			throw std::runtime_error("cannot delete from an unmodified mkt");
		}
		for (auto it = new_offers[idx].begin(); 
			it != new_offers[idx].end(); it++) {


			if (matches_offer((*it), metadata.operation_id, metadata.tx_metadata.sourceAccount)) {
				new_offers[idx].erase(it);
				return;
			}
		}
		throw std::runtime_error("cannot delete a nonexistent offer");
	}
/*
	template<typename Database>
	TransactionProcessingStatus process_manage_sell_offer(
		int idx,
		const OperationMetadata<Database>& metadata,
		const ManageSellOfferOp& manage_offer,
		int64_t *amount_change,
		std::vector<std::unique_ptr<CommittableSideEffect<Database>>>& effects) {

		return main_manager.process_edit_existing_offer(
			idx,
			metadata,
			manage_offer.newOfferParameters,
			amount_change,
			effects);
	}

	template<typename Database>
	bool validate_manage_sell_offer(
		int idx, 
		const OperationMetadata<Database>& metadata,
		const ManageSellOfferOp& manage_offer, 
		int64_t* amount_change) {

		return main_manager.validate_edit_existing_offer(
			idx,
			metadata,
			manage_offer.newOfferParameters,
			amount_change);
	}

	template <typename Database>
	void unwind_manage_sell_offer(
		int idx,
		const OperationMetadata<Database>& metadata,
		const ManageSellOfferOp& manage_offer, 
		int64_t* amount_change) {

		main_manager.unwind_edit_existing_offer(
			idx, 
			metadata, 
			manage_offer.newOfferParameters, 
			amount_change);
	}
*/
	//should not be called at same time as a rollback or look_up_idx (with a new market)
	void finish_merge() {
		int new_offers_size = new_offers.size();
		for (int i = 0; i < new_offers_size; i++) {
			main_manager.add_new_offers(i, new_offers[i]);
		}
		new_offers.clear();
	}


	// only used for benchmarking purposes
	void _add_offer(int idx, Offer offer) {
		main_manager.add_new_offers(idx, offer);
	}
};

}
