#include "synthetic_data_generator/synthetic_data_gen.h"


#include <cstddef>

#include "synthetic_data_generator/synthetic_data_gen_options.h"

#include "edce_options.h"

#include "xdr/experiments.h"


using namespace edce;

int main(int argc, char const *argv[])
{
	if (!(argc == 4 || argc == 5)) {
		std::printf("usage: ./synthetic_data_gen <edce_options> <experiment_yaml> <experiment_name> <just_params>\n");
		return 1;
	}

	std::minstd_rand gen(0);

	GenerationOptions options;
	auto parsed = options.parse(argv[2]);
	if (!parsed) {
		std::printf("yaml parse error\n");
		return 1;
	}
	std::printf("done parse\n");

	//auto txs = make_blocks(gen, options);

	ExperimentParameters params;
	params.num_assets = options.num_assets;
	params.num_accounts = options.num_accounts;

	EdceOptions edce_options;
	edce_options.parse_options(argv[1]);
	params.tax_rate = edce_options.tax_rate;
	params.smooth_mult = edce_options.smooth_mult;
	params.num_threads = 0; // SET LATER
	params.persistence_frequency = edce_options.persistence_frequency;
	params.num_blocks = options.num_blocks;

	std::string output_root = options.output_prefix + std::string(argv[3]) + std::string("/");

	if (mkdir_safe(options.output_prefix.c_str())) {
		std::printf("directory %s already exists, continuing\n", options.output_prefix.c_str());
	}
	if (mkdir_safe(output_root.c_str())) {
		std::printf("directory %s already exists, continuing\n", output_root.c_str());
	}

	/*auto first_res = mkdir(options.output_prefix.c_str(), exec_perms);
	if (first_res != 0) {

		if (errno == EEXIST) {
			std::printf("directory %s already exists, continuing\n", options.output_prefix.c_str());
		} else {
			throw std::runtime_error("unspecified mkdir error making data root");
		}
	}

	auto res = mkdir(output_root.c_str(), exec_perms);


	if (res != 0) {
		if (errno == EEXIST) {
			std::printf("directory %s already exists, continuing\n", output_root.c_str());
		} else {
			std::printf("could not make directory %s\n", output_root.c_str());
			std::printf("error %s\n", strerror(errno));
			throw std::runtime_error("unspecified mkdir error");
		}
	}*/

	if (params.num_assets != edce_options.num_assets) {
		throw std::runtime_error("mismatch in number of assets.  Are you sure?");
	}

	auto params_file = output_root + std::string("params");
	if (save_xdr_to_file(params, params_file.c_str())) {
		throw std::runtime_error("failed to save params file");
	}

	GeneratorState generator (gen, options, output_root);

	if (argc == 4) {
		generator.make_blocks();
	}

	std::printf("made experiment, output to %s\n", output_root.c_str());
}
