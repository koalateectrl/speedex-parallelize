#include "database.h"
#include "merkle_work_unit_manager.h"
#include "end_to_end_simulator.h"

#include <getopt.h>

#include <random>

using namespace edce;

int main(int argc, char* const *argv)
{
	int tax_rate = 10;
	int smooth_mult = 5;
	int num_assets = 10;
	int num_accounts = 100000;
	int num_tx_per_thread = 100000;
	int num_block_gen_threads = 1;

	static struct option long_options[] =
	{
		{"tax_rate", required_argument, 0, 'r'},
		{"smooth_mult", required_argument, 0, 's'},
		{"assets", required_argument, 0, 'a'},
		{"accounts", required_argument, 0, 'A'},
		{"tx_gen_threads", required_argument, 0, 't'},
		{"tx_per_thread", required_argument, 0, 'T'},
		{0, 0, 0, 0}
	};

	bool done_opt = false;
	while(!done_opt) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "a:A:s:r:t:T:", long_options, &option_index);

		switch(c) {
			case 'r':
				tax_rate = std::stoi(std::string(optarg));
				break;
			case 's':
				smooth_mult = std::stoi(std::string(optarg));
				break;
			case 't':
				num_block_gen_threads = std::stoi(std::string(optarg));
				break;
			case 'T':
				num_tx_per_thread = std::stoi(std::string(optarg));
				break;
			case 'A':
				num_accounts = std::stoi(std::string(optarg));
				break;
			case 'a':
				num_assets = std::stoi(std::string(optarg));
				break;
			default:
				done_opt = true;
		}
	}

	std::printf("Running simulations with %d agents trading %d assets\ntx generator threads = %d, tx per thread per block = %d\n tax_rate = %d, smooth_mult = %d\n",
		num_accounts, num_assets, num_block_gen_threads, num_tx_per_thread, tax_rate, smooth_mult);



	MemoryDatabase db;



	MerkleWorkUnitManager manager(smooth_mult, tax_rate, num_assets);

	EndToEndSimulator simulator(db, manager, num_tx_per_thread, num_block_gen_threads, num_accounts);

	Price* prices = new Price[num_assets];

	std::mt19937 gen(0);

	std::uniform_real_distribution<> valuation(1, 10);
	Price* underlying_prices = new Price[num_assets];

	for (int i = 0; i < num_assets; i++) {
		underlying_prices[i] = PriceUtils::from_double(valuation(gen));
	}

	simulator.init(prices);

	for (int i = 0; i < 5; i++) {
		std::printf("Starting block %d\n", i);
		for (int i = 0; i < num_assets; i++) {
			prices[i] = underlying_prices[i];
		}
		simulator.run_block(prices);
	}
	

	return 0;
}