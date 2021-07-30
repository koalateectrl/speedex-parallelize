#include "xdr/signature_check_api.h"
#include "rpc/signature_shard_api.h"
#include <xdrpp/marshal.h>
#include <iostream>

#include "simple_debug.h"


#include "crypto_utils.h"
#include "utils.h"

#include "xdr/experiments.h"

#include "edce_management_structures.h"
#include "tbb/global_control.h"

#include <mutex>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace edce {

// rpc

std::unique_ptr<unsigned int> 
SignatureShardV1_server::init_shard(const SerializedAccountIDWithPK& account_with_pk, 
    const ExperimentParameters& params, uint16_t ip_idx, uint16_t checker_begin_idx, uint16_t checker_end_idx,
    uint16_t num_assets, uint8_t tax_rate, uint8_t smooth_mult) {

    _ip_idx = ip_idx;
    _checker_begin_idx = checker_begin_idx;
    _checker_end_idx = checker_end_idx;

    AccountIDWithPKList account_with_pk_list;
    xdr::xdr_from_opaque(account_with_pk, account_with_pk_list);

    for (size_t i = 0; i < account_with_pk_list.size(); i++) {
        _management_structures.db.add_account_to_db(account_with_pk_list[i].account, account_with_pk_list[i].pk);
    }

    _management_structures.db.commit(0);

    std::cout << "SUCCESSFULLY LOADED ACCOUNTS " << std::endl;
    return std::make_unique<unsigned int>(0);
}



std::unique_ptr<unsigned int>
SignatureShardV1_server::check_block(const SerializedBlockWithPK& block_with_pk, 
  const uint64_t& num_threads) {
    auto timestamp = init_time_measurement();

    SignedTransactionWithPKList tx_with_pk_list;

    xdr::xdr_from_opaque(block_with_pk, tx_with_pk_list);

    std::cout << "Total number of signatures in transaction block: " << tx_with_pk_list.size() << std::endl;

    auto filter_timestamp = init_time_measurement();

    SignedTransactionWithPKList filtered_tx_with_pk_list;

    filter_txs(tx_with_pk_list, filtered_tx_with_pk_list);

    std::cout << "Number of signatures this shard is responsible for: " << filtered_tx_with_pk_list.size() << std::endl;

    float filter_res = measure_time(filter_timestamp);
    std::cout << "Filtered signatures in " << filter_res << std::endl;

    size_t num_child_machines = _checker_end_idx - _checker_begin_idx;
    size_t checker_begin_idx = _checker_begin_idx;

    std::vector<SignedTransactionWithPKList> tx_with_pk_split_list;

    split_transaction_block(filtered_tx_with_pk_list, num_child_machines, tx_with_pk_split_list);

    size_t num_threads_lambda = num_threads;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, num_child_machines),
        [&](auto r) {
            for (size_t i = r.begin(); i != r.end(); i++) {
                SerializedBlockWithPK serialized_block_with_pk = xdr::xdr_to_opaque(tx_with_pk_split_list[i]);
                if (poll_node(checker_begin_idx + i, serialized_block_with_pk, num_threads_lambda) == 1) {
                    throw std::runtime_error("sig checking failed!!!");
                }
            }
        });

    float res = measure_time(timestamp);
    std::cout << "Total time for check_all_signatures RPC call for this shard: " << res << std::endl;

    return std::make_unique<unsigned int>(0);

}

// not rpc 
SignatureShardV1_server::SignatureShardV1_server()
    : _management_structures(EdceManagementStructures{20, ApproximationParameters{.tax_rate = 10, .smooth_mult = 10}}) {}

std::string SignatureShardV1_server::hostname_from_idx(int idx) {
    return std::string("10.10.1.") + std::to_string(idx);
}

void SignatureShardV1_server::filter_txs(const SignedTransactionWithPKList& tx_with_pk_list, 
    SignedTransactionWithPKList& filtered_tx_with_pk_list) {

    std::vector<SignedTransactionWithPK> tx_with_pks;

    std::mutex tx_with_pks_mutex;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, tx_with_pk_list.size()),
        [&tx_with_pks, &tx_with_pk_list, &tx_with_pks_mutex, this](auto r) {
            for (size_t i = r.begin(); i < r.end(); i++) {
                if (_management_structures.db.get_pk_nolock(tx_with_pk_list[i].signedTransaction.transaction.metadata.sourceAccount)) {
                    std::lock_guard<std::mutex> lock(tx_with_pks_mutex);
                    tx_with_pks.push_back(tx_with_pk_list[i]);
                }
            }
        });

    filtered_tx_with_pk_list.insert(filtered_tx_with_pk_list.end(), tx_with_pks.begin(), tx_with_pks.end());
}


void SignatureShardV1_server::split_transaction_block(const SignedTransactionWithPKList& orig_vec, 
    const size_t num_child_machines, std::vector<SignedTransactionWithPKList>& split_vec) {
    
    size_t length = orig_vec.size() / num_child_machines;
    size_t remain = orig_vec.size() % num_child_machines;
    size_t begin = 0;
    size_t end = 0;

    for (size_t i = 0; i < std::min(num_child_machines, orig_vec.size()); i++) {
        end += (remain > 0) ? (length + !!(remain--)) : length;
        split_vec.push_back(SignedTransactionWithPKList(orig_vec.begin() + begin, orig_vec.begin() + end));
        begin = end;
    }
}

uint32_t
SignatureShardV1_server::poll_node(int idx, const SerializedBlockWithPK& block_with_pk, 
    const uint64_t& num_threads) {

    print_local_ip();
    
    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_CHECK_PORT);
    auto client = xdr::srpc_client<SignatureCheckV1>(fd.get());

    uint32_t return_value = *client.check_all_signatures(block_with_pk, num_threads);
    return return_value;
}


uint32_t
SignatureShardV1_server::print_local_ip() {
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    sockaddr_in loopback;

    if (sock == -1) {
        std::cerr << "Could not socket\n";
        return 1;
    }

    std::memset(&loopback, 0, sizeof(loopback));
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = 1337;   // can be any IP address
    loopback.sin_port = htons(9);      // using debug port

    if (connect(sock, reinterpret_cast<sockaddr*>(&loopback), sizeof(loopback)) == -1) {
        close(sock);
        std::cerr << "Could not connect\n";
        return 1;
    }

    socklen_t addrlen = sizeof(loopback);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&loopback), &addrlen) == -1) {
        close(sock);
        std::cerr << "Could not getsockname\n";
        return 1;
    }

    close(sock);

    char buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &loopback.sin_addr, buf, INET_ADDRSTRLEN) == 0x0) {
        std::cerr << "Could not inet_ntop\n";
        return 1;
    } else {
        std::cout << "Local ip address: " << buf << "\n";
    }
}

}
