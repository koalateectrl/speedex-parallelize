#pragma once

#include "xdr/types.h"
#include "price_utils.h"
#include <cstdint>
#include "database.h"

#include "fixed_point_value.h"

namespace edce {

template<typename Database>
static void
clear_offer_full(const Offer& offer, const Price& sellPrice, const Price& buyPrice, const uint8_t tax_rate, Database& db, const account_db_idx db_idx) {


	auto amount = FractionalAsset::from_integral(offer.amount);

	auto buy_amount_fractional = 
		FractionalAsset::from_raw(
			PriceUtils::wide_multiply_val_by_a_over_b(
				amount.value, 
				sellPrice,
				buyPrice));



	//buy_amount_fractional -= (buy_amount_fractional >> tax_rate);

	//int64_t sell_amount = offer.amount;

	//db.transfer_escrow(db_idx, offer.category.sellAsset, -sell_amount);
	db.transfer_available(db_idx, offer.category.buyAsset, buy_amount_fractional.tax_and_round(tax_rate));
}

template<typename Database>
static void
clear_offer_partial(
	const Offer& offer, 
	const Price& sellPrice, 
	const Price& buyPrice,
	const uint8_t tax_rate,
	const FractionalAsset& remaining_to_clear, 
	Database& db,
	const account_db_idx db_idx,
	int64_t& out_sell_amount, 
	int64_t& out_buy_amount) {

	auto buy_amount_fractional = FractionalAsset::from_raw(
		PriceUtils::wide_multiply_val_by_a_over_b(
			remaining_to_clear.value, sellPrice, buyPrice));

	//buy_amount_fractional -= (buy_amount_fractional >> tax_rate);

	//out_buy_amount = buy_amount_fractional.floor();//FractionalAsset::from_raw(buy_amount_raw).floor();
	out_buy_amount = buy_amount_fractional.tax_and_round(tax_rate);
	out_sell_amount = remaining_to_clear.ceil();
	
	//db.transfer_escrow(db_idx, offer.category.sellAsset, out_sell_amount);
	db.transfer_available(db_idx, offer.category.buyAsset, out_buy_amount);

}


}