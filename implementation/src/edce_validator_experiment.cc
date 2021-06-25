#include "edce_node.h"
#include "edce_management_structures.h"
#include "consensus_api_server.h"

#include "tbb/global_control.h"
#include "singlenode_init.h"

using namespace edce;


void run_experiment(
	ExperimentParameters params, 
	std::string experiment_data_root, 
	std::string results_output_root, 
	EdceOptions& options, 
	std::string parent_hostname, 
	std::string self_hostname,
	const size_t num_threads) {

	EdceManagementStructures management_structures(
		options.num_assets,
		ApproximationParameters {
			options.tax_rate,
			options.smooth_mult
		});


	/*EdceManagementStructures management_structures{
		MemoryDatabase(),
		MerkleWorkUnitManager(
			options.smooth_mult,
			options.tax_rate,
			options.num_assets)
	};*/

	tbb::global_control control(
		tbb::global_control::max_allowed_parallelism, num_threads);

	init_management_structures_from_lmdb(management_structures);

	EdceNode node(management_structures, params, options, results_output_root, NodeType::BLOCK_VALIDATOR);

	ConsensusApiServer consensus_api_server(node);
	
	consensus_api_server.set_experiment_ready_to_start();
	consensus_api_server.wait_for_experiment_start();

	node.get_connection_manager().add_log_confirmation_target(parent_hostname, self_hostname);

	consensus_api_server.wait_until_upstream_finished();
	BLOCK_INFO("upstream finished");
	consensus_api_server.wait_until_block_buffer_empty(); // loop forever i.e. until signal from experiment controller
	BLOCK_INFO("block buffer flushed");
	node.get_connection_manager().shutdown_target_connections();
	BLOCK_INFO("target connections shutdown");
	consensus_api_server.set_experiment_done();
	BLOCK_INFO("experiment set to done");
	consensus_api_server.wait_for_experiment_start();
	BLOCK_INFO("got shutdown signal from controller, shutting down");

	node.write_measurements();
	BLOCK_INFO("shutting down");
	std::this_thread::sleep_for(std::chrono::seconds(5));
}

int main(int argc, char const *argv[])
{
	if (argc != 6) {
		std::printf("usage: ./whatever <data_directory> <results_directory> <upstream_hostname> <self_hostname> <num_threads>\n");
		return -1;
	}

	EdceOptions options;

	ExperimentParameters params;

	std::string experiment_data_root = std::string(argv[1]) + "/";
	std::string results_output_root = std::string(argv[2]) + "/";

	std::string params_filename = experiment_data_root + "params";

	if (load_xdr_from_file(params, params_filename.c_str())) {
		std::printf("couldn't load parameters file %s", params_filename.c_str());
		throw std::runtime_error("failed to load");
	}
	
	auto parent_hostname = std::string(argv[3]);
	auto self_hostname = std::string(argv[4]);

	int num_threads = std::stoi(argv[5]);

	ExperimentResults results;

	results.params.tax_rate = params.tax_rate;
	results.params.smooth_mult = params.smooth_mult;
	results.params.num_threads = num_threads;
	results.params.num_assets = params.num_assets;
	results.params.num_accounts = params.num_accounts;
	results.params.persistence_frequency = params.persistence_frequency;

	options.num_assets = params.num_assets;
	options.tax_rate = params.tax_rate;
	options.smooth_mult = params.smooth_mult;
	options.persistence_frequency = params.persistence_frequency;

	run_experiment(params, experiment_data_root, results_output_root, options, parent_hostname, self_hostname, num_threads);
	return 0;
}
