#include "tatonnement_sim_experiment.h"

#include "xdr/experiments.h"
#include "utils.h"

using namespace edce;

int main(int argc, char const *argv[])
{
	if (argc != 3) {
		std::printf("usage: <blah> data_directory outfolder\n");
		return -1;
	}

	ExperimentParameters params;

	std::string experiment_root = std::string(argv[1]) + "/";
	std::string params_filename = experiment_root + "params";
	if (load_xdr_from_file(params, params_filename.c_str())) {
		throw std::runtime_error("failed to load params file");
	}

	bool small = false;

	std::vector <size_t> num_tx_list = small ?
		std::vector<size_t>({500, 1'000, 5'000, 10'000, 20'000, 50'000, 100'000, 200'000, 500'000}) :
		std::vector<size_t>({500, 1'000, 2'000, 5'000, 10'000, 20'000, 30'000, 40'000, 50'000, 60'000, 70'000, 80'000, 90'000, 100'000, 200'000, 300'000, 400'000, 500'000});
;

	ApproximationParameters approx_params;
	approx_params.smooth_mult = params.smooth_mult,
	approx_params.tax_rate = params.tax_rate;

	EdceManagementStructures management_structures {
		params.num_assets,
		approx_params
	};

	/*EdceManagementStructures management_structures {
		MemoryDatabase(),
		MerkleWorkUnitManager(
			params.smooth_mult,
			params.tax_rate,
			params.num_assets),
		AccountModificationLog()
	};*/

	TatonnementSimExperiment experiment_runner(management_structures, std::string(argv[2]), params.num_assets, params.num_accounts);

	std::vector<uint8_t> tax_rates = small? std::vector<uint8_t>({10,15,20}) :
		 std::vector<uint8_t>({5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20});
	std::vector<uint8_t> smooth_mults = small ? std::vector<uint8_t>({5,10,15,20}) :
		 std::vector<uint8_t>({5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20});
	
	std::vector<ExperimentBlock> trials;

	for (int i = 0; i < 5; i++) {
		std::string filename = experiment_root + std::to_string(i+1) + ".txs";
		trials.emplace_back();
		if (load_xdr_from_file(trials.back(), filename.c_str())) {
			std::printf("filename was %s\n", filename.c_str());
			throw std::runtime_error("failed to load trial data file");
		}
	}

	for (auto tax_rate : tax_rates) {
		for (auto smooth_mult : smooth_mults) {
			if (experiment_runner.check_preexists(smooth_mult, tax_rate)) {
				continue;
			}
			std::printf("running %u %u\n", smooth_mult, tax_rate);
			experiment_runner.run_experiment(smooth_mult, tax_rate, num_tx_list, trials);
		}
	}




	return 0;
}
