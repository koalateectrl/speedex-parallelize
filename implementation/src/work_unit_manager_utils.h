#pragma once
#include "xdr/types.h"
#include "offer_clearing_params.h"
#include "fixed_point_value.h"
#include "work_unit_state_commitment.h"
#include "merkle_work_unit.h"
#include <tbb/parallel_for.h>


namespace edce {

class WorkUnitManagerUtils {
	static int map_type_to_int(OfferType type) {
		switch(type) {
			case SELL:
				return 0;
		}
		throw std::runtime_error("invalid offer type");
	}

	static OfferType map_int_to_type(int type) {
		switch(type) {
			case 0:
				return OfferType::SELL;
		}
		return static_cast<OfferType>(-1);
	}

public:

	static int category_to_idx(const OfferCategory& id, const unsigned int asset_count) {

		if (id.sellAsset >= asset_count || id.buyAsset >= asset_count) {
			std::printf("asset_count %u sellAsset %u buyAsset %u\n", asset_count, id.sellAsset, id.buyAsset);
			throw std::runtime_error("invalid asset number");
		}
		int units_per_order_type = asset_count * (asset_count - 1);
		int idx_in_order_type = id.sellAsset * (asset_count - 1) + (id.buyAsset - (id.buyAsset > id.sellAsset? 1:0));

		int idx_out = units_per_order_type * map_type_to_int(id.type) + idx_in_order_type;

		return idx_out;
	}

	static OfferCategory category_from_idx(const int idx, const int asset_count) {
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
	}

	static bool validate_category(const OfferCategory& id, const unsigned int asset_count) {
		if (id.sellAsset == id.buyAsset) return false;
		if (id.sellAsset >= asset_count || id.buyAsset >= asset_count) return false;
		return true;
	}

	static unsigned int get_num_work_units_by_asset_count(unsigned int asset_count) {
		return NUM_OFFER_TYPES * (asset_count * (asset_count - 1));
	}

	static bool check_clearing_params(WorkUnitStateCommitmentChecker& clearing_checker, const int num_assets) {
		auto num_work_units = clearing_checker.commitments.size();

		std::vector<FractionalAsset> supplies, demands;
		for (int i = 0; i < num_assets; i++) {
			supplies.emplace_back();
			demands.emplace_back();
		}

		auto& prices = clearing_checker.prices;

		for (unsigned int i = 0; i < num_work_units; i++) {
			auto category = category_from_idx(i, num_assets);

			auto supply_activated = clearing_checker[i].fractionalSupplyActivated();

			supplies[category.sellAsset] += supply_activated;

			auto demanded_raw = PriceUtils::wide_multiply_val_by_a_over_b(supply_activated.value, prices[category.sellAsset], prices[category.buyAsset]);
			//demanded_raw -= (demanded_raw >> clearing_checker.tax_rate);
			demands[category.buyAsset] += FractionalAsset::from_raw(demanded_raw);
		}

		for (int i = 0; i < num_assets; i++) {
			FractionalAsset taxed_demand = demands[i].tax(clearing_checker.tax_rate);//demands[i] - (demands[i]>>clearing_checker.tax_rate);

			CLEARING_INFO("asset %d supplies %lf demands %lf taxed_demand %lf", i, supplies[i].to_double(), demands[i].to_double(), taxed_demand.to_double());
			if (supplies[i] < taxed_demand) {
				CLEARING_INFO("invalid clearing: asset %d", i);
				return false;
			}
		}
		return true;
	}

	constexpr static uint16_t RELATIVE_VOLUME_BASEPT = 16;
	constexpr static double UINT16_MAX_DOUBLE = UINT16_MAX;

	static uint16_t safe_volume_calc(const FractionalAsset& max, const FractionalAsset& supply) {
		double candidate_out = (max.to_double() * ((double) RELATIVE_VOLUME_BASEPT)) / supply.to_double();
		if (candidate_out >= UINT16_MAX_DOUBLE) {
			return UINT16_MAX;
		}
		return std::floor(candidate_out);
	}

	static void get_relative_volumes(const ClearingParams& params, const Price* prices, const unsigned int num_assets, uint16_t* relative_volumes_out) {
		auto num_work_units = params.work_unit_params.size();

		FractionalAsset* supplies = new FractionalAsset[num_assets];
		//FractionalAsset* demands = new FractionalAsset[num_assets];

		FractionalAsset avg = FractionalAsset::from_raw(0);

		for (size_t i = 0; i < num_work_units; i++) {
			auto category = category_from_idx(i, num_assets);
			supplies[category.sellAsset] += params.work_unit_params[i].supply_activated * prices[category.sellAsset];
		//	demands[category.buyAsset] += FractionalAsset::from_raw(
		//			PriceUtils::wide_multiply_val_by_a_over_b(
		//				params.work_unit_params[i].supply_activated.value,
		//				prices[category.sellAsset],
		//				prices[category.buyAsset]));
		}

		FractionalAsset max = FractionalAsset::from_raw(0);

		for (size_t i = 0; i < num_assets; i++) {
			max = std::max(max, supplies[i]);
			avg += supplies[i];
		}

		avg.value /= num_assets;

		for (size_t i = 0; i < num_assets; i++) {

			//std::printf("%lu %lf %lf\n", i, supplies[i].to_double(), demands[i].to_double());
			if (supplies[i].value > 0) {
				relative_volumes_out[i] = safe_volume_calc(max, supplies[i]);
			} else {
				relative_volumes_out[i] = safe_volume_calc(max, avg);
			}
		}
		delete[] supplies;
//		delete[] demands;
	}

	static bool check_clearing_params(const ClearingParams& params, const Price* prices, const unsigned int num_assets) {

		auto num_work_units = params.work_unit_params.size();

		FractionalAsset* supplies = new FractionalAsset[num_assets];
		FractionalAsset* demands = new FractionalAsset[num_assets];

		auto tax_rate = params.tax_rate;
		CLEARING_INFO("tax rate:%u", tax_rate);


		//TODO i'm a fool
		// you can't do a parallel for here without proper atomic variables
		// you could do a paralle join instead
		
		for (unsigned int i = 0; i < num_work_units; i++) {
			auto category = category_from_idx(i, num_assets);
			auto& supply_activated = params.work_unit_params[i].supply_activated;

			supplies[category.sellAsset] += supply_activated;
			auto demanded = PriceUtils::wide_multiply_val_by_a_over_b(supply_activated.value, prices[category.sellAsset], prices[category.buyAsset]);
			demands[category.buyAsset] += FractionalAsset::from_raw(demanded);
		}

		CLEARING_INFO("rounded asset results");
		CLEARING_INFO("Asset\tsupply\tdemand\tprice");
		for (unsigned int i = 0; i < num_assets; i++) {
			FractionalAsset taxed_demand = demands[i].tax(tax_rate);//demands[i] - (demands[i]>>tax_rate);
			if (supplies[i] < taxed_demand) {
				CLEARING_INFO("failed on %d %f %f", i,supplies[i].to_double(), taxed_demand.to_double());
				delete[] supplies;
				delete[] demands;
				return false;
			}
			CLEARING_INFO("%d %f %f %f", i, supplies[i].to_double(), taxed_demand.to_double(), PriceUtils::to_double(prices[i]));
		}
		delete[] supplies;
		delete[] demands;
		return true;
	}

	static double get_weighted_price_asymmetry_metric(const ClearingParams& clearing_params, const std::vector<MerkleWorkUnit>& work_units, const Price* prices) {
		auto num_work_units = work_units.size();

		double total_vol = 0;
		double weighted_vol = 0;

		for (size_t i = 0; i < num_work_units; i++) {
			double feasible_mult = work_units[i].max_feasible_smooth_mult_double(clearing_params.work_unit_params[i].supply_activated.ceil(), prices);
			auto category = work_units[i].get_category();
			Price sell_price = prices[category.sellAsset];

			double volume = clearing_params.work_unit_params[i].supply_activated.to_double() * PriceUtils::to_double(sell_price);
			total_vol += volume;
			//if (feasible_mult != 255) {
				//double smooth_mult_d = std::pow(0.5, feasible_mult);
			weighted_vol += feasible_mult * volume;
			//}
		}
		return weighted_vol / total_vol;
	}

};

}; /* edce */
