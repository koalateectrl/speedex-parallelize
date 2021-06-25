#pragma once

#include <xdrpp/arpc.h>
#include "xdr/consensus_api.h"
#include "rpc/rpcconfig.h"
#include "xdr/database_commitments.h"
#include "async_worker.h"

#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>

namespace edce {

class BlockForwarder : public AsyncWorker {
	using AsyncWorker::mtx;
	using AsyncWorker::cv;

	HashedBlock header_to_send;

	std::unique_ptr<AccountModificationBlock> block_to_send;
	std::unique_ptr<SerializedBlock> block_to_send2;
	std::unique_ptr<SignedTransactionList> block_to_send3;

	using forwarding_client_t = xdr::srpc_client<BlockTransferV1>;
	using socket_t = xdr::unique_sock;


	std::vector<std::unique_ptr<forwarding_client_t>> forwarding_targets;
	std::vector<socket_t> sockets;

	std::vector<std::string> shutdown_notification_hosts;

	std::atomic<size_t> num_forwarding_targets = 0;


	bool exists_work_to_do() override final {
		return (block_to_send != nullptr) 
			|| (block_to_send2 != nullptr)
			|| (block_to_send3 != nullptr);
	}

	void send_block_(const HashedBlock& header, const SerializedBlock& serialized_data);

	void send_block_(const HashedBlock& header, const SignedTransactionList& tx_list) {
		SerializedBlock serialized_block = xdr::xdr_to_opaque(tx_list);
		send_block_(header, serialized_block);
	}

	void send_block_(const HashedBlock& header, const AccountModificationBlock& block) {
		SignedTransactionList list;
		for (auto& log : block) {
			list.insert(list.end(), log.new_transactions_self.begin(), log.new_transactions_self.end());
		}
		send_block_(header, list);
	}

	void run();

public:


	BlockForwarder()
		: AsyncWorker() {
			start_async_thread([this] {run();});
		}

	~BlockForwarder() {
		wait_for_async_task();
		end_async_thread();
	}

	void send_block(const HashedBlock& header, std::unique_ptr<AccountModificationBlock> block) {
		wait_for_async_task();
		std::lock_guard lock(mtx);
		block_to_send = std::move(block);
		header_to_send = header;
		cv.notify_all();
	}

	void send_block(const HashedBlock& header, std::unique_ptr<SerializedBlock> block) {
		wait_for_async_task();
		std::lock_guard lock(mtx);
		block_to_send2 = std::move(block);
		header_to_send = header;
		cv.notify_all();
	}

	void send_block(const HashedBlock& header, std::unique_ptr<SignedTransactionList> block) {
		wait_for_async_task();
		std::lock_guard lock(mtx);
		block_to_send3 = std::move(block);
		header_to_send = header;
		cv.notify_all();
	}

	void shutdown_target_connections();

	void add_forwarding_target(const std::string& hostname);

	bool self_confirmable() const {
		return num_forwarding_targets == 0;
//		std::lock_guard lock(mtx);
//		return forwarding_targets.size() == 0;
	}
};

class ConnectionManager : public AsyncWorker {

	using AsyncWorker::mtx;
	using AsyncWorker::cv;

	using socket_t = xdr::unique_sock;
	using confirmation_client_t = xdr::srpc_client<BlockAcknowledgeV1>;
	using request_client_t = xdr::srpc_client<RequestBlockForwardingV1>;

	std::vector<std::unique_ptr<confirmation_client_t>> confirmation_targets;
	std::vector<socket_t> sockets;

	std::optional<uint64_t> confirmation_to_log;


	BlockForwarder block_forwarder;

	bool exists_work_to_do() override final {
		return (bool)(confirmation_to_log);
	}

	void log_confirmation_(const uint64_t block_number);

	void run();

public:
	
	ConnectionManager()
		: AsyncWorker() {
			start_async_thread([this] {run();});
		}

	~ConnectionManager() {
		wait_for_async_task();
		end_async_thread();
	}


	void log_confirmation(const uint64_t block_number) {
		wait_for_async_task();
		std::lock_guard lock(mtx);
		confirmation_to_log = block_number;
		cv.notify_all();
	}

	void add_log_confirmation_target(std::string target_hostname, std::string self_hostname);


	bool self_confirmable() const {
		return block_forwarder.self_confirmable();
	}


	template<typename serialize_format>
	void send_block(const HashedBlock& header, std::unique_ptr<serialize_format> block) {
		block_forwarder.send_block(header, std::move(block));
	}

	void shutdown_target_connections();

	void add_forwarding_target(const std::string& hostname) {
		block_forwarder.add_forwarding_target(hostname);
	}

};

} /* edce */
