#include "work_unit_manager.h"

namespace edce {


/*int WorkUnitManager::look_up_idx(OfferCategory id, int asset_count) {
	int units_per_order_type = asset_count * (asset_count - 1);
	int idx_in_order_type = id.sellAsset * (asset_count - 1) + (id.buyAsset - (id.buyAsset > id.sellAsset? 1:0));

	int idx_out = units_per_order_type * map_type_to_int(id.type) + idx_in_order_type;

	return idx_out;
}

int WorkUnitManager::look_up_idx(OfferCategory id) {
	return look_up_idx(id, num_assets);
}

OfferCategory WorkUnitManager::category_from_idx(int idx, int asset_count) {
	int units_per_order_type = asset_count * (asset_count - 1);
	OfferType type = map_int_to_type(idx/units_per_order_type);
	int remainder = idx % units_per_order_type;

	AssetID sell_asset = remainder / (asset_count - 1);
	AssetID buy_asset = remainder % (asset_count - 1);
	if (buy_asset >= sell_asset) {
		buy_asset += 1;
	}
	
	OfferCategory output;
	output.type = type;
	output.buyAsset = buy_asset;
	output.sellAsset = sell_asset;
	return output;
}*/

/*int WorkUnitManager::look_up_idx(OfferCategory id) {
	//auto id = WorkUnitIdentifier(buy_asset, sell_asset, type);
	INFO("looking up category %ld %ld", id.sellAsset, id.buyAsset);
	auto iter = work_unit_idx_map.find(id);
	if (iter != work_unit_idx_map.end()) {
		INFO("found in committed section");
		return iter->second;
	}
	std::shared_lock<std::shared_mutex> shared_lock(*map_mtx);
	iter = uncommitted_map.find(id);
	if (iter != uncommitted_map.end()) {
		INFO("found in uncommitted section");
		return iter->second;
	}
	shared_lock.unlock();
	std::lock_guard<std::shared_mutex> single_lock(*map_mtx);
	iter = uncommitted_map.find(id);
	if (iter != uncommitted_map.end()) {
		return iter->second;
	}
	uncommitted_map[id] = work_units.size();

	INFO("made new work unit");

	work_units.emplace_back(std::move(WorkUnit(id, smooth_mult, tax_rate)));
	return work_units.size() - 1;
}*/

}