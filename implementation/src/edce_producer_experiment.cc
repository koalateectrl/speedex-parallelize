#include "edce_node.h"
#include "edce_management_structures.h"
#include "consensus_api_server.h"

#include <tbb/global_control.h>
#include "singlenode_init.h"

using namespace edce;

struct MempoolManager {
	constexpr static size_t BUFFER_SIZE = 100'000'000;

	std::string experiment_data_root;
	EdceNode& node;
	std::thread mempool_thread;

	std::mutex mtx;
	std::condition_variable cv;

	bool done_flag = false;
	std::optional<int64_t> additional_tx_count = std::nullopt;

	uint64_t tx_block_number = 1;

	bool experiment_done_flag = false;

	unsigned char* buffer;

	MempoolManager(
		std::string experiment_data_root,
		EdceNode& node)
	: experiment_data_root(experiment_data_root)
	, node(node)
	, mtx()
	, cv() {
		mempool_thread = std::thread([this] {run();});
		buffer = new unsigned char[BUFFER_SIZE];
	}

	~MempoolManager() {
		{
			std::lock_guard lock(mtx);
			done_flag = true;
			cv.notify_one();
		}
		mempool_thread.join();
		delete[] buffer;
	}

	void run() {
		while (true) {
			std::unique_lock lock(mtx);
			if (((!done_flag) && (!additional_tx_count))) {
				cv.wait(lock, [this] { return ((bool) additional_tx_count) || done_flag;});
			}
			if (done_flag) return;

			experiment_done_flag = experiment_done_flag || add_txs_to_mempool();
			additional_tx_count = std::nullopt;
			cv.notify_one();
			if (experiment_done_flag) return;
		}
	}

	bool add_txs_to_mempool() {
		ExperimentBlock data;
		int64_t tx_to_add = *additional_tx_count;
		BLOCK_INFO("adding %lu txs to mempool", tx_to_add);
		while (tx_to_add > 0) {
			auto filename = experiment_data_root + std::to_string(tx_block_number) + ".txs";

			if(load_xdr_from_file_fast(data, filename.c_str(), buffer, BUFFER_SIZE) != 0) {
				return true;
			}

			tx_to_add -= data.size();

			node.add_txs_to_mempool(std::move(data), tx_block_number);
			tx_block_number ++;
		}
		BLOCK_INFO("done adding txs, used up to block number %lu", tx_block_number);
		return false;
	}

	void wait_for_tx_addition() {
		std::unique_lock lock(mtx);
		if (!additional_tx_count) return;
		cv.wait(lock, [this] { return !additional_tx_count; });
	}

	void call_lazy_tx_addition(int64_t num_txs_to_add) {
		wait_for_tx_addition();
		std::unique_lock lock(mtx);
		additional_tx_count = num_txs_to_add;
		cv.notify_one();
	}

	bool is_done() {
		std::lock_guard lock(mtx);
		return experiment_done_flag;
	}
};

void run_experiment(ExperimentParameters params, std::string experiment_data_root, std::string results_output_root, EdceOptions& options, const size_t num_threads) {

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

	EdceNode node(management_structures, params, options, results_output_root, NodeType::BLOCK_PRODUCER);

	MempoolManager pool_manager(experiment_data_root, node);

	ConsensusApiServer consensus_api_server(node);

	consensus_api_server.set_experiment_ready_to_start();

	bool wait_for_control_server = false;
		
	if (wait_for_control_server) {
		consensus_api_server.wait_for_experiment_start();
	}


	const int64_t large_TARGET_MEMPOOL_SIZE = 2'000'000;
	const int64_t small_TARGET_MEMPOOL_SIZE = 200'000;
	
	const int64_t TARGET_MEMPOOL_SIZE = large_TARGET_MEMPOOL_SIZE;

	pool_manager.call_lazy_tx_addition(TARGET_MEMPOOL_SIZE);
	pool_manager.wait_for_tx_addition();
	node.push_mempool_buffer_to_mempool();

	while (!pool_manager.is_done()) {

		auto mempool_wait = init_time_measurement();
		pool_manager.wait_for_tx_addition();

		int64_t gap = TARGET_MEMPOOL_SIZE - node.mempool_size();
		if (gap > 0) {
			pool_manager.call_lazy_tx_addition(gap);
		}
		BLOCK_INFO("mempool lazy tx addition wait time: %lf", measure_time(mempool_wait));

		if (!node.produce_block()) {
			BLOCK_INFO("ending because mempool filled with garbage");
			break;
		}


		//auto [header, block] = node.produce_block();

		//node.log_block_confirmation(header.block.blockNumber);
	}
	node.write_measurements();
	BLOCK_INFO("experiment finished!");
	consensus_api_server.wait_until_block_buffer_empty();
	consensus_api_server.set_experiment_done();
	BLOCK_INFO("ok actually now finished sending all blocks to validators");
	node.get_connection_manager().shutdown_target_connections();


	if (wait_for_control_server) {
		consensus_api_server.wait_for_experiment_start(); // wait for signal from experiment controller
	}

	BLOCK_INFO("shutting down");
	std::this_thread::sleep_for(std::chrono::seconds(5));

}

int main(int argc, char const *argv[])
{
	if (argc != 4) {
		std::printf("usage: ./whatever <data_directory> <results_directory> <num_threads>\n");
		return -1;
	}

	EdceOptions options;

	ExperimentParameters params;

	std::string experiment_data_root = std::string(argv[1]) + "/";
	std::string results_output_root = std::string(argv[2]) + "/";

	std::string params_filename = experiment_data_root + "params";

	if (load_xdr_from_file(params, params_filename.c_str())) {
		throw std::runtime_error("failed to load experiment params file");
	}

	int num_threads = std::stoi(argv[3]);

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

	run_experiment(params, experiment_data_root, results_output_root, options, num_threads);
	return 0;
}
