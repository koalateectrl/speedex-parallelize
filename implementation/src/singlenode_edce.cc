#include "edce.h"
#include "edce_options.h"
#include "rpc/transaction_submission_server.h"
#include "rpc/server_control_api.h"

#include <thread>
#include <xdrpp/server.h>
#include <xdrpp/srpc.h>
#include <xdrpp/pollset.h>


using namespace edce;

int main(int argc, char const *argv[])
{
	EdceOptions options;

	if (argc != 2) {
		throw std::runtime_error("need options yaml");
	}

	options.parse_options(argv[1]);

	Edce edce(options);

	std::thread([&edce] () {
		std::printf("Starting transaction submission server\n");
		TransactionSubmissionServer server(edce.get_sig_buffer_manager());
		server.run();
	}).detach();

	Price* price_workspace = new Price[options.num_assets];

	for (unsigned int i = 0; i < options.num_assets; i++) {
		price_workspace[i] = PriceUtils::from_double(1);
	}

	unsigned char* pk = new unsigned char[crypto_sign_PUBLICKEYBYTES];
	unsigned char* sk = new unsigned char[crypto_sign_SECRETKEYBYTES];

	crypto_sign_keypair(pk, sk);

	edce.initial_state_one_oligarch(pk);

	std::printf("Starting control server\n");
	xdr::pollset ps;
	ServerControlV1_server server(edce, price_workspace, sk);
	xdr::srpc_tcp_listener<> listener(ps, xdr::tcp_listen(SERVER_CONTROL_PORT, AF_INET), false, xdr::session_allocator<void>());
	listener.register_service(server);
	ps.run();
	return 0;
}