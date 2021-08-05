#include "signature_shard_api_server.h"

namespace edce {

SignatureShardApiServer::SignatureShardApiServer()
    : ps()
    , signature_shard_listener(ps, xdr::tcp_listen(SIGNATURE_SHARD_PORT, AF_INET), false, xdr::session_allocator<rpcsockptr>()) {
        signature_shard_listener.register_service(signature_shard_server);

        ps.run();
        //std::thread th([this] {ps.run();});
        //th.detach();
    }

} /* edce */