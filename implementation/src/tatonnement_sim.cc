#include "database.h"
#include "merkle_work_unit_manager.h"
#include "tatonnement_sim_setup.h"
#include "tatonnement_oracle.h"
#include "edce_management_structures.h"
#include "lp_solver.h"

#include <cmath>
#include <random>
#include <chrono>



using namespace edce;

int main(int argc, char const *argv[])
{
	constexpr int num_assets = 40;
	int tax_rate = 15;
	int smooth_mult = 10;

	EdceManagementStructures management_structures{
		MemoryDatabase(),
		MerkleWorkUnitManager(
			smooth_mult,
			tax_rate,
			num_assets),
		AccountModificationLog()
	};

	auto& db = management_structures.db;
	//MemoryDatabase db;

	auto& manager = management_structures.work_unit_manager;
	//MerkleWorkUnitManager manager(smooth_mult, tax_rate, num_assets);

	TatonnementSimSetup setup(management_structures);
	int num_accounts = 1000;
	int num_txs = 1000000;

	Price prices[num_assets];

	std::mt19937 gen(0);

	std::uniform_real_distribution<> valuation_dist(0.1, 10);
	for (int i = 0; i < num_assets; i++) {
		double value = valuation_dist(gen);
		value = std::exp(value);
		prices[i] = PriceUtils::from_double(value);
		std::printf("%d %f %lu\t\t%f\n", i, value, prices[i], PriceUtils::to_double(prices[i])/PriceUtils::to_double(prices[0]));
	}

	std::printf("starting to create accounts\n");


	setup.create_accounts(num_accounts);

	std::printf("made accounts\n");

	setup.create_txs(num_txs, num_accounts, num_assets, prices);

	std::printf("finished making setup\n");

	LPSolver solver(manager);

	TatonnementOracle oracle(manager, solver,  0);

	for (int i = 0; i < num_assets; i++) {
		prices[i] = PriceUtils::from_double(1);
	}
	auto start = std::chrono::high_resolution_clock::now();
	oracle.compute_prices(prices);

	auto end = std::chrono::high_resolution_clock::now();
	double time = ((double)std::chrono::duration_cast<std::chrono::microseconds>(end-start).count()) / 1000000;
	std::printf("elapsed time:%f\n", time);



	return 0;
}
