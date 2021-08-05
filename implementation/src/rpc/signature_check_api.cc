
#include "rpc/signature_check_api.h"
#include <xdrpp/marshal.h>
#include <iostream>

#include "simple_debug.h"


#include "crypto_utils.h"
#include "utils.h"

#include "xdr/experiments.h"

#include "edce_management_structures.h"
#include "tbb/global_control.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace edce {

//rpc

std::unique_ptr<unsigned int>
SignatureCheckV1_server::check_all_signatures(const SerializedBlockWithPK& block_with_pk, 
  const uint64& num_threads)
{

  auto timestamp = init_time_measurement();

  SamBlockSignatureChecker sam_checker;

  tbb::global_control control(
    tbb::global_control::max_allowed_parallelism, num_threads);

  if (!sam_checker.check_all_sigs(block_with_pk)) {
    return std::make_unique<unsigned int>(1);
  } 

  float res = measure_time(timestamp);
  std::cout << "Total time for check_all_signatures RPC call: " << res << std::endl;

  return std::make_unique<unsigned int>(0);

}

}
