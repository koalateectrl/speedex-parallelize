#include "xdr/experiments.h"
#include "xdr/signature_check_api.h"
#include "rpc/rpcconfig.h"
#include <xdrpp/srpc.h>
#include <thread>
#include <chrono>

#include "utils.h"

#include <cstdint>
#include <vector>

#include "crypto_utils.h"
#include "utils.h"
#include "xdr/experiments.h"
#include "edce_management_structures.h"
#include "tbb/global_control.h"


using namespace edce;

std::string hostname_from_idx(int idx) {
    return std::string("10.10.1.") + std::to_string(idx);
}

uint32_t
poll_node(int idx, const std::string& experiment_name, 
    const SerializedBlock& block, const uint64& num_threads) {
    
    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_CHECK_PORT);
    auto client = xdr::srpc_client<SignatureCheckV1>(fd.get());

    // if works return 0 else if failed return 1
    uint32_t return_value = *client.check_all_signatures(experiment_name, block, num_threads);
    std::cout << return_value << std::endl;
    return return_value;
}


int main(int argc, char const *argv[]) {

    if (argc != 4) {
        std::printf("usage: ./signature_check_controller experiment_name block_number num_threads\n");
        return -1;
    }

    std::string experiment_root = std::string("experiment_data/") + std::string(argv[1]);

    ExperimentBlock block;

    std::string block_filename = experiment_root + std::string("/") + std::string(argv[2]) + std::string(".txs");

    if (load_xdr_from_file(block, block_filename.c_str())) {
        std::printf("%s\n", block_filename.c_str());
        throw std::runtime_error("failed to load tx block");
    }

    SignedTransactionList tx_list;

    tx_list.insert(tx_list.end(), block.begin(), block.end());

    SerializedBlock serialized_block = xdr::xdr_to_opaque(tx_list);
    
    size_t num_threads = std::stoi(argv[3]);

    auto timestamp = init_time_measurement();

    if (poll_node(2, std::string(argv[1]), serialized_block, num_threads) == 1) {
        throw std::runtime_error("sig checking failed!!!");
    }

    float res = measure_time(timestamp);

    std::printf("checked %lu sigs in %lf with max %lu threads\n", tx_list.size(), res, num_threads);

    return 0;

}




















