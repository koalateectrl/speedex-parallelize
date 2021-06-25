#include "signature_check_api_server.h"

namespace edce {


SignatureCheckApiServer::SignatureCheckApiServer(EdceNode& main_node)
    : transfer_server(main_node)
    , ack_server(main_node)
    , req_server(main_node)
    , control_server(main_node)
    , sigcheck_server(main_node)
    , ps()
    , bt_listener(ps, xdr::tcp_listen(BLOCK_FORWARDING_PORT, AF_INET), false, xdr::session_allocator<void>())
    , ack_listener(ps, xdr::tcp_listen(BLOCK_CONFIRMATION_PORT, AF_INET), false, xdr::session_allocator<void>())
    , req_listener(ps, xdr::tcp_listen(FORWARDING_REQUEST_PORT, AF_INET), false, xdr::session_allocator<void>())
    , control_listener(ps, xdr::tcp_listen(SERVER_CONTROL_PORT, AF_INET), false, xdr::session_allocator<void>())
    , sigcheck_listener(ps, xdr::tcp_listen(SIGCHECK_PORT, AF_INET), false, xdr::session_allocator<void>()) {
        bt_listener.register_service(transfer_server);
        ack_listener.register_service(ack_server);
        req_listener.register_service(req_server);
        control_listener.register_service(control_server);
        sigcheck_listener.register_service(sigcheck_server);

        std::thread th([this] {ps.run();});
        th.detach();
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