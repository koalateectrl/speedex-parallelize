#include "merkle_work_unit.h"

#include "work_unit_manager_utils.h"

#include "database.h"
#include "utils.h"

#include <thread>

#include <tbb/task_arena.h>


namespace edce {






/*
Validation workflow:

SerialTransactionValidator runs through offer lists, autoclears anything below partial execution threshold listed in block.  Logs amount of supply activated.
All new offers added are marked as rollbackable.


Next, we call tentative_commit_for_validation.  This merges tries together (needed to compute hashes), starts persistence thunk preparation.

THen we call tentative_clear_offers_for_validation.  This is responsible for marking cleared entries as deleted, logging supply activation, and making the partial exec modifications.
returns true if supply activations add up.  Also should finish adding entries to thunk.

Then we do external validity checks.  Check hashes, clearing, supply activations add up, etc.

If those pass, we call finalize_validation.  If fails, we call rollback_validation.
*/
void MerkleWorkUnit::tentative_commit_for_validation(uint64_t current_block_number) {
	{
		std::lock_guard lock(*lmdb_instance.mtx);
		auto& thunk = lmdb_instance.add_new_thunk(current_block_number);
		thunk.uncommitted_offers_vec = uncommitted_offers.accumulate_values<std::vector<Offer>>();
		auto& accumulate_deleted_keys = thunk.deleted_keys;
		committed_offers.perform_marked_deletions(accumulate_deleted_keys);
	}
	committed_offers.merge_in(std::move(uncommitted_offers));
	uncommitted_offers.clear();
}

void MerkleWorkUnit::undo_thunk(WorkUnitLMDBCommitmentThunk<MerkleTrieT>& thunk) {
	std::printf("starting thunk undo\n");
	for (auto& kv : thunk.deleted_keys.deleted_keys) {
		committed_offers.insert(kv.first, OfferWrapper(kv.second));
	}

	std::printf("done inserting deleted keys\n");

	thunk.cleared_offers.clean_singlechild_nodes(thunk.partial_exec_key);

	committed_offers.merge_in(std::move(thunk.cleared_offers));

	for (auto& offer : thunk.uncommitted_offers_vec) {
		MerkleTrieT::prefix_t key;
		generate_key(offer, key);
		committed_offers.mark_for_deletion(key);
	}
	committed_offers.perform_marked_deletions();
	std::printf("done marked deletions\n");

	if (thunk.get_exists_partial_exec()) {
		auto bytes_array = thunk.partial_exec_key.get_bytes_array();
		std::printf("key:%s\n", DebugUtils::__array_to_str(bytes_array.data(), bytes_array.size()).c_str());
		committed_offers.insert(thunk.partial_exec_key, OfferWrapper(thunk.preexecute_partial_exec_offer));
	}
	std::printf("done thunk undo\n");
}
/*
void MerkleWorkUnit::tentative_commit_for_validation(uint64_t current_block_number) {

	//if (lmdb_instance) {
	{
		std::lock_guard lock(*lmdb_instance.mtx);
		auto& thunk = lmdb_instance.add_new_thunk(current_block_number);
		thunk.uncommitted_offers_vec = uncommitted_offers.accumulate_values<std::vector<Offer>>();

		//std::printf("new offers size = %lu\n", thunk.uncommitted_offers_vec.size());
	}
	committed_offers.merge_in(std::move(uncommitted_offers));
	uncommitted_offers.clear();
}*/

void MerkleWorkUnit::finalize_validation() {
	committed_offers.clear_rollback();  //TODO on a higher powered machine, maybe parallelize?

	if (uncommitted_offers.size() != 0) {
		throw std::runtime_error("shouldn't have uncommitted_offers nonempty when calling finalize");
	}

	//if (lmdb_instance) {
	//	std::lock_guard lock(*lmdb_instance.mtx);
	//	auto& thunk = lmdb_instance.get_top_thunk();
	//	auto& accumulate_deleted_keys = thunk.deleted_keys;
	//	committed_offers.perform_marked_deletions(accumulate_deleted_keys);
	//} else {
	//	committed_offers.perform_marked_deletions();
	//}
}



//rolls back tentative_commit_for_validation, along with transaction side effects
void MerkleWorkUnit::rollback_validation() {//const SingleWorkUnitStateCommitmentChecker& clearing_commitment_log) {
	//committed_offers.clear_marked_deletions();
	
	uncommitted_offers.clear();
	committed_offers.do_rollback(); // takes care of new round's uncommitted offers, so we can safely clear them from the thunk.

	std::lock_guard(*lmdb_instance.mtx);

	auto& thunk = lmdb_instance.get_top_thunk();

	thunk.uncommitted_offers_vec.clear();

	undo_thunk(thunk);



	//undoing partial execution
	/*auto delete_result = committed_offers.perform_deletion(clearing_commitment_log.partialExecThresholdKey);
	if (delete_result) {
		//delete_result might be nullopt if the partial exec offer was new in this round, i.e. if rollback already got rid of it.
		TrieValueT offer = *delete_result;
		offer.amount += clearing_commitment_log.partialExecOfferActivationAmount().ceil();
		try {
			committed_offers.insert(clearing_commitment_log.partialExecThresholdKey, offer);
		} catch(...) {
			committed_offers._log("post insert ");
			std::fflush(stdout);
			throw;
		}
	}*/

	lmdb_instance.pop_top_thunk_nolock();
}

void MerkleWorkUnit::commit_for_production(uint64_t current_block_number) {
	tentative_commit_for_validation(current_block_number);
	generate_metadata_index();
}
/*	{
		std::lock_guard lock(*lmdb_instance.mtx);
		auto& thunk = lmdb_instance.add_new_thunk(current_block_number);
		auto& accumulate_deleted_keys = thunk.deleted_keys;
		committed_offers.perform_marked_deletions(accumulate_deleted_keys);

		INTEGRITY_CHECK_F(
			if (!committed_offers.metadata_integrity_check()) {
				throw std::runtime_error("left in bad state somehow");
			}
		);
	}

	//std::printf("finished integrity check%lu\n", std::this_thread::get_id());
	{
		std::lock_guard lock(*lmdb_instance.mtx);
		auto& thunk = lmdb_instance.get_top_thunk();
		try {
			thunk.uncommitted_offers_vec = uncommitted_offers.accumulate_values<std::vector<Offer>>();
		} catch(...) {
			std::printf("error in uncommitted_offers.accumulate_values!\n");
			uncommitted_offers._log("offers ");
			throw;	
		}

		INTEGRITY_CHECK_F(
			if (thunk.uncommitted_offers.size() != uncommitted_offers.size()) {
				throw std::runtime_error("deep copy sz error");
			}
			if (!thunk.uncommitted_offers.metadata_integrity_check()) {
				throw std::runtime_error("thunk.uncommitted_offers metadata error");
			}
		);
	}

	//std::printf("thunk added%lu\n", std::this_thread::get_id());
	//std::printf("uncommitted_offers size = %u\n", uncommitted_offers.size());
	//std::printf("committed offers size = %u\n", committed_offers.size());

	committed_offers.merge_in(std::move(uncommitted_offers));
	//std::printf("did merge%lu\n", std::this_thread::get_id());

	INTEGRITY_CHECK_F(
		if (!committed_offers.metadata_integrity_check()) {
			throw std::runtime_error("merge did us wrong");
		}
		std::printf("done final integrity check%lu\n", std::this_thread::get_id());
	);
	uncommitted_offers.clear();

	generate_metadata_index();

	//indexed_metadata = committed_offers.metadata_traversal<EndowAccumulator, Price, FuncWrapper>(PriceUtils::PRICE_BIT_LEN);
	//std::printf("done commit%lu\n", std::this_thread::get_id());
}*/

void MerkleWorkUnit::generate_metadata_index() {
	indexed_metadata = committed_offers.metadata_traversal<EndowAccumulator, Price, FuncWrapper>(PriceUtils::PRICE_BIT_LEN);
}

//rolls back transaction processing, NOT commit_for_production.
//TODO might want to rework this
/*void MerkleWorkUnit::rollback_for_production() {
	INFO("doing rollback for production for sell %d buy %d", category.sellAsset, category.buyAsset);
	uncommitted_offers.clear();
	INFO("starting clear_marked_deletions");
	committed_offers.clear_marked_deletions();
//	if (lmdb_instance) {
		lmdb_instance.pop_top_thunk();
	//}
}*/

void MerkleWorkUnit::persist_lmdb(uint64_t current_block_number) {

	if (!lmdb_instance) {
		return;
	}
	auto wtx = lmdb_instance.wbegin();
	lmdb_instance.write_thunks(wtx, current_block_number);//category.sellAsset == 0 && category.buyAsset == 1);
	lmdb_instance.commit_wtxn(wtx, current_block_number);

	auto stats = lmdb_instance.stat();

	bool do_check = false;
	INTEGRITY_CHECK_F(do_check = true);

	bool error_found = false;

	if (stats.ms_entries != committed_offers.size()) {
//		std::printf("stats.ms_entries = %lu, committed_offers.size = %u\n", stats.ms_entries, committed_offers.size());
		do_check = true;
		error_found = true;
	}

	//THIS OBVIOUSLY DOESN'T WORK WITH ASYNCHRONOUS PERSISTENCE; HENCE THE NEXT LINE
	do_check = false;

	if (do_check) {
		auto iterator = committed_offers.begin();

		unsigned int sz = 0;

		auto wtxn = lmdb_instance.wbegin();
		while (!iterator.at_end()) {
			sz++;

			const auto key = (*iterator) . first;

			//std::printf("searching for mem key %s\n",  DebugUtils::__array_to_str(key, MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());

			MerkleTrieT::prefix_t key_buf = key;

//			unsigned char key_buf[MerkleWorkUnit::WORKUNIT_KEY_LEN];
//			memcpy(key_buf, key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);

			auto key_buf_bytes = key_buf.get_bytes(key_buf.len());
			auto db_key = dbval{key_buf_bytes};//, MerkleWorkUnit::WORKUNIT_KEY_LEN};

			auto res = wtxn.get(lmdb_instance.get_data_dbi(), db_key);
			if (!res) {
				std::printf("IN MEM NOT DISK %s\n", key.to_string(key.len()).c_str());
				//std::printf("found missing key: %s\n", DebugUtils::__array_to_str(key, MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());

			//	if (lmdb_instance.get_top_thunk().uncommitted_offers.get_value(key)) {
			//		std::printf("was present in current thunk!");
			//	}
				error_found = true;
			} else {
				//std::printf("FOUND\n");
				//std::printf("key exists in db: %s\n", DebugUtils::__array_to_str(key, MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());
			}
			++iterator;
		}

		std::printf("iterated over %u memory elts\n", sz);

		auto read_cursor = wtxn.cursor_open(lmdb_instance.get_data_dbi());
		read_cursor.get(MDB_FIRST);

		while (read_cursor) {
			auto db_key = (*read_cursor).first;
			MerkleTrieT::prefix_t key;
			
			const unsigned char* key_raw = static_cast<unsigned char*>(db_key.mv_data);

			key.set_from_raw(key_raw, db_key.mv_size);
			//memcpy(key.data(), key_raw, key.size());
			if (key.size() != db_key.mv_size) {
				throw std::runtime_error("mismatch in size!");
			}

			if (!committed_offers.get_value(key)) {
				std::printf("read key len = %lu\n", db_key.mv_size);
				std::printf("IN DISK NOT MEM %s\n", key.to_string(key.len()).c_str());
				error_found = true;

			//	if (lmdb_instance.get_top_thunk().uncommitted_offers.get_value(key)) {
			//		std::printf("present in uncommitted_offers\n");
			//	}

				Offer value;
				dbval_to_xdr((*read_cursor). second, value);
				MerkleTrieT::prefix_t real_key;
				//unsigned char real_key[MerkleWorkUnit::WORKUNIT_KEY_LEN];
				generate_key(&value, real_key);
				std::printf("real key was %s\n", real_key.to_string(real_key.len()).c_str());//DebugUtils::__array_to_str(real_key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());
			}
			++read_cursor;
		}
		if (error_found) {
			//lmdb_instance.get_top_thunk().uncommitted_offers._log("thunk.uncommitted_offers");

			committed_offers._log("committed_offers");

			throw std::runtime_error("desync between memory and lmdb");
		}

		wtxn.abort();
	}

	lmdb_instance.clear_thunks(current_block_number);


}

/*
//old version
WorkUnitMetadata MerkleWorkUnit::get_metadata(Price p) {
	unsigned char price_bytes[PriceUtils::PRICE_BYTES];
	PriceUtils::write_price_big_endian(price_bytes, partial_exec_p);

	return committed_offers.metadata_query(
		price_bytes, PriceUtils::PRICE_BIT_LEN);
}
*/

Price divide_prices(Price sell_price, Price buy_price) {
	uint8_t extra_bits_len = (64-PriceUtils::PRICE_RADIX);
	uint128_t ratio = (((uint128_t)sell_price)<<64) / buy_price;
	ratio >>= extra_bits_len;
	return ratio & UINT64_MAX;
}

double 
MerkleWorkUnit::max_feasible_smooth_mult_double(int64_t amount, const Price* prices) const {
	size_t start = 1;
	size_t end = indexed_metadata.size() - 1;

	Price sell_price = prices[category.sellAsset];
	Price buy_price = prices[category.buyAsset];

	Price exact_exchange_rate = divide_prices(sell_price, buy_price);

	if (end <= 0) {
		return 0;
	}

	if (amount > indexed_metadata[end].metadata.endow) {
		return 0;
	}

	Price max_activated_price = 0;

	int mp = (end + start) /2;
	while (true) {
		if (end == start) {
			if (indexed_metadata[end].metadata.endow > amount) {
				max_activated_price = indexed_metadata[end].key;
			} else {
				if (end + 1 == indexed_metadata.size()) {
					return 0;
				}
				max_activated_price = indexed_metadata[end+1].key;
			}
			break;
		}

		if (amount >= indexed_metadata[mp].metadata.endow) {
			start = mp + 1;
		} else {
			end = mp;
		}
		mp = (start + end) / 2;
	}

	Price raw_difference = exact_exchange_rate - max_activated_price;
	if (exact_exchange_rate <= max_activated_price) {
		//Should never happen, but maybe if there's some rounding error I'm not accounting for.
		return 0;
	}

	return PriceUtils::to_double(raw_difference) / PriceUtils::to_double(exact_exchange_rate);
}

uint8_t
MerkleWorkUnit::max_feasible_smooth_mult(int64_t amount, const Price* prices) const {
	size_t start = 1;
	size_t end = indexed_metadata.size() - 1;

	Price sell_price = prices[category.sellAsset];
	Price buy_price = prices[category.buyAsset];

	Price exact_exchange_rate = divide_prices(sell_price, buy_price);

	if (end <= 0) {
		return UINT8_MAX;
	}

	if (amount > indexed_metadata[end].metadata.endow) {
		return UINT8_MAX;
	}

	Price max_activated_price = 0;

	int mp = (end + start) /2;
	while (true) {
		if (end == start) {
			if (indexed_metadata[end].metadata.endow > amount) {
				max_activated_price = indexed_metadata[end].key;
			} else {
				if (end + 1 == indexed_metadata.size()) {
					return UINT8_MAX;
				}
				max_activated_price = indexed_metadata[end+1].key;
			}
			break;
		}

		if (amount >= indexed_metadata[mp].metadata.endow) {
			start = mp + 1;
		} else {
			end = mp;
		}
		mp = (start + end) / 2;
	}

	//std::printf("exact price: %lu max activated: %lu amount %lu max_activated_amt %lu\n", exact_exchange_rate, max_activated_price, amount, indexed_metadata[end].metadata.endow);

	//std::printf("max activated price :%lf (%lu)\n", PriceUtils::to_double(max_activated_price), max_activated_price);

	Price raw_difference = exact_exchange_rate - max_activated_price;
	if (exact_exchange_rate <= max_activated_price) {
		//Should never happen, but maybe if there's some rounding error I'm not accounting for.
		return UINT8_MAX;
	}

	uint8_t out = 0;
	while (raw_difference <= (exact_exchange_rate >> out)) {
		out++;
	}
	if (out > 0) {
		return out - 1;
	}
	return 0;
}

size_t 
MerkleWorkUnit::num_open_offers() const {
	return committed_offers.size();
}


GetMetadataTask
MerkleWorkUnit::coro_get_metadata(Price p, EndowAccumulator& endow_out, DemandCalcScheduler& scheduler) const {

	using awaiter_t = DemandCalcAwaiter<const Price, DemandCalcScheduler>;

	int start = 1;
	int end = indexed_metadata.size() - 1;

	if (end <= 0) {
		endow_out = EndowAccumulator{};
		co_return;
	}
	if (p > indexed_metadata[end].key) {
		endow_out = indexed_metadata[end].metadata;
		co_return;
	}

	int mp = (end + start) / 2;
	while(true) {
		if (end == start) {
			endow_out = indexed_metadata[end - 1].metadata;
			co_return;
		}

		const Price compare_val = co_await awaiter_t{&(indexed_metadata[mp].key), scheduler};

		if (p >= compare_val) {
			start = mp + 1;
		} else {
			end = mp;
		}
		mp = (start + end) / 2;
	}
}


EndowAccumulator 
MerkleWorkUnit::get_metadata(Price p) const{
	int start = 1;
	int end = indexed_metadata.size()-1;
	DEMAND_CALC_INFO("committed_offers_sz:%d", committed_offers.size());
	DEMAND_CALC_INFO("indexed_metadata_sz:%d, end:%d", indexed_metadata.size(), end);
	if (end <= 0) {
		DEMAND_CALC_INFO("empty work unit, outputting 0");
		return EndowAccumulator{};
	}
	if (p > indexed_metadata[end].key) {
		DEMAND_CALC_INFO("outputting end");
		DEMAND_CALC_INFO("%lu, %lu", p, indexed_metadata[end].key);
		return indexed_metadata[end].metadata;
	}
	int mp = (end+start)/2;
	
	while (true) {
		if (end==start) {
			DEMAND_CALC_INFO("outputting idx %d, key %f", end, PriceUtils::to_double(indexed_metadata[end - 1].key));
			DEMAND_CALC_INFO("supply:%lu", indexed_metadata[end - 1].metadata.endow);
			return indexed_metadata[end - 1].metadata;
		}
		if (p >= indexed_metadata[mp].key) {
			start = mp + 1;
		} else {
			end = mp;
		}
		mp = (end + start)/2;
	}
}

std::pair<Price, Price> MerkleWorkUnit::get_execution_prices(Price sell_price, Price buy_price, const uint8_t smooth_mult) const {
	uint8_t extra_bits_len = (64-PriceUtils::PRICE_RADIX);
	uint128_t ratio = (((uint128_t)sell_price)<<64) / buy_price;
	ratio >>= extra_bits_len;

	Price upper_bound_price = ratio & UINT64_MAX;
	Price lower_bound_price = upper_bound_price;
	if (smooth_mult) {
		lower_bound_price = upper_bound_price - (upper_bound_price>>smooth_mult);
	}
	return std::make_pair(lower_bound_price, upper_bound_price);
}

std::pair<Price, Price> MerkleWorkUnit::get_execution_prices(const Price* prices, const uint8_t smooth_mult) const {
	auto sell_price = prices[category.sellAsset];
	auto buy_price = prices[category.buyAsset];
	return get_execution_prices(sell_price, buy_price, smooth_mult);

	/*uint8_t extra_bits_len = (64-PriceUtils::PRICE_RADIX);
	uint128_t ratio = (((uint128_t)sell_price)<<64) / buy_price;
	ratio >>= extra_bits_len;

	Price upper_bound_price = ratio & UINT64_MAX;
	Price lower_bound_price = upper_bound_price;
	if (smooth_mult) {
		lower_bound_price = upper_bound_price - (upper_bound_price>>smooth_mult);
	}
	return std::make_pair(lower_bound_price, upper_bound_price);*/
}

std::pair<uint64_t, uint64_t>
MerkleWorkUnit::get_supply_bounds(const Price* prices, const uint8_t smooth_mult) const {	
	
	auto [lower_bound_price, upper_bound_price] = get_execution_prices(prices, smooth_mult);

	uint64_t upper_bound = get_metadata(upper_bound_price).endow;
	uint64_t lower_bound = get_metadata(lower_bound_price).endow;

	return std::make_pair(lower_bound, upper_bound);
}

std::pair<uint64_t, uint64_t>
MerkleWorkUnit::get_supply_bounds(Price sell_price, Price buy_price, const uint8_t smooth_mult) const {
	
	auto [lower_bound_price, upper_bound_price] = get_execution_prices(sell_price, buy_price, smooth_mult);

	uint64_t upper_bound = get_metadata(upper_bound_price).endow;
	uint64_t lower_bound = get_metadata(lower_bound_price).endow;
	
	return std::make_pair(lower_bound, upper_bound);
}

void MerkleWorkUnit::calculate_demands_and_supplies_from_metadata(
	const Price* prices, 
	uint128_t* demands_workspace,
	uint128_t* supplies_workspace,
	const uint8_t smooth_mult, 
	const EndowAccumulator& metadata_partial,
	const EndowAccumulator& metadata_full) {

	auto sell_price = prices[category.sellAsset];
	auto buy_price = prices[category.buyAsset];

	uint64_t full_exec_endow = metadata_full.endow;

	uint64_t partial_exec_endow = metadata_partial.endow - full_exec_endow;//radix:0

	//std::printf("new calc price bds %lu %lu\n", full_exec_p, partial_exec_p);
	//std::printf("new calc endow %lu %lu\n", full_exec_endow, partial_exec_endow);


	//std::printf("endow %lu %lu\n", full_exec_endow, partial_exec_endow);

	uint128_t full_exec_endow_times_price = metadata_full.endow_times_price; // radix:PRICE_RADIX
	uint128_t partial_exec_endow_times_price = metadata_partial.endow_times_price - full_exec_endow_times_price; //radix:PRICE_RADIX
	if (metadata_full.endow_times_price > metadata_partial.endow_times_price) {

		/*std::printf("full=%Lf partial=%Lf\n", (long double)metadata_full.endow_times_price, (long double)metadata_partial.endow_times_price);
		committed_offers._log("");

		unsigned char price_buf[6];

		std::printf("sellAsset = %d buyAsset = %d\n", category.sellAsset, category.buyAsset);

		PriceUtils::write_price_big_endian(price_buf, partial_exec_p);
		std::printf("partial exec p = %s\n", DebugUtils::__array_to_str(price_buf, 6).c_str());
		PriceUtils::write_price_big_endian(price_buf, full_exec_p);
		std::printf("full exec p = %s\n", DebugUtils::__array_to_str(price_buf, 6).c_str());

		for (unsigned int i = 0; i < indexed_metadata.size(); i++) {
			PriceUtils::write_price_big_endian(price_buf, indexed_metadata[i].key);
			std::printf("%s %ld %lf\n", DebugUtils::__array_to_str(price_buf, 6).c_str(), indexed_metadata[i].metadata.endow, (double) indexed_metadata[i].metadata.endow_times_price);
		}*/
		throw std::runtime_error("This should absolutely never happen, and means indexed_metadata or binary search is broken (or maybe an overflow)");
	}

	uint128_t partial_sell_volume = 0; //radix:PRICE_RADIX
	uint128_t partial_buy_volume = 0;  //radix:PRICE_RADIX


	if (smooth_mult) {


		//net endow times ratio is at most (partial exec endow) * sell_over_buy.
		uint128_t endow_over_epsilon = (partial_exec_endow) << smooth_mult; // radix:0

		DEMAND_CALC_INFO("net endow over epsilon:%f", PriceUtils::amount_to_double(endow_over_epsilon, 0));

		uint128_t endow_times_price_over_epsilon = (partial_exec_endow_times_price)<<smooth_mult; //radix:PRICE_RADIX


		DEMAND_CALC_INFO("net endow times price over epsilon:%f", PriceUtils::amount_to_double(endow_times_price_over_epsilon, PriceUtils::PRICE_RADIX));

		uint128_t sell_wide_multiply_result = PriceUtils::wide_multiply_val_by_a_over_b(endow_times_price_over_epsilon, buy_price, sell_price); //radix:PRICE_RADIX


		partial_sell_volume = (endow_over_epsilon<<PriceUtils::PRICE_RADIX) - (sell_wide_multiply_result); //radix:PRICE_RADIX
		if ((endow_over_epsilon<<PriceUtils::PRICE_RADIX) < (sell_wide_multiply_result)) {
			/*partial_sell_volume = 0;
			std::printf("weird %lf\n", (double)(sell_wide_multiply_result - (endow_over_epsilon<<PriceUtils::PRICE_RADIX)));
			//mostly a sanity check

			std::printf("%lf %lf\n", (double) (sell_wide_multiply_result)/16777216, (double) (endow_over_epsilon<<PriceUtils::PRICE_RADIX)/16777216);

			for (std::size_t i = 0; i < indexed_metadata.size(); i++) {
				std::printf("%lu %lu %lf \n", indexed_metadata[i].key/16777216, indexed_metadata[i].metadata.endow, (double)indexed_metadata[i].metadata.endow_times_price/16777216);
			}*/

			throw std::runtime_error("this should not happen unless something has begun to overflow");
		}

		uint128_t buy_wide_multiply_result = PriceUtils::wide_multiply_val_by_a_over_b(
			endow_over_epsilon<<PriceUtils::PRICE_RADIX,
			sell_price,
			buy_price);
		partial_buy_volume = (buy_wide_multiply_result - endow_times_price_over_epsilon); // radix:PRICE_RADIX

		if (buy_wide_multiply_result < endow_times_price_over_epsilon) {
			partial_buy_volume = 0;
			std::printf("weird2 %lf\n", (double) (endow_times_price_over_epsilon - buy_wide_multiply_result));

			throw std::runtime_error("this should not happen unless something has begun to overflow");
			//sanity check
		}
	}

	uint128_t full_sell_volume = partial_sell_volume 
		+ (((uint128_t)full_exec_endow)<<PriceUtils::PRICE_RADIX);


	auto full_buy_volume = partial_buy_volume 
		+ PriceUtils::wide_multiply_val_by_a_over_b(((uint128_t)full_exec_endow)<<PriceUtils::PRICE_RADIX, sell_price, buy_price);//(((uint128_t)full_exec_amount) * ((uint128_t)sell_price)) / buy_price;

	//std::printf("new full buy %lf new full sell %lf\n", PriceUtils::amount_to_double(full_buy_volume), PriceUtils::amount_to_double(full_sell_volume));

	demands_workspace[category.buyAsset] += full_buy_volume;
	supplies_workspace[category.sellAsset] += full_sell_volume;
}

void MerkleWorkUnit::calculate_demands_and_supplies(
	const Price* prices, 
	uint128_t* demands_workspace, 
	uint128_t* supplies_workspace,
	const uint8_t smooth_mult) {
	//ObjectiveFunctionInputs& func) {

	DEMAND_CALC_INFO("demand query: sellAsset %d buyAsset %d", category.sellAsset, category.buyAsset);
	auto [full_exec_p, partial_exec_p] = get_execution_prices(prices, smooth_mult);
	
	auto sell_price = prices[category.sellAsset];
	auto buy_price = prices[category.buyAsset];

	//std::printf("regular calc price bds %lu %lu\n", full_exec_p, partial_exec_p);

	//uint8_t extra_bits_len = (64-PriceUtils::PRICE_RADIX);
	//uint128_t extra_bits = (((uint128_t)1)<<(extra_bits_len)) - 1;
	//uint128_t ratio = (((uint128_t)sell_price)<<64) / buy_price;

	//extra_bits &= ratio;
	//ratio >>= extra_bits_len;
	/*if (extra_bits) {
		ratio ++;  //round up price.
		//Not including this implicitly rounds down.
		//THis is correct behavior.
		//Metadata queries return sum of metadata
		//for keys <= current key.
		//We don't want to include sell offers with
		// prices greater than our target price.

	}*/

	/*Price partial_exec_p = ratio & UINT64_MAX;
	unsigned char price_bytes[PriceUtils::PRICE_BYTES];
	PriceUtils::write_price_big_endian(price_bytes, partial_exec_p);

	WorkUnitMetadata metadata_partial = committed_offers.metadata_query(price_bytes, PriceUtils::PRICE_BIT_LEN);
	WorkUnitMetadata metadata_full = metadata_partial;
	*/

	//Price partial_exec_p = ratio & UINT64_MAX;
	DEMAND_CALC_INFO("partial exec price:%f", PriceUtils::to_double(partial_exec_p));

	auto metadata_partial = get_metadata(partial_exec_p);
	auto metadata_full = metadata_partial;
	if (smooth_mult) {
		//Price full_exec_p = partial_exec_p - (partial_exec_p>>smooth_mult);
		DEMAND_CALC_INFO("full exec price:%f", PriceUtils::to_double(full_exec_p));
		metadata_full = get_metadata(full_exec_p);
		//PriceUtils::write_price_big_endian(price_bytes, full_exec_p);
		//metadata_full = committed_offers.metadata_query(price_bytes, PriceUtils::PRICE_BIT_LEN);
	}

	uint64_t full_exec_endow = metadata_full.endow;

	//std::printf("full_exec_endow=%lu\n", full_exec_endow);
	uint64_t partial_exec_endow = metadata_partial.endow - full_exec_endow;//radix:0
	//std::printf("regular calc endow %lu %lu\n", full_exec_endow, partial_exec_endow);

	uint128_t full_exec_endow_times_price = metadata_full.endow_times_price; // radix:PRICE_RADIX
	uint128_t partial_exec_endow_times_price = metadata_partial.endow_times_price - full_exec_endow_times_price; //radix:PRICE_RADIX
	if (metadata_full.endow_times_price > metadata_partial.endow_times_price) {

		std::printf("full=%Lf partial=%Lf\n", (long double)metadata_full.endow_times_price, (long double)metadata_partial.endow_times_price);
		committed_offers._log("");

		unsigned char price_buf[6];

		std::printf("sellAsset = %d buyAsset = %d\n", category.sellAsset, category.buyAsset);

		PriceUtils::write_price_big_endian(price_buf, partial_exec_p);
		std::printf("partial exec p = %s\n", DebugUtils::__array_to_str(price_buf, 6).c_str());
		PriceUtils::write_price_big_endian(price_buf, full_exec_p);
		std::printf("full exec p = %s\n", DebugUtils::__array_to_str(price_buf, 6).c_str());

		for (unsigned int i = 0; i < indexed_metadata.size(); i++) {
			PriceUtils::write_price_big_endian(price_buf, indexed_metadata[i].key);
			std::printf("%s %ld %lf\n", DebugUtils::__array_to_str(price_buf, 6).c_str(), indexed_metadata[i].metadata.endow, (double) indexed_metadata[i].metadata.endow_times_price);
		}
		throw std::runtime_error("This should absolutely never happen, and means indexed_metadata or binary search is broken (or maybe an overflow)");
	}

	uint128_t partial_sell_volume = 0; //radix:PRICE_RADIX
	uint128_t partial_buy_volume = 0;  //radix:PRICE_RADIX


	if (smooth_mult) {


		//net endow times ratio is at most (partial exec endow) * sell_over_buy.
		uint128_t endow_over_epsilon = (partial_exec_endow) << smooth_mult; // radix:0

		DEMAND_CALC_INFO("net endow over epsilon:%f", PriceUtils::amount_to_double(endow_over_epsilon, 0));

		uint128_t endow_times_price_over_epsilon = (partial_exec_endow_times_price)<<smooth_mult; //radix:PRICE_RADIX


		DEMAND_CALC_INFO("net endow times price over epsilon:%f", PriceUtils::amount_to_double(endow_times_price_over_epsilon, PriceUtils::PRICE_RADIX));

		uint128_t sell_wide_multiply_result = PriceUtils::wide_multiply_val_by_a_over_b(endow_times_price_over_epsilon, buy_price, sell_price); //radix:PRICE_RADIX


		partial_sell_volume = (endow_over_epsilon<<PriceUtils::PRICE_RADIX) - (sell_wide_multiply_result); //radix:PRICE_RADIX
		if ((endow_over_epsilon<<PriceUtils::PRICE_RADIX) < (sell_wide_multiply_result)) {
			partial_sell_volume = 0;
			std::printf("weird %lf\n", (double)(sell_wide_multiply_result - (endow_over_epsilon<<PriceUtils::PRICE_RADIX)));
			//mostly a sanity check

			std::printf("%lf %lf\n", (double) (sell_wide_multiply_result)/16777216, (double) (endow_over_epsilon<<PriceUtils::PRICE_RADIX)/16777216);

			for (std::size_t i = 0; i < indexed_metadata.size(); i++) {
				std::printf("%lu %lu %lf \n", indexed_metadata[i].key/16777216, indexed_metadata[i].metadata.endow, (double)indexed_metadata[i].metadata.endow_times_price/16777216);
			}

			throw std::runtime_error("this should not happen unless something has begun to overflow");
		}
		/*partial_sell_volume = endow_over_epsilon
			- PriceUtils::wide_multiply_val_by_a_over_b(
				endow_times_price_over_epsilon,
				buy_price,
				sell_price);*/


		uint128_t buy_wide_multiply_result = PriceUtils::wide_multiply_val_by_a_over_b(
			endow_over_epsilon<<PriceUtils::PRICE_RADIX,
			sell_price,
			buy_price);
		partial_buy_volume = (buy_wide_multiply_result - endow_times_price_over_epsilon); // radix:PRICE_RADIX
		if (buy_wide_multiply_result < endow_times_price_over_epsilon) {
			partial_buy_volume = 0;
			std::printf("weird2 %lf\n", (double) (endow_times_price_over_epsilon - buy_wide_multiply_result));

			throw std::runtime_error("this should not happen unless something has begun to overflow");
			//sanity check
		}
	}

	uint128_t full_sell_volume = partial_sell_volume 
		+ (((uint128_t)full_exec_endow)<<PriceUtils::PRICE_RADIX);

	//std::printf("partial_sell_volume=%f full_exec_endow=%f\n", PriceUtils::amount_to_double(partial_sell_volume), PriceUtils::amount_to_double(full_exec_endow));

	auto full_buy_volume = partial_buy_volume 
		+ PriceUtils::wide_multiply_val_by_a_over_b(((uint128_t)full_exec_endow)<<PriceUtils::PRICE_RADIX, sell_price, buy_price);//(((uint128_t)full_exec_amount) * ((uint128_t)sell_price)) / buy_price;

	constexpr bool aggressive_rounding = false; //INCORRECT to turn this on
	if (aggressive_rounding) {
		uint64_t lowbits = (((uint64_t) 1) << (PriceUtils::PRICE_RADIX)) - 1;
		full_buy_volume &= ~lowbits;
		bool round_up = full_sell_volume & lowbits;
		full_sell_volume &= ~lowbits;
		full_sell_volume += (round_up << PriceUtils::PRICE_RADIX);
	}


	//std::printf("orig full buy %lf orig full sell %lf\n", PriceUtils::amount_to_double(full_buy_volume), PriceUtils::amount_to_double(full_sell_volume));

	demands_workspace[category.buyAsset] += full_buy_volume;
	supplies_workspace[category.sellAsset] += full_sell_volume;

	//double sell_price_d = PriceUtils::to_double(sell_price);
	//double buy_price_d = PriceUtils::to_double(buy_price);
	
	//func.values[category.sellAsset][category.buyAsset] = (double) full_sell_volume;

	//func.value += ((double)full_sell_volume) * sell_price_d * std::log2f(sell_price_d / buy_price_d);

	//std::printf("work unit sell %lu buy %lu: demand %f supply %f\n", 
	//	category.sellAsset, category.buyAsset, PriceUtils::amount_to_double(full_buy_volume), PriceUtils::amount_to_double(full_sell_volume));
}

bool MerkleWorkUnit::tentative_clear_offers_for_validation(
	MemoryDatabase& db,
	SerialAccountModificationLog& serial_account_log,
	SingleValidationStatistics& validation_statistics,
	const SingleWorkUnitStateCommitmentChecker& local_clearing_log,
	const WorkUnitStateCommitmentChecker& clearing_commitment_log,
	BlockStateUpdateStatsWrapper& state_update_stats){


	//int idx = WorkUnitManagerUtils::category_to_idx(category, 20);


	MerkleTrieT::prefix_t partialExecThresholdKey(local_clearing_log.partialExecThresholdKey);

	int64_t endow_below_partial_exec_key = committed_offers.endow_lt_key(local_clearing_log.partialExecThresholdKey);
	validation_statistics.activated_supply += FractionalAsset::from_integral(endow_below_partial_exec_key);


	//if (idx == 266) {
	//	std::printf("endow below partial exec key: %ld\n", endow_below_partial_exec_key);
	//}

	Price sellPrice = clearing_commitment_log.prices[category.sellAsset];
	Price buyPrice = clearing_commitment_log.prices[category.buyAsset];

	CompleteClearingFunc func(sellPrice, buyPrice, clearing_commitment_log.tax_rate, db, serial_account_log);

	unsigned char zero_buf[MerkleWorkUnit::WORKUNIT_KEY_LEN];
	memset(zero_buf, 0, MerkleWorkUnit::WORKUNIT_KEY_LEN);

	auto partial_exec_offer_opt = committed_offers.perform_deletion(local_clearing_log.partialExecThresholdKey);

	//if no partial exec offer, partial exec threshold key in block header must be all zeros.
	if (!partial_exec_offer_opt) {
		INFO("no partial exec offer");
		

	//	if (idx == 266) {
	//		std::printf("no partial exec offer matching key found!\n");
	//	}

		if (memcmp(local_clearing_log.partialExecThresholdKey.data(), zero_buf, MerkleWorkUnit::WORKUNIT_KEY_LEN)!= 0) {
			std::printf("key was not zero\n");
			return false;
		}
		if (local_clearing_log.partialExecOfferActivationAmount() != FractionalAsset::from_integral(0)) {
			std::printf("partial activate amt was nonzero\n");
			return false;
		}

		validation_statistics.activated_supply += FractionalAsset::from_integral(committed_offers.get_root_metadata().endow);
		try {
			committed_offers.apply(func);
		} catch (...) {
			std::printf("failed apply WHEN NO PARTIAL EXEC OFFER in category sell %u buy %u with endow_below_partial_exec_key %ld\n", category.sellAsset, category.buyAsset, endow_below_partial_exec_key);
			std::printf("committed offers sz was %lu\n", committed_offers.size());
			std::fflush(stdout);
			throw;
		}
		state_update_stats.fully_clear_offer_count += committed_offers.size();



		{
			std::lock_guard lock(*lmdb_instance.mtx);
			auto& thunk = lmdb_instance.get_top_thunk();
			thunk.set_no_partial_exec();
			thunk.cleared_offers = std::move(committed_offers);
			committed_offers.clear();
		}

		//committed_offers.mark_entire_tree_for_deletion();

		INFO("no partial exec correct exit");
		return true;
	}


	auto partial_exec_offer = *partial_exec_offer_opt;

	serial_account_log.log_self_modification(partial_exec_offer.owner, partial_exec_offer.offerId);

	//if (idx == 266) {
	//	std::printf("logging self mod owner %lu offerId %lu\n", partial_exec_offer.owner, partial_exec_offer.offerId);
//	}
	int64_t partial_exec_sell_amount, partial_exec_buy_amount;


	account_db_idx db_idx;

	if (!db.lookup_user_id(partial_exec_offer.owner, &db_idx)) {
		std::printf("couldn't lookup user\n");
		committed_offers.insert(local_clearing_log.partialExecThresholdKey, partial_exec_offer);
		return false;
	}

	//std::printf("doing the partial exec clear\n");

	clear_offer_partial(
		partial_exec_offer, 
		clearing_commitment_log.prices[partial_exec_offer.category.sellAsset],
		clearing_commitment_log.prices[partial_exec_offer.category.buyAsset],
		clearing_commitment_log.tax_rate,
		local_clearing_log.partialExecOfferActivationAmount(),
		db, 
		db_idx,
		partial_exec_sell_amount,
		partial_exec_buy_amount);

	if ((uint64_t)partial_exec_sell_amount > partial_exec_offer.amount) {
		std::printf("sell amount too high: partial_exec_sell_amount %ld partial_exec_offer.amount %ld\n", partial_exec_sell_amount, partial_exec_offer.amount);
		committed_offers.insert(local_clearing_log.partialExecThresholdKey, partial_exec_offer);
		return false;
	}

//	if (idx == 266) {
//		std::printf("partial exec sell amount: %ld\n", partial_exec_sell_amount);
//		std::printf("partial exec offer %lu %lu\n", partial_exec_offer.owner, partial_exec_offer.offerId);
//	}

	{
		std::lock_guard lock(*lmdb_instance.mtx);
		auto& thunk = lmdb_instance.get_top_thunk();

		thunk.set_partial_exec(
			local_clearing_log.partialExecThresholdKey, partial_exec_sell_amount, partial_exec_offer);

		// achieves same effect as a hypothetical committed_offers.split_lt_key
		try{
			thunk.cleared_offers = committed_offers.endow_split(endow_below_partial_exec_key);
		
//			if (idx == 266) {
//
//				std::printf("thunk.cleared_offers.size(): %lu\n", thunk.cleared_offers.size());
//				thunk.cleared_offers._log("thunk.cleared offers");
//			}
		} catch (...) {
			std::printf("failed endow_split in category sell %u buy %u with endow_below_partial_exec_key %ld\n", category.sellAsset, category.buyAsset, endow_below_partial_exec_key);
			std::fflush(stdout);
			throw;
		}
		try {
			thunk.cleared_offers.apply(func);
		} catch (...) {
			std::printf("failed apply in category sell %u buy %u with endow_below_partial_exec_key %ld\n", category.sellAsset, category.buyAsset, endow_below_partial_exec_key);
			std::printf("cleared offers sz was %lu\n", thunk.cleared_offers.size());
			std::printf("partial exec amount was (predecrement) %ld, partial_exec_sell_amount was %ld\n", partial_exec_offer.amount, partial_exec_sell_amount);
			std::printf("partial exec offer minPrice is %lf (%lu)\n", PriceUtils::to_double(partial_exec_offer.minPrice), partial_exec_offer.minPrice);
			std::fflush(stdout);
			throw;
		}
		state_update_stats.fully_clear_offer_count += thunk.cleared_offers.size();
	}

	partial_exec_offer.amount -= partial_exec_sell_amount;


	//committed_offers.mark_subtree_lt_key_for_deletion(local_clearing_log.partialExecThresholdKey);
	//committed_offers.apply_lt_key(func, local_clearing_log.partialExecThresholdKey);

	if (partial_exec_offer.amount != 0) {
		//added if statement 4/22/2021
		committed_offers.insert(local_clearing_log.partialExecThresholdKey, partial_exec_offer);
		state_update_stats.partial_clear_offer_count ++;
	}
	return true;
}

void MerkleWorkUnit::process_clear_offers(
	const WorkUnitClearingParams& params, 
	const Price* prices, 
	const uint8_t& tax_rate, 
	MemoryDatabase& db,
	SerialAccountModificationLog& serial_account_log,
	SingleWorkUnitStateCommitment& clearing_commitment_log,
	BlockStateUpdateStatsWrapper& state_update_stats) {
	

	auto clear_amount = params.supply_activated.floor();
	if (clear_amount > INT64_MAX) {
		throw std::runtime_error("trying to clear more than there should exist");
	}

	PriceUtils::write_unsigned_big_endian(clearing_commitment_log.fractionalSupplyActivated, params.supply_activated.value);

	INTEGRITY_CHECK_F(
		if (!committed_offers.metadata_integrity_check()) {
			throw std::runtime_error("metadata corrupted in committed offers");
		}

		if (!committed_offers.partial_metadata_integrity_check()) {
			throw std::runtime_error("committed offers corrupted");
		}

		auto prev_size = committed_offers.uncached_size();
	);

	//std::printf("committed_offers.size() = %lu\n", committed_offers.size());
	auto fully_cleared_trie = committed_offers.endow_split(clear_amount);
	//std::printf("post split committed_offers.size() = %lu\n", committed_offers.size());

	INTEGRITY_CHECK_F(
		if (fully_cleared_trie.uncached_size() + committed_offers.uncached_size() != prev_size) {
			throw std::runtime_error("split doesn't preserve size");
		}
		if (!fully_cleared_trie.partial_metadata_integrity_check()) {
			throw std::runtime_error("partial metadata");
		}
		if (!committed_offers.partial_metadata_integrity_check()) {
			throw std::runtime_error("this makes no sense");
		}
		if (!committed_offers.metadata_integrity_check()) {
			throw std::runtime_error("post split committed corruption");
		}
	);

	Price sellPrice = prices[category.sellAsset];
	Price buyPrice = prices[category.buyAsset];

	CompleteClearingFunc func(sellPrice, buyPrice, tax_rate, db, serial_account_log);

/*	tbb::this_task_arena::isolate([&func, &fully_cleared_trie]() {
		fully_cleared_trie.parallel_apply(func);
	});
*/
	//todo see if coroutines still don't make sense
	try {
		fully_cleared_trie.apply(func);
	} catch (...) {
		fully_cleared_trie._log("fully cleared trie: ");

		std::fflush(stdout);
		throw;
	}
	state_update_stats.fully_clear_offer_count += fully_cleared_trie.size();

	
	auto remaining_to_clear = params.supply_activated 
		- FractionalAsset::from_integral(fully_cleared_trie.get_root_metadata().endow);

	{
		std::lock_guard lock(*lmdb_instance.mtx);
		lmdb_instance.get_top_thunk().cleared_offers = std::move(fully_cleared_trie);
		fully_cleared_trie.clear();
	}
	PriceUtils::write_unsigned_big_endian(clearing_commitment_log.partialExecOfferActivationAmount, remaining_to_clear.value);

	//We own this key
	std::optional<MerkleTrieT::prefix_t> partial_exec_key = committed_offers.get_lowest_key();

	if ((!partial_exec_key) && remaining_to_clear != FractionalAsset::from_integral(0)) {
		throw std::runtime_error("null partial_exec_key (lowest offer key) but remaining to clear is nonzero");
	}

	if (!partial_exec_key) {
		INFO("partial exec key is nullptr");
		INTEGRITY_CHECK("remaining offers size: %d", committed_offers.size());
		//no committed offers remain
		std::lock_guard lock(*lmdb_instance.mtx);
		lmdb_instance.get_top_thunk().set_no_partial_exec();
		clearing_commitment_log.partialExecThresholdKey.fill(0);
//		memset(clearing_commitment_log.partialExecThresholdKey.data(), 0, MerkleWorkUnit::WORKUNIT_KEY_LEN);
		clearing_commitment_log.thresholdKeyIsNull = 1;


		return;
	}
	clearing_commitment_log.thresholdKeyIsNull = 0;


	INFO("partial exec key = %s", DebugUtils::__array_to_str(*partial_exec_key.data(), *partial_exec_key.size()).c_str());

	INTEGRITY_CHECK_F(
		auto predelete_size = committed_offers.uncached_size();
	);

	//std::printf("starting last committed offers delete\n");
	//committed_offers._log("predelete ");

	auto try_delete = (committed_offers.perform_deletion(*partial_exec_key));

	if (!try_delete) {
		throw std::runtime_error("couldn't find partial exec offer!!!");
	}

	auto partial_exec_offer = *try_delete;

	//std::printf("done last committed offers delete\n");

	INTEGRITY_CHECK_F(
		if (committed_offers.uncached_size() + 1 != predelete_size) {
			throw std::runtime_error("deletion didn't actually happen");
		}
	);

	serial_account_log.log_self_modification(partial_exec_offer.owner, partial_exec_offer.offerId);

	int64_t buy_amount, sell_amount;

	account_db_idx idx;
	auto result = db.lookup_user_id(partial_exec_offer.owner, &idx);
	if (!result) {
		throw std::runtime_error("(partialexec) Offer in manager from nonexistent account");
	}

	clear_offer_partial(partial_exec_offer, sellPrice, buyPrice, tax_rate, remaining_to_clear, db, idx, sell_amount,buy_amount);

	//uint128_t buy_amount_raw = PriceUtils::wide_multiply_val_by_a_over_b(
	//	remaining_to_clear.value, sellPrice, buyPrice);
	//int64_t buy_amount = FractionalAsset::from_raw(buy_amount_raw).floor();



	//int64_t sell_amount = remaining_to_clear.ceil();

	if (partial_exec_offer.amount < (uint64_t) sell_amount) {
		throw std::runtime_error("should not have been partially clearing this offer");
	}

	std::lock_guard lock(*lmdb_instance.mtx);
	lmdb_instance.get_top_thunk().set_partial_exec(*partial_exec_key, sell_amount, partial_exec_offer);
	
	if (partial_exec_key->size() != MerkleWorkUnit::WORKUNIT_KEY_LEN || clearing_commitment_log.partialExecThresholdKey.size() != MerkleWorkUnit::WORKUNIT_KEY_LEN) {
		throw std::runtime_error("invalid partial_exec_key size!?!!");
	}
	//clearing_commitment_log.partialExecThresholdKey = xdr::opaque_array<MerkleWorkUnit::WORKUNIT_KEY_LEN>(*partial_exec_key);
	clearing_commitment_log.partialExecThresholdKey = partial_exec_key->get_bytes_array();
//	memcpy(clearing_commitment_log.partialExecThresholdKey.data(), partial_exec_key->data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);


	partial_exec_offer.amount -= sell_amount;

	//db.transfer_escrow(idx, category.sellAsset, -sell_amount);
	//db.transfer_available(idx, category.buyAsset, buy_amount);

	if (partial_exec_offer.amount > 0) {
		//std::printf("starting last committed offers insert\n");
		//committed_offers._log("committed offers ");
		committed_offers.insert(*partial_exec_key, partial_exec_offer);
		//std::printf("ending last committed offers insert\n");
		state_update_stats.partial_clear_offer_count++;
	} else if (partial_exec_offer.amount < 0) {
		throw std::runtime_error("how on earth did partial_exec_offer.amount become less than 0");
	}

	INTEGRITY_CHECK("remaining offers size: %d", committed_offers.size());

	INTEGRITY_CHECK_F(
		if (committed_offers.size() != committed_offers.uncached_size()) {
			throw std::runtime_error("metadata size inconsistency " + std::to_string(committed_offers.size()) + " " +  std::to_string(committed_offers.uncached_size()));
		}
	);

	//TODO partial exec rebate policy?
}

struct LMDBInsertLambda {

	dbenv::wtxn& txn;
	MDB_dbi& dbi;
	bool display = false;

	int num_values_put = 0;

	void operator() (const unsigned char* key, const Offer& offer) {

		//TODO see if it breaks to not use key_buf.  mdb_put shouldn't modify the input key, right?
		//unsigned char key_buf [MerkleWorkUnit::WORKUNIT_KEY_LEN];
		//memcpy(key_buf, key, MerkleWorkUnit::WORKUNIT_KEY_LEN);

		INTEGRITY_CHECK_F(
			unsigned char key_test[MerkleWorkUnit::WORKUNIT_KEY_LEN];
			MerkleWorkUnit::generate_key(&offer, key_test);
			if (memcmp(key_test, key, MerkleWorkUnit::WORKUNIT_KEY_LEN) != 0) {
				throw std::runtime_error("what the fuck");
			}
			if (display) {
				std::printf("inserting key %s %s\n", DebugUtils::__array_to_str(key, MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str(), DebugUtils::__array_to_str(key_test, MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());
			}
		);

		dbval db_key = dbval{key, MerkleWorkUnit::WORKUNIT_KEY_LEN};

		auto value_buf = xdr::xdr_to_msg(offer);
		dbval value = dbval{value_buf->data(), value_buf->size()};//xdr_to_dbval(offer);
		txn.put(dbi, db_key, value);

		INTEGRITY_CHECK_F(
			if (display) {
				std::printf("post put: %s\n", DebugUtils::__array_to_str(db_key.bytes().data(), MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());
			}
			auto res = txn.get(dbi, db_key);

			if (display) {
				std::printf("post get: res = %d %s\n", (bool)res, DebugUtils::__array_to_str(db_key.bytes().data(), MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());
			}
		);

		num_values_put ++;
	}
};

void MerkleWorkUnit::rollback_thunks(uint64_t current_block_number) {
	std::lock_guard lock(*lmdb_instance.mtx);
	if (current_block_number <= lmdb_instance.get_persisted_round_number()) {
		throw std::runtime_error("can't rollback persisted objects");
	}

	auto& thunks = lmdb_instance.get_thunks_ref();

	for (size_t i = 0; i < thunks.size();) {
		if (thunks[i].current_block_number > current_block_number) {

			undo_thunk(thunks[i]);

			thunks.erase(thunks.begin() + i);
		} else {
			i++;
		}
	}
}

void WorkUnitLMDB::clear_thunks(uint64_t current_block_number) {
	return;

	/*std::lock_guard lock(*mtx);
	bool print = false;
	for (size_t i = 0; i < thunks.size();) {

		if (print)
			std::printf("clearing i %lu %lu\n", i, thunks[i].current_block_number);
		if (thunks[i].current_block_number <= current_block_number) {
			thunks[i].cleared_offers.detached_delete();
			thunks.erase(thunks.begin() + i);
		} else {
			i++;
		}
	}
	if (print)
		std::printf("remaining: %lu\n", thunks.size());*/
}

void WorkUnitLMDB::write_thunks(dbenv::wtxn& wtx, const uint64_t current_block_number, bool debug) {


	std::vector<LMDBCommitmentThunk> relevant_thunks;
	{
		std::lock_guard lock(*mtx);
		for (size_t i = 0; i < thunks.size();) {
			auto& thunk = thunks.at(i);
			if (thunk.current_block_number <= current_block_number) {
				relevant_thunks.emplace_back(std::move(thunk));
				thunks.at(i).reset_trie();
				thunks.erase(thunks.begin() + i);
			} else {
				i++;
			}

		}
	}
	//In one round, everything below partial_exec_key is deleted (cleared).
	//So first, we find the maximum partial_exec_key, and delete everything BELOW that.

	//get maximum key, remove offers
	// iterate from top to bot, adding only successful offers and rolling key downwards

	MerkleTrieT::prefix_t key_buf;

	//unsigned char key_buf[MerkleWorkUnit::WORKUNIT_KEY_LEN];
	//memset(key_buf, 0, MerkleWorkUnit::WORKUNIT_KEY_LEN);


	bool key_set = false;

	if (relevant_thunks.size() == 0 && get_persisted_round_number() != current_block_number) {
		throw std::runtime_error("can't persist without thunks");
	}

	if (relevant_thunks.size() == 0) {
		return;
	}

	if (relevant_thunks[0].current_block_number != get_persisted_round_number() + 1) {
		throw std::runtime_error("invalid current_block_number");
	}


	//compute maximum key

	bool print = false;

	if (print)
		std::printf("phase 1\n");

	for (size_t i = 0; i < relevant_thunks.size(); i++) {
		auto& thunk = relevant_thunks[i];
		if (thunk.current_block_number > current_block_number) {
			throw std::runtime_error("impossible");
			//inflight thunk that's not done yet, or for some reason we're not committing that far out yet.
			continue;
		}
		if (print)
			std::printf("phase 1(max key) thunk i=%lu %lu\n", i,  thunk.current_block_number);

		//remove deleted keys
		for (auto& delete_kv : thunk.deleted_keys.deleted_keys) {
			auto& delete_key = delete_kv.first;
			auto bytes = delete_key.get_bytes_array();
			dbval key = dbval{bytes};
			wtx.del(dbi, key);
		}

		if (thunk.get_exists_partial_exec()) {
			key_set = true;
			if (key_buf < thunk.partial_exec_key) {
			//auto res = memcmp(key_buf, thunk.partial_exec_key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);
			//if (res < 0) {
				key_buf = thunk.partial_exec_key;
//				memcpy(key_buf, thunk.partial_exec_key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);
			}
			INTEGRITY_CHECK("thunk threshold key: %s", DebugUtils::__array_to_str(thunk.partial_exec_key, MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());

		}
	}

	INTEGRITY_CHECK("final max key: %s", DebugUtils::__array_to_str(key_buf, MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());

	//auto& wtx = wtxn();
	
	if (print)
		std::printf("phase 2\n");

	auto cursor = wtx.cursor_open(dbi);

	//auto begin_cursor = wtx.cursor_open(dbi).begin();

	auto key_buf_bytes = key_buf.get_bytes_array();

	dbval key = dbval{key_buf_bytes};


	//MerkleTrieT::prefix_t key_backup = key_buf;
	//unsigned char key_backup[MerkleWorkUnit::WORKUNIT_KEY_LEN];
	//memcpy(key_backup, key_buf, MerkleWorkUnit::WORKUNIT_KEY_LEN);

	cursor.get(MDB_SET_RANGE, key); 

	//get returns the least key geq  key_buf.
	//So after one --, we get the greatest key < key_buf.
	// find greatest key leq key_buf;
	
	int num_deleted = 0;

	if ((!key_set) || (!cursor)) {
		INTEGRITY_CHECK("setting cursor to last, key_set = %d", key_set);
		cursor.get(MDB_LAST);
		//If the get operation fails, then there isn't a least key greater than key_buf.
		//if key was not set by any thunk, then all thunks left db in fully cleared state.
		// so we have to delete everything on disk.

		//So we must delete the entire database.


	} else {
		--cursor;
	}

	while (cursor) {
		cursor.del();
		--cursor;
		num_deleted++;
	}

	//while (cursor != begin_cursor) {
	//	--cursor;
	//	cursor.del();

	//	num_deleted++;
	//}
	INTEGRITY_CHECK_F(
		if (num_deleted > 0) {
			INTEGRITY_CHECK("num deleted is %u", num_deleted);
		}
	);

	//std::printf("num deleted first pass = %d\n", num_deleted);

	key_buf.clear();
//	memset(key_buf, 0, MerkleWorkUnit::WORKUNIT_KEY_LEN);


	if (print) 
		std::printf("phase 3\n");
	for (int i = relevant_thunks.size() - 1; i>= 0; i--) {

		if (relevant_thunks.at(i).current_block_number > current_block_number) {
			// again ignore inflight thunks
			continue;
		}

		if (print)
			std::printf("phase 3 i %d %lu\n", i, relevant_thunks[i].current_block_number);
		//std::printf("processing thunk %u\n", i);
		//auto res = memcmp(key_buf, relevant_thunks[i].partial_exec_key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);
		//if (res < 0) {
		if (key_buf < relevant_thunks[i].partial_exec_key) {
			key_buf = relevant_thunks[i].partial_exec_key;
		//	memcpy(key_buf, relevant_thunks[i].partial_exec_key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);
		
		}




		// no longer useful since we accumulate values into a list, not a sa copy of a trie.
		//LMDBInsertLambda func{wtx, dbi, debug};

		INTEGRITY_CHECK("apply_geq_key to %s", DebugUtils::__array_to_str(key_buf, MerkleWorkUnit::WORKUNIT_KEY_LEN).c_str());

		Price min_exec_price = PriceUtils::read_price_big_endian(key_buf);

		MerkleWorkUnit::MerkleTrieT::prefix_t offer_key_buf;

//		unsigned char offer_key_buf[MerkleWorkUnit::WORKUNIT_KEY_LEN];

		if (print)
			std::printf("thunks[i].uncommitted_offers_vec.size() %lu , current_block_number %lu\n",
			relevant_thunks[i].uncommitted_offers_vec.size(), relevant_thunks[i].current_block_number);
		
		for (auto idx = relevant_thunks[i].uncommitted_offers_vec.size(); idx > 0; idx--) { // can't test an unsigned for >=0 meaningfully

			auto& cur_offer = relevant_thunks[i].uncommitted_offers_vec[idx-1]; //hence idx -1
			if (cur_offer.amount == 0) {
				std::printf("cur_offer.owner %lu id %lu cur_offer.amount %lu cur_offer.minPrice %lu\n", cur_offer.owner, cur_offer.offerId, cur_offer.amount, cur_offer.minPrice);
				throw std::runtime_error("tried to persist an amount 0 offer!");
			}
			
			//	std::printf("%lu %lu\n", idx, cur_offer.minPrice);
			if (cur_offer.minPrice >= min_exec_price) {
				MerkleWorkUnit::generate_key(&cur_offer, offer_key_buf);
				bool db_put = false;
				if (cur_offer.minPrice > min_exec_price) {
					db_put = true;
				} else {

					if (offer_key_buf >= key_buf) {
					//auto memcmp_res = memcmp(offer_key_buf.data(), key_buf, MerkleWorkUnit::WORKUNIT_KEY_LEN);
					//if (memcmp_res >= 0) {
						db_put = true;
					}
				}
				if (db_put) {

					auto offer_key_buf_bytes = offer_key_buf.get_bytes_array();
					dbval db_key = dbval{offer_key_buf_bytes};//offer_key_buf.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN};

					auto value_buf = xdr::xdr_to_msg(cur_offer);
					dbval value = dbval{value_buf->data(), value_buf->size()};
					wtx.put(dbi, db_key, value);
				} else {
					break;
				}
			} else {
				break;
			}
		}
		if (print)
			std::printf("done phase 3 loop\n");
	}

	//insert the partial exec offers

	if (print)
		std::printf("phase 4\n");

	for (size_t i = 0; i < relevant_thunks.size(); i++) {

		if (relevant_thunks.at(i).current_block_number > current_block_number) {
			// again ignore inflight thunks
			throw std::runtime_error("impossible");
			continue;
		}

		if (print)
			std::printf("phase 4 i %lu %lu\n", i, relevant_thunks[i].current_block_number);

		//thunks[i].uncommitted_offers.apply_geq_key(func, key_buf);

		INTEGRITY_CHECK("thunks[i].uncommitted_offers.size() = %d", relevant_thunks[i].uncommitted_offers.size());

		INTEGRITY_CHECK("num values put = %d", func.num_values_put);


		if (!relevant_thunks[i].get_exists_partial_exec()) {
			INTEGRITY_CHECK("no partial exec, continuing to next thunk");
			continue;
		}


		auto partial_exec_key_bytes = relevant_thunks[i].partial_exec_key.get_bytes_array();
		dbval partial_exec_key{partial_exec_key_bytes};//(relevant_thunks[i].partial_exec_key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);

		auto get_res = wtx.get(dbi, partial_exec_key);

		if (!get_res) {
			INTEGRITY_CHECK("didn't find partial exec key because of preemptive clearing");
			continue;
			//throw std::runtime_error("did not find offer that should be in lmdb");
		}

		//offer in memory
		Offer partial_exec_offer;// = thunks[i].preexecute_partial_exec_offer;

		//if (get_res) {
			//std::printf("partial exec of preexisting offer\n");
			//use offer on disk instead, if it exists.
			//This lets us process partial executions in backwards order.
		dbval_to_xdr(*get_res, partial_exec_offer);
		//}

		//Offer partial_exec_offer = thunks[i].preexecute_partial_exec_offer;

		if ((uint64_t)relevant_thunks[i].partial_exec_amount > partial_exec_offer.amount) {
			throw std::runtime_error("can't have partial exec offer.amount < thunk[i].partial_exec_amount");
		}

		partial_exec_offer.amount -= relevant_thunks[i].partial_exec_amount;


		if (relevant_thunks[i].partial_exec_amount < 0) {
			//allowed to be 0 if no partial exec
			std::printf("thunks[i].partial_exec_amount = %ld\n", relevant_thunks[i].partial_exec_amount);
			throw std::runtime_error("invalid thunks[i].partial_exec_amount");
		}

		if (partial_exec_offer.amount < 0) {	
			std::printf("partial_exec_offer.amount = %ld]n", partial_exec_offer.amount);
			std::printf("relevant_thunks.partial_exec_amount was %ld\n", relevant_thunks[i].partial_exec_amount);
			throw std::runtime_error("invalid partial exec offer leftover amount");
		}

		//std::printf("producing dbval for modified offer\n");

		if (partial_exec_offer.amount > 0) {
			auto modified_offer_buf = xdr::xdr_to_opaque(partial_exec_offer);
			dbval modified_offer = dbval{modified_offer_buf.data(), modified_offer_buf.size()};//xdr_to_dbval(partial_exec_offer);
			wtx.put(dbi, partial_exec_key, modified_offer);
		} else {
			//partial_exec_offer.amount = 0
			wtx.del(dbi, partial_exec_key);
		}

	}
	
	if (print)
		std::printf("phase 5\n");
	//finally, clear the partial exec offers, if they clear
	for (size_t i = 0; i < relevant_thunks.size(); i++) {

		if (relevant_thunks[i].current_block_number > current_block_number) {
			throw std::runtime_error("impossible!");
			// again ignore inflight thunks
			continue;
		}

		if (print)
			std::printf("phase 5 i %lu %lu\n", i, relevant_thunks[i].current_block_number);

		if (!relevant_thunks[i].get_exists_partial_exec()) {
			continue;
		}

		for (size_t future = i + 1; future < relevant_thunks.size(); future++) {
			if (relevant_thunks[future].current_block_number > current_block_number) {
				throw std::runtime_error("impossible!!!");
				continue;
			}

			if (print)
				std::printf("continuing to future %lu %lu\n", future, relevant_thunks[future].current_block_number);
			if (relevant_thunks[i].partial_exec_key < relevant_thunks[future].partial_exec_key) {
			//auto res = memcmp(relevant_thunks[i].partial_exec_key.data(), relevant_thunks[future].partial_exec_key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);
			//if (res < 0) {
				//strictly less than 0 - that is, round i's key is strictly less than round future's, so i's partial exec offer then fully clears in future.
				// We already took care of the 0 case in the preceding loop.
				auto partial_exec_key_bytes = relevant_thunks[i].partial_exec_key.get_bytes_array();
				dbval partial_exec_key{partial_exec_key_bytes};//(relevant_thunks[i].partial_exec_key.data(), MerkleWorkUnit::WORKUNIT_KEY_LEN);
				wtx.del(dbi, partial_exec_key);
			}
		}
	}
	if (print)
		std::printf("done saving workunit\n");

	for (auto& thunk : relevant_thunks) {
		thunk.cleared_offers.detached_delete();
	}
}


void MerkleWorkUnit::load_lmdb_contents_to_memory() {
	auto rtx = lmdb_instance.rbegin();
	auto cursor = rtx.cursor_open(lmdb_instance.get_data_dbi());

	MerkleTrieT::prefix_t key_buf;
	//unsigned char key_buf[MerkleWorkUnit::WORKUNIT_KEY_LEN];

	for (auto kv : cursor) {
		Offer offer;
		dbval_to_xdr(kv.second, offer);
		MerkleWorkUnit::generate_key(offer, key_buf);
		if (offer.amount <= 0) {

			std::printf("offer.owner = %lu offer.amount = %ld offer.offerId = %lu sellAsset %u buyAsset %u\n",
					offer.owner,
					offer.amount,
					offer.offerId,
					offer.category.sellAsset,
					offer.category.buyAsset);
					//WorkUnitManagerUtils::category_to_idx(offer.category, 20));
			std::fflush(stdout);
			throw std::runtime_error("invalid offer amount present in database!");
		}
		committed_offers.insert(key_buf, TrieValueT(offer));
	}

	generate_metadata_index();
}

}
