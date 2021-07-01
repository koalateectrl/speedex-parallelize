#pragma once

#include "edce_node.h"
#include "rpc/rpcconfig.h"
#include "rpc/signature_check_api.h"
#include "xdr/signature_check_api.h"

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>
#include <xdrpp/pollset.h>


namespace edce {

class SignatureCheckApiServer {

    using SignatureCheck = SignatureCheckV1_server;

    SignatureCheck signaturecheck_server;

    xdr::pollset ps;

    xdr::srpc_tcp_listener<> signaturecheck_listener;

public:

    SignatureCheckApiServer();

};

} /* edce */