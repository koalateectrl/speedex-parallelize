#pragma once

#include "memory_database.h"
#include "merkle_work_unit_manager.h"
#include "account_modification_log.h"
#include "block_header_hash_map.h"
#include "approximation_parameters.h"

#include "tatonnement_oracle.h"
#include "lp_solver.h"
#include "normalization_factor_average.h"

namespace edce {


struct EdceManagementStructures {
	MemoryDatabase db;
	MerkleWorkUnitManager work_unit_manager;
	AccountModificationLog account_modification_log;
	BlockHeaderHashMap block_header_hash_map;
	ApproximationParameters approx_params;

	void create_lmdb() {
		db.create_lmdb();
		work_unit_manager.create_lmdb();
		block_header_hash_map.create_lmdb();
	}

	void open_lmdb_env() {
		db.open_lmdb_env();
		work_unit_manager.open_lmdb_env();
		block_header_hash_map.open_lmdb_env();
	}

	void open_lmdb() {
		db.open_lmdb();
		work_unit_manager.open_lmdb();
		block_header_hash_map.open_lmdb();
	}

	EdceManagementStructures(uint16_t num_assets, ApproximationParameters approx_params)
		: db()
		, work_unit_manager(num_assets)
		, account_modification_log()
		, block_header_hash_map()
		, approx_params(approx_params) {}
};

struct TatonnementManagementStructures {
	LPSolver lp_solver;
	TatonnementOracle oracle;
	NormalizationRollingAverage rolling_averages;

	TatonnementManagementStructures(EdceManagementStructures& management_structures)
		: lp_solver(management_structures.work_unit_manager)
		, oracle(management_structures.work_unit_manager, lp_solver, 0)
		, rolling_averages(management_structures.work_unit_manager.get_num_assets()) {}
};

} /* edce */