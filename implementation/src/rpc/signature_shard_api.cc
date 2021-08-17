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
SignatureShardV1_server::init_shard(rpcsockptr* ip_addr, const SerializedAccountIDWithPK& account_with_pk, 
    const ExperimentParameters& params, uint16_t ip_idx,
    uint16_t num_assets, uint8_t tax_rate, uint8_t smooth_mult, uint64_t virt_shard_idx) {

    _ip_idx = ip_idx;
    std::vector<uint64_t> account_idxs;

    AccountIDWithPKList account_with_pk_list;
    xdr::xdr_from_opaque(account_with_pk, account_with_pk_list);

    for (size_t i = 0; i < account_with_pk_list.size(); i++) {
        uint64_t account_idx = _management_structures.db.add_account_to_db(account_with_pk_list[i].account, account_with_pk_list[i].pk);
        account_idxs.push_back(account_idx);
    }

    _management_structures.db.commit(0);

    _virtual_shards_mapping.insert({virt_shard_idx, account_idxs});

    std::cout << "SUCCESSFULLY LOADED ACCOUNTS " << std::endl;
    return std::make_unique<unsigned int>(0);
}



std::unique_ptr<unsigned int>
SignatureShardV1_server::check_block(rpcsockptr* ip_addr, const SerializedBlockWithPK& block_with_pk, 
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

    update_checker_ips();

    std::vector<std::string> signature_checker_ips_vec;

    signature_checker_ips_vec.insert(signature_checker_ips_vec.end(), _signature_checker_ips.begin(), 
        _signature_checker_ips.end());
    
    std::vector<SignedTransactionWithPKList> tx_with_pk_split_list;

    split_transaction_block(filtered_tx_with_pk_list, _signature_checker_ips.size(), tx_with_pk_split_list);

    size_t num_threads_lambda = num_threads;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, signature_checker_ips_vec.size()),
        [&](auto r) {
            for (size_t i = r.begin(); i != r.end(); i++) {
                SerializedBlockWithPK serialized_block_with_pk = xdr::xdr_to_opaque(tx_with_pk_split_list[i]);
                if (poll_node(signature_checker_ips_vec[i], serialized_block_with_pk, num_threads_lambda) == 1) {
                    throw std::runtime_error("sig checking failed!!!");
                }
            }
        });

    float res = measure_time(timestamp);
    std::cout << "Total time for check_all_signatures RPC call for this shard: " << res << std::endl;

    return std::make_unique<unsigned int>(0);

}

std::unique_ptr<unsigned int>
SignatureShardV1_server::init_checker(rpcsockptr* ip_addr)
{
    int fd = ip_addr->sock_ptr->ms_->get_sock().fd();

    struct sockaddr sa;
    socklen_t sval;
    sval = sizeof(sa);

    getpeername(fd, (struct sockaddr *)&sa, &sval);
    struct sockaddr_in *addr_in = (struct sockaddr_in *)&sa;
    char *ip = inet_ntoa(addr_in->sin_addr);

    _signature_checker_ips.insert(std::string(ip));

    std::cout << ip << std::endl;

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

    std::vector<SignedTransactionWithPK> tx_with_pks {tx_with_pk_list.size()};

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, tx_with_pk_list.size()),
        [&tx_with_pks, &tx_with_pk_list, this](auto r) {
            for (size_t i = r.begin(); i < r.end(); i++) {
                if (_management_structures.db.get_pk_nolock(tx_with_pk_list[i].signedTransaction.transaction.metadata.sourceAccount)) {
                    tx_with_pks[i] = tx_with_pk_list[i];
                }
            }
        });

    std::copy_if(tx_with_pks.begin(), tx_with_pks.end(), std::back_inserter(filtered_tx_with_pk_list),
        [](auto val) {return val.signedTransaction.transaction.metadata.sourceAccount != 0;});

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
SignatureShardV1_server::poll_node(const std::string& ip_addr, const SerializedBlockWithPK& block_with_pk, 
    const uint64_t& num_threads) {

    auto fd = xdr::tcp_connect(ip_addr.c_str(), SIGNATURE_CHECK_PORT);
    auto client = xdr::srpc_client<SignatureCheckV1>(fd.get());

    uint32_t return_value = *client.check_all_signatures(block_with_pk, num_threads);
    return return_value;
}


uint32_t
SignatureShardV1_server::check_heartbeat(const std::string& ip_addr) {
    try {
        auto fd = xdr::tcp_connect(ip_addr.c_str(), SIGNATURE_CHECK_PORT);
        auto client = xdr::srpc_client<SignatureCheckV1>(fd.get());
        uint32_t return_value = *client.heartbeat();
        std::cout << "ALIVE" << std::endl;
        return return_value;
    } catch (const std::system_error& e) {
        std::cout << "DEAD" << std::endl;
        _signature_checker_ips.erase(ip_addr);
        return 1;
    }
}

void
SignatureShardV1_server::update_checker_ips() {
    std::vector<std::string> signature_checker_ips_vec;
    signature_checker_ips_vec.insert(signature_checker_ips_vec.end(), _signature_checker_ips.begin(), 
        _signature_checker_ips.end());

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, signature_checker_ips_vec.size()),
        [&signature_checker_ips_vec, this](auto r) {
            for (size_t i = r.begin(); i < r.end(); i++) {
                check_heartbeat(signature_checker_ips_vec[i]);
            }
        });
}




}
