
#include "rpc/hello_world_api.h"
#include <xdrpp/marshal.h>
#include <iostream>

#include "simple_debug.h"

namespace edce {

void
MyProgV1_server::print_hello_world()
{
  std::cout << "Hello World" << std::endl;
}

}
