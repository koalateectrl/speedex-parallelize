#include "synthetic_data_generator/synthetic_data_gen.h"
#include "synthetic_data_generator/synthetic_data_gen_options.h"

#include "edce_options.h"
#include "utils.h"

#include "xdr/cryptocoin_experiment.h"

#include <cstddef>

using namespace edce;

std::vector<double> get_cumulative_volumes(const CryptocoinExperiment& experiment, size_t idx) {
	std::vector<double> out;
	double acc = 0;
	for (size_t i = 0; i < experiment.coins.size(); i++) {
		std::printf("%s %lf\n", experiment.coins[i].name.c_str(), experiment.coins[i].snapshots[idx].volume);
		acc += experiment.coins[i].snapshots[idx].volume;
		
		out.push_back(acc);
	}
	return out;
}

std::vector<double> get_prices(const CryptocoinExperiment& experiment, size_t idx) {
	std::vector<double> out;
	for (size_t i = 0; i < experiment.coins.size(); i++) {
		double price = experiment.coins[i].snapshots[idx].price;
		out.push_back(price);
	}
	return out;
}

int main(int argc, char const *argv[])
{

	if (!(argc == 4 || argc == 5)) {
		std::printf("usage: ./blah <synthetic_data_params_yaml> <edce_options_yaml> <experiment_name> <params_only?>\n");
		return -1;
	}

	CryptocoinExperiment experiment;

	std::string coin_file = std::string("coingecko_data/unified_data");

	if (load_xdr_from_file(experiment, coin_file.c_str())) {
		throw std::runtime_error("failed to load " + coin_file);
	}

	size_t num_assets = experiment.coins.size();

	if (num_assets == 0) {
		throw std::runtime_error("no coins!");
	}

	size_t num_coin_datapts = experiment.coins[0].snapshots.size();

	for (size_t i = 1; i < num_assets; i++) {
		if (experiment.coins[i].snapshots.size() != num_coin_datapts) {
			throw std::runtime_error("invalid number of snapshots");
		}
	}

	GenerationOptions options;
	auto parsed = options.parse(argv[1]);
	if (!parsed) {
		std::printf("yaml parse error\n");
		return -1;
	}

	if (num_assets != options.num_assets) {
		throw std::runtime_error("mismatch between num coins and num assets in yaml");
	}


	ExperimentParameters params;
	params.num_assets = options.num_assets;
	params.num_accounts = options.num_accounts;

	EdceOptions edce_options;
	edce_options.parse_options(argv[2]);
	params.tax_rate = edce_options.tax_rate;
	params.smooth_mult = edce_options.smooth_mult;
	params.persistence_frequency = edce_options.persistence_frequency;
	params.num_blocks = options.num_blocks;

	std::string output_exp_name = std::string(argv[3]);

	if (mkdir_safe(options.output_prefix.c_str())) {
		std::printf("directory %s already exists, continuing\n", options.output_prefix.c_str());
	}

	std::string output_root = options.output_prefix + output_exp_name;

	std::string long_output_root = options.output_prefix + "long" + output_exp_name;

	if (mkdir_safe(output_root.c_str())) {
		std::printf("directory %s already exists, continuing\n", output_root.c_str());
	}

	if (mkdir_safe(long_output_root.c_str())) {
		std::printf("directory %s already exists, continuing\n", long_output_root.c_str());
	}

	auto params_file = output_root + std::string("params");
	if (save_xdr_to_file(params, params_file.c_str())) {
		throw std::runtime_error("failed to save params file");
	}
	params_file = long_output_root + std::string("params");
	if (save_xdr_to_file(params, params_file.c_str())) {
		throw std::runtime_error("failed to save params file");
	}

	if (argc == 5) {
		std::printf("only doing params, exiting\n");
		return 0;
	}

	std::minstd_rand gen(0);
/*
	GeneratorState generator(gen, options, output_root);

	for (size_t i = 0; i < num_coin_datapts; i++) {
		generator.asset_probabilities = get_cumulative_volumes(experiment, i);
		auto prices = get_prices(experiment, i);
		generator.make_block(prices);
	}
*/
	//std::string long_output_root = options.output_prefix + "long" + output_exp_name;

	//if (mkdir_safe(long_output_root.c_str())) {
	//	std::printf("directory %s already exists, continuing\n", long_output_root.c_str());
	//}

	std::minstd_rand gen2(0);

	GeneratorState generator2 (gen2, options, long_output_root);

	for (size_t i = 400; i < num_coin_datapts; i++) {
		generator2.asset_probabilities = get_cumulative_volumes(experiment, i);
		auto prices = get_prices(experiment, i);
		for (size_t j = 0; j < 5; j++) {
			generator2.make_block(prices);
		}
	}

	return 0;
}
