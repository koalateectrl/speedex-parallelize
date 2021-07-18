#pragma once

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>

#include "xdr/signature_shard_api.h"
#include "edce_node.h"

namespace edce {

class SignatureShardV1_server {

public:
  using rpc_interface_type = SignatureShardV1;

  SignatureShardV1_server() {};

  std::unique_ptr<unsigned int> init_shard(const SerializedAccountIDWithPK& account_with_pk, 
    const ExperimentParameters& params, 
    uint16_t num_assets, uint8_t tax_rate, uint8_t smooth_mult);

  std::unique_ptr<unsigned int> check_block(const SerializedBlockWithPK& block_with_pk, 
    const uint64& num_threads);

  void split_transaction_block(const SignedTransactionWithPKList& orig_vec, 
    const size_t num_child_machines, std::vector<SignedTransactionWithPKList>& split_vec);

};

}
