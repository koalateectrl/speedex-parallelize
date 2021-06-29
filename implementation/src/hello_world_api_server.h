#pragma once

#include "edce_node.h"
#include "rpc/rpcconfig.h"
#include "rpc/hello_world_api.h"
#include "xdr/hello_world_api.h"

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>
#include <xdrpp/pollset.h>


namespace edce {

class MyProgApiServer {

    using MyProg = MyProgV1_server;

    MyProg myprog_server;

    xdr::pollset ps;

    xdr::srpc_tcp_listener<> myprog_listener;

public:

    MyProgApiServer();

};

} /* edce */