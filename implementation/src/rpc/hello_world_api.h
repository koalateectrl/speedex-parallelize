#pragma once

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>

#include "xdr/hello_world_api.h"
#include "edce_node.h"

namespace edce {

class MyProgV1_server {
public:
  using rpc_interface_type = MyProgV1;

  MyProgV1_server() : {};

  void print_hello_world();
};

}
