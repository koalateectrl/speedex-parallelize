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

  void print_hello_world();

  std::unique_ptr<unsigned int> init_shard(const SerializedAccountIDWithPK& account_with_pk, 
    uint16_t num_assets, uint8_t tax_rate, uint8_t smooth_mult);

};

}
