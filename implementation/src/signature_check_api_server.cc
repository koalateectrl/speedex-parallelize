#include "signature_check_api_server.h"
#include "rpc/signature_shard_api.h"

namespace edce {

SignatureCheckApiServer::SignatureCheckApiServer(std::string shard_ip)
    : ps()
    , signature_check_listener(ps, xdr::tcp_listen(SIGNATURE_CHECK_PORT, AF_INET), false, xdr::session_allocator<void>())
    , ip_of_shard(shard_ip) {
        signature_check_listener.register_service(signature_check_server);
        init_ping_shard(ip_of_shard);
        ps.run();
        //std::thread th([this] {ps.run();});
        //th.detach();
    }

void
SignatureCheckApiServer::init_ping_shard(std::string ip_of_shard)
{
    auto fd = xdr::tcp_connect(ip_of_shard.c_str(), SIGNATURE_SHARD_PORT);
    auto client = xdr::srpc_client<SignatureShardV1>(fd.get());

    if (*client.init_ping_shard() != 0) {
        throw std::runtime_error("Initial Pinging shard failed!!!");
    }
}


} /* edce */