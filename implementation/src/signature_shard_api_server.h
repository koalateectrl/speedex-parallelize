#pragma once

#include "edce_node.h"
#include "rpc/rpcconfig.h"
#include "rpc/signature_shard_api.h"
#include "xdr/signature_shard_api.h"

#include <xdrpp/arpc.h>
#include <xdrpp/srpc.h>
#include <xdrpp/pollset.h>

#include "connection_info.h"


namespace edce {

class SignatureShardApiServer {

    using SignatureShard = SignatureShardV1_server;

    SignatureShard signature_shard_server;

    xdr::pollset ps;

    xdr::srpc_tcp_listener<rpcsockptr> signature_shard_listener;

public:

    SignatureShardApiServer();
};

} /* edce */