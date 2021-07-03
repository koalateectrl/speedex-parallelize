
#include "rpc/signature_check_api.h"
#include <xdrpp/marshal.h>
#include <iostream>

#include "simple_debug.h"

namespace edce {

void
SignatureCheckV1_server::print_hello_world()
{
  std::cout << "Hello World" << std::endl;
}

std::unique_ptr<unsigned int>
SignatureCheckV1_server::check_all_signatures(const SerializedBlock& block)
{
  return std::make_unique<unsigned int>(1);
}


}
