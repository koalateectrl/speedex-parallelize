#pragma once

#include "xdr/block.h"

namespace edce {

struct BlockStateUpdateStatsWrapper : public BlockStateUpdateStats {

	BlockStateUpdateStatsWrapper& operator+=(const BlockStateUpdateStats& other) {
		new_offer_count += other.new_offer_count;
		cancel_offer_count += other.cancel_offer_count;
		fully_clear_offer_count += other.fully_clear_offer_count;
		partial_clear_offer_count += other.partial_clear_offer_count;
		payment_count += other.payment_count;
		new_account_count += other.new_account_count;
		return *this;
	}

	BlockStateUpdateStats& get_xdr() {
		return *this;
	}
};

struct AtomicBlockStateUpdateStatsWrapper : public BlockStateUpdateStatsWrapper {
	std::mutex mtx;
	AtomicBlockStateUpdateStatsWrapper& operator+=(const BlockStateUpdateStats& other) {
		std::lock_guard lock(mtx);
		this -> operator+=(other);
		return *this;
	}
};

} /* edce */