#pragma once

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>

#include "xdr/signature_check_api.h"
#include "edce_node.h"

namespace edce {

class SignatureCheckV1_server {
public:
  using rpc_interface_type = SignatureCheckV1;

  SignatureCheckV1_server() {};

  void print_hello_world();

  std::unique_ptr<unsigned int> check_all_signatures(SerializedBlock& block);
};

}
