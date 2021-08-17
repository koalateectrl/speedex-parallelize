#pragma once

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>

#include "xdr/signature_shard_api.h"
#include "edce_node.h"
#include "connection_info.h"

#include <set>


namespace edce {

class SignatureShardV1_server {
    int _ip_idx;
    EdceManagementStructures _management_structures;
    std::set<std::string> _signature_checker_ips;
    std::map<uint64_t, std::vector<uint64_t>> _virtual_shards_mapping;

public:
    using rpc_interface_type = SignatureShardV1;

    SignatureShardV1_server();

    std::unique_ptr<unsigned int> init_shard(rpcsockptr* ip_addr, const SerializedAccountIDWithPK& account_with_pk, 
        uint16_t ip_idx, uint16_t num_assets, uint8_t tax_rate, 
        uint8_t smooth_mult, uint64_t virt_shard_idx);

    std::unique_ptr<unsigned int> check_block(rpcsockptr* ip_addr, const SerializedBlockWithPK& block_with_pk, 
        const uint64& num_threads);

    std::unique_ptr<unsigned int> init_checker(rpcsockptr* ip_addr);

    std::unique_ptr<unsigned int> move_virt_shard(rpcsockptr* ip_addr, const ip_str& to_ip, 
        const uint64& virt_shard_num);

    // not rpc

    std::string hostname_from_idx(int idx);

    void filter_txs(const SignedTransactionWithPKList& tx_with_pk_list, 
        SignedTransactionWithPKList& filtered_tx_with_pk_list);

    void split_transaction_block(const SignedTransactionWithPKList& orig_vec, 
        const size_t num_child_machines, std::vector<SignedTransactionWithPKList>& split_vec);

    uint32_t poll_node(const std::string& ip_addr, const SerializedBlockWithPK& block_with_pk, 
        const uint64_t& num_threads);

    uint32_t check_heartbeat(const std::string& ip_addr);

    void update_checker_ips();

};

}
