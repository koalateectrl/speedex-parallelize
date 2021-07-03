
#include "rpc/signature_check_api.h"
#include <xdrpp/marshal.h>
#include <iostream>

#include "simple_debug.h"

#include "tbb/global_control.h"

namespace edce {

void
SignatureCheckV1_server::print_hello_world()
{
  std::cout << "Hello World" << std::endl;
}

std::unique_ptr<unsigned int>
SignatureCheckV1_server::check_all_signatures(const SerializedBlock& block, const uint64& num_threads)
{
  tbb::global_control control(
    tbb::global_control::max_allowed_parallelism, num_threads);

  return std::make_unique<unsigned int>(1);
}


}
