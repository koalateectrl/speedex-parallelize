#include "signature_shard_api_server.h"

namespace edce {

SignatureShardApiServer::SignatureShardApiServer()
    : ps()
    , signature_shard_listener(ps, xdr::tcp_listen(SIGNATURE_SHARD_PORT, AF_INET), false, xdr::session_allocator<rpcsockptr>())
    //, signature_checker_connect_listener(ps, xdr::tcp_listen(SIGNATURE_CHECK_PORT, AF_INET), false, xdr::session_allocator<rpcsockptr>()) {
        {signature_shard_listener.register_service(signature_shard_server);
        //signature_checker_connect_listener.register_service(signature_checker_connect_server);

        ps.run();
        //std::thread th([this] {ps.run();});
        //th.detach();
    }

} /* edce */