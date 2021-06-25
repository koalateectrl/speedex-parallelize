#include "database.h"
#include "merkle_work_unit_manager.h"
#include "tatonnement_sim_setup.h"
#include "tatonnement_oracle.h"
#include "lp_solver.h"

#include <cmath>
#include <random>
#include <chrono>



using namespace edce;

double run_experiment(int num_assets, int num_accounts, int seed) {
	//MemoryDatabase db;

	int tax_rate = 20;
	int smooth_mult = 7;
	//MerkleWorkUnitManager manager(smooth_mult, tax_rate, num_assets);


	EdceManagementStructures management_structures{
		MemoryDatabase(),
		MerkleWorkUnitManager(
			smooth_mult,
			tax_rate,
			num_assets),
		AccountModificationLog()
	};

	auto& manager = management_structures.work_unit_manager;

	TatonnementSimSetup setup(management_structures);

	Price prices[num_assets];

	std::minstd_rand gen(seed);

	std::uniform_real_distribution<> valuation_dist (0.0, 1.0); 


	for (int i = 0; i < num_assets; i++) {
		prices[i] = PriceUtils::from_double(valuation_dist(gen) * 100 + 10);
	}


	setup.create_accounts(num_accounts);

	setup.create_cvxpy_comparison_txs(num_accounts, num_assets, prices, seed);

	LPSolver lp_solver(manager);
	TatonnementOracle oracle(manager, lp_solver, 0);


	for (int i = 0; i < num_assets; i++) {
		prices[i] = PriceUtils::from_double(1);
	}

	auto start = std::chrono::high_resolution_clock::now();

	oracle.compute_prices(prices);
	auto end = std::chrono::high_resolution_clock::now();

	return ((double)std::chrono::duration_cast<std::chrono::microseconds>(end-start).count()) / 1000000.0;
}

void format_output(FILE* f, int num_assets, int num_accounts, int seed) {

	std::printf("starting %d\n", num_accounts);
	double time = run_experiment(num_assets, num_accounts, seed);
	std::printf("%d,%d,%d,%f\n", num_assets, num_accounts, seed, time);
	fprintf(f, "%d,%d,%d,%f\n", num_assets, num_accounts, seed, time);

}

int main(int argc, char const *argv[])
{

	if (argc != 2) {
		std::printf("need file output name\n");
		return -1;
	}

	FILE* f = std::fopen(argv[1], "w");


	for (int i = 0; i < 5; i++) {
		format_output(f, 20, 400, i);
	}
	//format_output(f, 20, 500, 2);

	/*for (int i = 0; i < 5; i++) {
		//format_output(f, 20, 200, i);
		//format_output(f, 20, 300, i);
		//format_output(f, 20, 400, i);
		format_output(f, 20, 600, i);
		format_output(f, 20, 800, i);
		//format_output(f, 20, 100, i);
		format_output(f, 20, 1000, i);
		format_output(f, 20, 1200, i);
		format_output(f, 20, 1500, i);
		format_output(f, 20, 10000, i);
		format_output(f, 20, 20000, i);
		format_output(f, 20, 50000, i);
		format_output(f, 20, 100000, i);
		format_output(f, 20, 500000, i);
		std::fflush(f);
	}*/

	/*for (int i = 1; i < 10; i++) {
		format_output(f, 10, 100, i);
		format_output(f, 10, 1000, i);
		format_output(f, 10, 10000, i);
		format_output(f, 10, 100000, i);
		format_output(f, 10, 1000000, i);
		format_output(f, 20, 1000, i);
		format_output(f, 20, 10000, i);
		format_output(f, 20, 100000, i);
		format_output(f, 20, 1000000, i);
		std::fflush(f);
	}
	for (int i = 1; i < 10; i++) {
		format_output(f, 30, 1000, i);
		format_output(f, 30, 10000, i);
		format_output(f, 30, 100000, i);
		format_output(f, 30, 1000000, i);
		format_output(f, 40, 1000, i);
		format_output(f, 40, 10000, i);
		format_output(f, 40, 100000, i);
		format_output(f, 40, 1000000, i);
		format_output(f, 50, 10000, i);
		format_output(f, 50, 100000, i);
		format_output(f, 50, 1000000, i);
		std::fflush(f);
	}
	for (int i = 1; i < 10; i++) {

		format_output(f, 10, 10000000, i);
		format_output(f, 20, 10000000, i);
		format_output(f, 30, 10000000, i);
		format_output(f, 40, 10000000, i);
		format_output(f, 50, 10000000, i);
		std::fflush(f);

	}
	for (int i = 1; i < 10; i++) {

		format_output(f, 10, 100000000, i);
		format_output(f, 20, 100000000, i);
		format_output(f, 30, 100000000, i);
		format_output(f, 40, 100000000, i);
		format_output(f, 50, 100000000, i);
		std::fflush(f);

	}*/
	std::fflush(f);
	std::fclose(f);



}