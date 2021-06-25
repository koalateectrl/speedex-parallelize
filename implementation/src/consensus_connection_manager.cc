#include "consensus_connection_manager.h"

#include "simple_debug.h"

namespace edce {

void 
BlockForwarder::send_block_(const HashedBlock& header, const SerializedBlock& serialized_data) {
	BLOCK_INFO("sending block number %lu to %lu clients", header.block.blockNumber, forwarding_targets.size());

	for (auto& client : forwarding_targets) {
		if (!client) {
			BLOCK_INFO("Lost connection to a client!!!");
		} else {
			client->send_block(header, serialized_data);//, [](xdr::call_result<void> r) {
			//	if (!r) {
			//		BLOCK_INFO("async call failed: msg = %s", r.message());
			//	}
			//});
		}
	}
	BLOCK_INFO("done sending block %lu", header.block.blockNumber);
}

void 
BlockForwarder::shutdown_target_connections() {
	wait_for_async_task();

	std::lock_guard lock(mtx);
	forwarding_targets.clear();
	sockets.clear();

	for (auto host : shutdown_notification_hosts) {
		auto fd = xdr::tcp_connect(host.c_str(), SERVER_CONTROL_PORT);
		if (fd) {
			auto client = xdr::srpc_client<ExperimentControlV1>(fd.get());
			client.signal_upstream_finish();
		}
	}
	num_forwarding_targets = 0;
}

void
ConnectionManager::shutdown_target_connections() {
	block_forwarder.shutdown_target_connections();

	wait_for_async_task();
	std::lock_guard lock(mtx);
	confirmation_targets.clear();
	sockets.clear();
}

void 
BlockForwarder::add_forwarding_target(const std::string& hostname) {
	std::lock_guard lock(mtx);
	num_forwarding_targets ++; 
	BLOCK_INFO("connecting to %s", hostname.c_str());

	auto fd = xdr::tcp_connect(hostname.c_str(), BLOCK_FORWARDING_PORT);

	auto client = std::make_unique<forwarding_client_t>(fd.get());
	
	forwarding_targets.emplace_back(std::move(client));
	sockets.emplace_back(std::move(fd));

	shutdown_notification_hosts.push_back(hostname);
	BLOCK_INFO("done connecting to forwarding target");
}

void 
BlockForwarder::run() {
	BLOCK_INFO("Starting block forwarder thread");
	while (true) {
		std::unique_lock lock(mtx);

		if ((!done_flag) && (!exists_work_to_do())) {
			cv.wait(lock, [this] () { return done_flag || exists_work_to_do();});
		}
		if (done_flag) return;
		if (block_to_send) {
			send_block_(header_to_send, *block_to_send);
			block_to_send = nullptr;
		}
		if (block_to_send2) {
			send_block_(header_to_send, *block_to_send2);
			block_to_send2 = nullptr;
		}
		if (block_to_send3) {
			send_block_(header_to_send, *block_to_send3);
			block_to_send3 = nullptr;
		}

		cv.notify_all();
	}
}

void 
ConnectionManager::run() {
	BLOCK_INFO("Starting log confirmation thread");
	while (true) {
		std::unique_lock lock(mtx);

		if ((!done_flag) && (!exists_work_to_do())) {
			cv.wait(lock, [this] () { return done_flag || exists_work_to_do();});
		}
		if (done_flag) return;
		if (confirmation_to_log) {
			log_confirmation_(*confirmation_to_log);
			confirmation_to_log = std::nullopt;
		}
		cv.notify_all();
	}
}



void 
ConnectionManager::log_confirmation_(const uint64_t block_number) {

	for (auto& client : confirmation_targets) {
		if (!client) {
			BLOCK_INFO("tried to confirm upstream, but upstream already turned off");
			continue;
		}
		try {
			client->ack_block(block_number);
		} catch(...) {
			BLOCK_INFO("error when confirming block %lu", block_number);
		}
	}
}


void 
ConnectionManager::add_log_confirmation_target(std::string target_hostname, std::string self_hostname) {
	std::lock_guard lock(mtx);

	//add log confirmation client (parent) to clients list
	auto fd = xdr::tcp_connect(target_hostname.c_str(), BLOCK_CONFIRMATION_PORT);

	auto client = std::make_unique<confirmation_client_t>(fd.get());
	
	confirmation_targets.emplace_back(std::move(client));

	sockets.emplace_back(std::move(fd));


	// request block forwarding from parent
	BLOCK_INFO("requesting block forwarding from %s", target_hostname.c_str());

	auto fd2 = xdr::tcp_connect(target_hostname.c_str(), FORWARDING_REQUEST_PORT);

	request_client_t req_client{fd2.get()};

	req_client.request_forwarding(self_hostname);
}


} /* namespace edce */