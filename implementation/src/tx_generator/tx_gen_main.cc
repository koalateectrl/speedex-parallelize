#include "account_manager.h"

#include <xdrpp/srpc.h>
#include "../xdr/server_control_api.h"
#include "../xdr/transaction_submission_api.h"
#include "../rpc/rpcconfig.h"

#include "../edce_options.h"

using namespace txgen;

int main(int argc, char const *argv[])
{

	edce::EdceOptions options;

	if (argc != 2) {
		throw std::runtime_error("need options yaml");
	}

	options.parse_options(argv[1]);

	auto control_fd = xdr::tcp_connect("localhost", SERVER_CONTROL_PORT);
	auto submit_fd = xdr::tcp_connect("localhost", TRANSACTION_SUBMISSION_PORT);

	xdr::srpc_client<edce::ServerControlV1> control_c{control_fd.get()};
	xdr::srpc_client<edce::SubmitTransactionV1> submit_c{submit_fd.get()};

	edce::SigningKey global_root_sk = *control_c.get_root_sk();

	AccountManager manager(global_root_sk, 1, options.num_assets);

	uint64_t global_root_seqnum = 1;

	manager.init(submit_c, global_root_seqnum);

	//manager.create_accounts()










	return 0;
}