#pragma once

#include "merkle_trie.h"
#include "merkle_trie_utils.h"

#include "account_merkle_trie.h"

#include "xdr/database_commitments.h"
#include "threadlocal_cache.h"
#include "file_prealloc_worker.h"

#include "xdr/types.h"
#include "utils.h"

#include <cstdint>
#include <thread>

#include "../config.h"

namespace edce {

using AccountModificationTxListWrapper = XdrTypeWrapper<AccountModificationTxList>;


//might like to make these one struct, reduce extra code/etc, but type signatures on metadata merge are slightly diff
struct LogMergeFn {
	static void value_merge(AccountModificationTxList& original_value, const AccountModificationTxList& merge_in_value) {
		original_value.new_transactions_self.insert(
			original_value.new_transactions_self.end(),
			merge_in_value.new_transactions_self.begin(),
			merge_in_value.new_transactions_self.end());
		original_value.identifiers_self.insert(
			original_value.identifiers_self.end(),
			merge_in_value.identifiers_self.begin(),
			merge_in_value.identifiers_self.end());
		original_value.identifiers_others.insert(
			original_value.identifiers_others.end(),
			merge_in_value.identifiers_others.begin(),
			merge_in_value.identifiers_others.end());


		if (original_value.owner != merge_in_value.owner) {
			throw std::runtime_error("owner mismatch when merging logs!!!");
		}

/*		if (original_value.new_transactions_self.size() > original_value.new_transactions_self.capacity() || original_value.new_transactions_self.size() > 500000) {
			throw std::runtime_error("invalid main_value!!!");
		}
*/	}
	
	//The only metadata is trie size, and merging two nodes (each size 1) leaves a node (size 1) so metadata delta is 0.
	//TODO if we add additional metadata, this would need to be reworked.
	template<typename AtomicMetadataType>
	static typename AtomicMetadataType::BaseT 
	metadata_merge(AtomicMetadataType& main_metadata, const AtomicMetadataType& other_metadata) {
		return typename AtomicMetadataType::BaseT();
	}
};

struct LogNormalizeFn {

	static void apply_to_value (AccountModificationTxListWrapper& log);
};


template<typename TrieT>
class BackgroundDeleter {

	TrieT* trie_ptr;
	AccountModificationBlock* block_ptr;

	std::mutex mtx;
	std::condition_variable cv;

	bool end_flag = false;

	void do_deletions() {
		if (trie_ptr != nullptr) {
			delete trie_ptr;
			trie_ptr = nullptr;
		}
		if (block_ptr != nullptr) {
			delete block_ptr;
			block_ptr = nullptr;
		}
		cv.notify_one();
	}

public:

	void run() {
		while(true) {
			std::unique_lock lock(mtx);
			if (end_flag) return;
			cv.wait(lock, [this] (){ return (trie_ptr != nullptr) || (block_ptr != nullptr) || end_flag;});
			do_deletions();
		}
	}

	void terminate() {
		wait_for_delete();
		std::lock_guard lock(mtx);
		end_flag = true;
		cv.notify_one();
	}

	void call_delete(TrieT* t_ptr, AccountModificationBlock* b_ptr) {
		wait_for_delete();
		std::lock_guard lock(mtx);
	
		trie_ptr = t_ptr;
		block_ptr = b_ptr;
		cv.notify_one();
	}

	void wait_for_delete() {
		std::unique_lock lock(mtx);
		if (trie_ptr == nullptr && block_ptr == nullptr) {
			return;
		}

		cv.wait(lock, [this] {return trie_ptr == nullptr && block_ptr == nullptr;});
	}
};


class SerialAccountModificationLog;


class AccountModificationLog {
public:
	//constexpr static unsigned int LOG_KEY_LEN = sizeof(AccountID);

	using LogValueT = AccountModificationTxListWrapper;
	//using LogMetadataT = CombinedMetadata<SizeMixin>;
	//using TrieT = MerkleTrie<LOG_KEY_LEN, LogValueT, LogMetadataT>;
	using TrieT = AccountTrie<LogValueT>;
	using serial_trie_t = TrieT::serial_trie_t;

	//using FrozenTrieT = FrozenMerkleTrie<LOG_KEY_LEN, LogValueT, LogMetadataT>;
	using serial_cache_t = ThreadlocalCache<serial_trie_t>;
private:

	serial_cache_t cache;

	TrieT modification_log;
	//std::vector<AccountID> dirty_accounts;
	std::unique_ptr<AccountModificationBlock> persistable_block;
	//uint64_t num_txs_in_log;
	mutable std::shared_mutex mtx;
	FilePreallocWorker file_preallocator;
	BackgroundDeleter<int> deleter;

	constexpr static unsigned int BUF_SIZE = 5*1677716;
	unsigned char* write_buffer = new unsigned char[BUF_SIZE];
	friend class SerialAccountModificationLog;

public:

	AccountModificationLog() 
		: modification_log()
		//, dirty_accounts()
		, persistable_block(std::make_unique<AccountModificationBlock>())
		//, block_fd()
		//, num_txs_in_log(0)
		, mtx()
		, file_preallocator()
		, deleter() 
		{
			std::thread([this] () {
				deleter.run();
			}).detach();
	};

	AccountModificationLog(const AccountModificationLog& other) = delete;
	AccountModificationLog(AccountModificationLog&& other) = delete;

	~AccountModificationLog() {
		std::lock_guard lock(mtx);
		delete[] write_buffer;
		deleter.terminate();
	}

//	TrieT::iterator begin() {
//		return modification_log.begin();
//	}

	void sanity_check();

	void log_trie() {
		modification_log.log();
	}

	std::size_t size() const {
		std::shared_lock lock(mtx);
		return modification_log.size();
	}

	template<typename ApplyFn>
	void parallel_iterate_over_log(ApplyFn& fn) const {
		std::shared_lock lock(mtx);
		modification_log.parallel_batch_value_modify(fn);
	}

	//template<typename ApplyFn>
	//void parallel_iterate_over_log_threadlocal_acc(ApplyFn& fn) const {
//		modification_log.parallel_apply_threadlocal_acc(fn);
//	}

	template<typename VectorType>
	VectorType parallel_accumulate_values() const {
		VectorType out;
		parallel_accumulate_values(out);
		return out;
	}

	/*template<typename VectorType>
	VectorType coroutine_parallel_accumulate_values() const {
		VectorType out;
		std::shared_lock lock(mtx);
		modification_log.coroutine_accumulate_values_parallel(out);
		return out;
	}*/

	template<typename VectorType>
	void parallel_accumulate_values(VectorType& vec) const {
		std::shared_lock lock(mtx);
		modification_log.accumulate_values_parallel(vec);
	}

	//void merge_in_log_batch(std::vector<SerialAccountModificationLog>& local_logs);
	void merge_in_log_batch();

/*	void merge_in_log(serial_trie_t& local_log) {
		std::lock_guard lock(mtx);
		modification_log.template merge_in<LogMergeFn>(local_log);
		//num_txs_in_log += num_txs_in_new_log;
	}

	void _merge_in_log_nolock(serial_trie_t& local_log) {
		modification_log.template merge_in<LogMergeFn>(local_log);
		//num_txs_in_log += num_txs_in_new_log;
	}*/

	void freeze_and_hash(Hash& hash);

	//void clear() {
	//	modification_log.clear();
	//	dirty_accounts.clear();
	//}

	void detached_clear();

	void prepare_block_fd(uint64_t block_number) {
		file_preallocator.call_prealloc(block_number);

		//file_alloc_thread = std::thread([this, block_number] () {
		//	auto filename = log_name(block_number);
		//	std::printf("preallocating file for block %lu filename %s\n", block_number, filename.c_str());
		//	block_fd = preallocate_file(filename.c_str());
		//});
	}

	void cancel_prepare_block_fd() {
		file_preallocator.cancel_prealloc();
		//worker.wait_for_prealloc();
		//block_fd.clear();
/*
		if (file_alloc_thread.joinable()) {
			file_alloc_thread.join();
		}
		if (block_fd) {
			block_fd.clear();
		}*/
	}

	std::unique_ptr<AccountModificationBlock>
	persist_block(uint64_t block_number, bool return_block);

	//const std::vector<AccountID>& get_dirty_accounts();


	void diff_with_prev_log(uint64_t block_number);
};

class NullModificationLog : public AccountModificationLog {

public:
	using AccountModificationLog::TrieT;
	void merge_in_log( [[maybe_unused]] TrieT&& local_log) {
	}
};

class SerialAccountModificationLog {
	using serial_trie_t = AccountModificationLog::TrieT::serial_trie_t;
	using serial_cache_t = AccountModificationLog::serial_cache_t;

	serial_trie_t& modification_log;
	AccountModificationLog& main_log;
	//uint64_t num_txs_in_log;

public:
	SerialAccountModificationLog(const SerialAccountModificationLog& other) = delete;
	//SerialAccountModificationLog(SerialAccountModificationLog&& other) 
	//	: modification_log(other.modification_log.extract_root())
	//	, main_log(other.main_log)
	//	, num_txs_in_log(0) {}

	//SerialAccountModificationLog(AccountModificationLog& main_log) : modification_log(main_log.modification_log.open_serial_subsidiary()), main_log(main_log) {}

	SerialAccountModificationLog(AccountModificationLog& main_log) : modification_log(main_log.cache.get(main_log.modification_log)), main_log(main_log) {}
	SerialAccountModificationLog(AccountModificationLog& main_log, int idx) : modification_log(main_log.cache.get_index(idx, main_log.modification_log)), main_log(main_log) {}

	void log_self_modification(AccountID owner, uint64_t sequence_number);
	void log_other_modification(AccountID tx_owner, uint64_t sequence_number, AccountID modified_account);
	void log_new_self_transaction(const SignedTransaction& tx);

	//void finish();

	//void _finish_nolock();

	//uint64_t get_num_txs_in_log() {
	//	return num_txs_in_log;
	//}

	//careful with threadsafety, should be ok since trie is threadsafe
	//void merge_in_other_serial_log(SerialAccountModificationLog& other_log) {
	//	modification_log.template merge_in<LogMergeFn>(std::move(other_log.modification_log));
	//	other_log.modification_log.clear();
	//}

	//std::unique_ptr<AccountModificationLog::TrieT::TrieT> extract_root() {
	//	return modification_log.extract_root();
	//}

	std::size_t size() {
		return modification_log.size();
	}

	//void clear_and_reset() {
	//	modification_log.clear_and_reset();
	//}

	//void clear() {
	//	modification_log.clear();
	//}

	void sanity_check();
};

} /* edce */
