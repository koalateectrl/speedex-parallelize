#pragma once

#include <cstdint>
#include <memory>
#include <atomic>
#include <thread>

#include "merkle_work_unit_manager.h"
#include "operation_metadata.h"
#include "serial_transaction_processor.h"
#include "signature_check.h"
#include "account_modification_log.h"
#include "block_update_stats.h"

#include "xdr/transaction.h"
#include "xdr/ledger.h"
#include "xdr/block.h"

namespace edce {

bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const AccountModificationBlock& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& state_update_stats);

bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const TransactionData& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& state_update_stats);

bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const SignedTransactionList& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& state_update_stats);

bool validate_transaction_block(
	EdceManagementStructures& management_structures,
	const SerializedBlock& transactions,
	const WorkUnitStateCommitmentChecker& clearing_commitment,
	ThreadsafeValidationStatistics& main_stats,
	BlockValidationMeasurements& measurements,
	BlockStateUpdateStatsWrapper& state_update_stats);



void replay_trusted_block(
	EdceManagementStructures& management_structures,
	const AccountModificationBlock& block,
	const HashedBlock& header);

} /* edce */