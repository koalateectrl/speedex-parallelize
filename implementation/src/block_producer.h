#pragma once

#include "edce_management_structures.h"
#include "mempool.h"
#include "xdr/block.h"
#include "async_worker.h"
#include "block_update_stats.h"
#include "log_merge_worker.h"

namespace edce {

class BlockProducer {

	EdceManagementStructures& management_structures;
	LogMergeWorker worker;

public:
	BlockProducer(EdceManagementStructures& management_structures)
		: management_structures(management_structures)
		, worker(management_structures) {}

	//output block is implicitly held within account_modification_log
	//returns (somewhat redundantly) total number of txs in block
	uint64_t
	build_block(
		Mempool& mempool,
		int64_t max_block_size,
		BlockCreationMeasurements& measurements,
		BlockStateUpdateStatsWrapper& state_update_stats);

};

} /* edce */