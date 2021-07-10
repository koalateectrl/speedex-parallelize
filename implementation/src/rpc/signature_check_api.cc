
#include "rpc/signature_check_api.h"
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

std::unique_ptr<unsigned int>
SignatureCheckV1_server::check_all_signatures(const std::string& experiment_name, 
  const SerializedBlockWithPK& block_with_pk, const uint64& num_threads)
{

  SamBlockSignatureChecker sam_checker;

  tbb::global_control control(
    tbb::global_control::max_allowed_parallelism, num_threads);

  if (!sam_checker.check_all_sigs(block_with_pk)) {
    return std::make_unique<unsigned int>(1);
  } 

  return std::make_unique<unsigned int>(0);

}
