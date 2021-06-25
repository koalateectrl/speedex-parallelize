#pragma once

#include "xdr/types.h"
#include "xdr/transaction.h"

#include "database_types.h"
#include "block_update_stats.h"

#include <cstdint>

namespace edce {

template<typename Database, typename DatabaseViewType>
struct OperationMetadata {
	const TransactionMetadata& tx_metadata;
	const account_db_idx source_account_idx;
	DatabaseViewType db_view; 
	uint64_t operation_id;
	BlockStateUpdateStats local_stats;

	template<typename... DBViewArgs>
	OperationMetadata(
		const TransactionMetadata& tx_metadata,
		const account_db_idx source_account_idx,
		Database& db,
		DBViewArgs... args)
	: tx_metadata(tx_metadata)
	, source_account_idx(source_account_idx)
	, db_view(args..., db)
	, operation_id(0) {}

	void commit(BlockStateUpdateStatsWrapper& stats) {
		stats += local_stats;
		db_view.commit();
	}
	void unwind() {
		db_view.unwind();
	}
};

/*
Scoring rules
hanson's proper scoring rule as a keyword

lack of leverage makes it different from an option.

it's always worse to fill via stop orders than from buying the underlying

short sellers are used to taking a ton of abuse

Mark kaodis?  shortseller

Cap-weighted funds have this positive-feedback effect, crazy stuff happens

think about presenting hanson's stuff like week after next


*/

}