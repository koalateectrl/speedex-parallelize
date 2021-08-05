#pragma once

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>

#include "xdr/signature_shard_api.h"
#include "edce_node.h"
#include "connection_info.h"


namespace edce {

class SignatureShardV1_server {
    int _ip_idx;
    int _checker_begin_idx;
    int _checker_end_idx;
    EdceManagementStructures _management_structures;

public:
    using rpc_interface_type = SignatureShardV1;

    SignatureShardV1_server();

    std::unique_ptr<unsigned int> init_shard(const SerializedAccountIDWithPK& account_with_pk, 
        const ExperimentParameters& params, uint16_t ip_idx,
        uint16_t checker_begin_idx, 
        uint16_t checker_end_idx,
        uint16_t num_assets, uint8_t tax_rate, uint8_t smooth_mult);

    std::unique_ptr<unsigned int> check_block(const SerializedBlockWithPK& block_with_pk, 
        const uint64& num_threads);

    // not rpc

    std::string hostname_from_idx(int idx);

    void filter_txs(const SignedTransactionWithPKList& tx_with_pk_list, 
        SignedTransactionWithPKList& filtered_tx_with_pk_list);

    void split_transaction_block(const SignedTransactionWithPKList& orig_vec, 
        const size_t num_child_machines, std::vector<SignedTransactionWithPKList>& split_vec);

    uint32_t poll_node(int idx, const SerializedBlockWithPK& block_with_pk, 
        const uint64_t& num_threads);

};


class SignatureCheckerConnectV1_server {
    std::vector<std::string> signature_checker_ips;

public:
    using rpc_interface_type = SignatureCheckerConnectV1;

    SignatureCheckerConnectV1_server() {};

    void init_ping_shard(rpcsockptr* ip_addr);
};

}
