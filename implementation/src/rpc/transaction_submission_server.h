#pragma once

#include "transaction_buffer_manager.h"
#include "rpc/transaction_submission_api.h"
#include "rpc/rpcconfig.h"

#include <xdrpp/server.h>
#include <xdrpp/srpc.h>
#include <xdrpp/pollset.h>

namespace edce {


class TransactionSubmissionServer {

	TransactionBufferManager& buffer_manager;

	//TODO figure out if multithreading this server is a good thing to do
	//Looking at xdrpp source (server.cc:98) it appears listeners cannot register more than one type of a service. (?)
	//the servers_ map is program -> (version -> (handler))

public:

	TransactionSubmissionServer(
		TransactionBufferManager& buffer_manager)
		: buffer_manager(buffer_manager) {}

	void run() {
		xdr::pollset ps;
		SubmitTransactionV1_server server(buffer_manager);
		xdr::srpc_tcp_listener<> listener(ps, xdr::tcp_listen(TRANSACTION_SUBMISSION_PORT, AF_INET), false, xdr::session_allocator<void>());
		listener.register_service(server);
		ps.run();
		throw std::runtime_error("transaction submission server ended");
	}

};

} /* edce */