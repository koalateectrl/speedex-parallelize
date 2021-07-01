
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

}
