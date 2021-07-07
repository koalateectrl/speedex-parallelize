#pragma once

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>

#include "xdr/signature_check_api.h"
#include "edce_node.h"

namespace edce {

class SignatureCheckV1_server {
  std::atomic<bool> experiment_loaded = false;
  std::vector<PublicKey> pks;
  EdceManagementStructures management_structures;
  BlockSignatureChecker checker;

public:
  using rpc_interface_type = SignatureCheckV1;

  SignatureCheckV1_server();

  void print_hello_world();

  std::unique_ptr<unsigned int> check_all_signatures(const std::string& experiment_name, 
    const SerializedBlock& block, const uint64& num_threads);

  //not rpc
  bool is_experiment_loaded() {
    return experiment_loaded;
  }

  void load_experiment(const std::string& experiment_name);

};

}
