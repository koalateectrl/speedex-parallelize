#include "signature_shard_api_server.h"

namespace edce {

SignatureShardApiServer::SignatureShardApiServer()
    : ps()
    , signature_shard_listener(ps, xdr::tcp_listen(SIGNATURE_SHARD_PORT, AF_INET), false, xdr::session_allocator<void>()) {
        signature_shard_listener.register_service(signature_shard_server);

        ps.run();
        //std::thread th([this] {ps.run();});
        //th.detach();
    }


/*

void
ConsensusApiServer::add_to_pollset(xdr::pollset& ps) {

    BLOCK_INFO("running consensus_api_server");

    bt_listener = xdr::arpc_tcp_listener<> (ps, xdr::tcp_listen(BLOCK_FORWARDING_PORT, AF_INET), false, xdr::session_allocator<void>());

    ack_listener = xdr::arpc_tcp_listener<> (ps, xdr::tcp_listen(BLOCK_CONFIRMATION_PORT, AF_INET), false, xdr::session_allocator<void>());

    req_listener = xdr::srpc_tcp_listener<> (ps, xdr::tcp_listen(FORWARDING_REQUEST_PORT, AF_INET), false, xdr::session_allocator<void>());

    control_listener = xdr::srpc_tcp_listener<> (ps, xdr::tcp_listen(SERVER_CONTROL_PORT, AF_INET), false, xdr::session_allocator<void>());
    
    //ps.run();
    //throw std::runtime_error("pollset.run() should never terminate!");
}*/

} /* edce */