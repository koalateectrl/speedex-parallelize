#pragma once

#include "edce_node.h"
#include "rpc/rpcconfig.h"
#include "rpc/signature_check_api.h"
#include "xdr/signature_check_api.h"

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>
#include <xdrpp/pollset.h>

#include "connection_info.h"


namespace edce {

class SignatureCheckApiServer {

    using SignatureCheck = SignatureCheckV1_server;

    SignatureCheck signature_check_server;

    xdr::pollset ps;

    xdr::srpc_tcp_listener<ip_address_type> signature_check_listener;

public:

    SignatureCheckApiServer();

};

} /* edce */