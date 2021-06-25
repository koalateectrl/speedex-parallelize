
#include <xdrpp/srpc.h>
#include "../xdr/server_control_api.h"
#include "../rpc/rpcconfig.h"

int main(int argc, char const *argv[])
{
	auto control_fd = xdr::tcp_connect("localhost", SERVER_CONTROL_PORT);
	xdr::srpc_client<edce::ServerControlV1> control_c{control_fd.get()};

	control_c.generate_block();

	return 0;
}