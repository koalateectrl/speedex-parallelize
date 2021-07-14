
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
SignatureShardV1_server::print_hello_world() {
  std::cout << "HELLO WORLD" << std::endl;
}

}
