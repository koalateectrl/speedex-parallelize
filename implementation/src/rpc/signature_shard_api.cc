
#include "rpc/signature_shard_api.h"
#include <xdrpp/marshal.h>
#include <iostream>

#include "simple_debug.h"


#include "crypto_utils.h"
#include "utils.h"

#include "xdr/experiments.h"

#include "edce_management_structures.h"
#include "tbb/global_control.h"

namespace edce {

//rpc
void SignatureShardV1_server::print_hello_world() {
  std::cout << "HELLO WORLD" << std::endl;
}

std::unique_ptr<unsigned int> 
SignatureShardV1_server::init_shard(const SerializedAccountIDWithPK& account_with_pk) {
  std::cout << "HELLO WORLD" << std::endl;
  return std::make_unique<unsigned int>(0);
}

}
