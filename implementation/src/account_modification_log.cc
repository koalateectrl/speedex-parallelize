#include "account_modification_log.h"

#include "price_utils.h"

namespace edce {

struct LogInsertFn : public GenericInsertFn {
	static void value_insert(AccountModificationTxList& main_value, const uint64_t self_sequence_number) {
		main_value.identifiers_self.push_back(self_sequence_number);
	}

	static void value_insert(AccountModificationTxList& main_value, const TxIdentifier& other_identifier) {
		main_value.identifiers_others.push_back(other_identifier);
	}

	static void value_insert(AccountModificationTxList& main_value, const SignedTransaction& self_transaction) {
		main_value.new_transactions_self.push_back(self_transaction);
		if (main_value.new_transactions_self.size() > main_value.new_transactions_self.capacity() || main_value.new_transactions_self.size() > 500000) {
			std::printf("%lu %lu %lu\n", main_value.new_transactions_self.size(), main_value.new_transactions_self.capacity(),
				main_value.new_transactions_self.size());
			throw std::runtime_error("invalid main_value!!!");
		}
	}

	//static void value_insert(AccountModificationTxList& main_value, const AccountModificationTxList& other_value) {
	//	LogMergeFn::value_merge(main_value, other_value);
	//}

	template<typename AtomicMetadataType, typename ValueType>
	static typename AtomicMetadataType::BaseT 
	metadata_insert(AtomicMetadataType& original_metadata, const ValueType& new_value) {
		//multiple insertions to one key doesn't change any metadata, since metadata is only size
		return typename AtomicMetadataType::BaseT();
	}

	template<typename OutType>
	static AccountModificationTxListWrapper new_value(const AccountIDPrefix /*const AccountModificationLog::TrieT::prefix_t&*/ prefix) {
		static_assert(std::is_same<OutType, AccountModificationTxListWrapper>::value, "invalid type invocation");
		AccountModificationTxListWrapper out;
		//PriceUtils::read_unsigned_big_endian(prefix, (out.owner));
		out.owner = prefix.get_account();
		return out;
	}
};


struct SanityCheckFn {
	void operator() (const AccountModificationLog::LogValueT& value) {
		if (value.owner > 100000000) {
			throw std::runtime_error("invalid owner\n");
		}
		if (value.new_transactions_self.size() > value.new_transactions_self.capacity()) {
			throw std::runtime_error("size() > capacity()!!!");
		}
		if (value.new_transactions_self.size() > 1000000) {
			throw std::runtime_error("new tx size() too big!");
		}
		if (value.identifiers_self.size() > 100000) {
			throw std::runtime_error("identifiers_self too big");
		}
	};
};


void
AccountModificationLog::sanity_check() {
	return;
	//SanityCheckFn fn{};
	//modification_log.apply(fn);
}

void 
SerialAccountModificationLog::sanity_check() {
	return;
	//SanityCheckFn fn{};
	//modification_log.apply(fn);
}

struct TxIdentifierCompareFn {
	bool operator() (const TxIdentifier& a, const TxIdentifier& b) {
		return (a.owner < b.owner) || ((a.owner == b.owner) && (a.sequence_number < b.sequence_number));
	}
};

struct NewSelfTransactionCompareFn {
	bool operator() (const SignedTransaction& a, const SignedTransaction& b) {
		return  (a.transaction.metadata.sequenceNumber < b.transaction.metadata.sequenceNumber);
	}
};

template<typename Value, typename CompareFn>
void dedup(std::vector<Value>& values, CompareFn comparator) {
	for (std::size_t i = 1; i < values.size(); i++) {
		if (comparator(values[i], values[i-1])) {
			values.erase(values.begin() + i);
		} else {
			i++;
		}
	}
}


void 
LogNormalizeFn::apply_to_value (AccountModificationTxListWrapper& log) {
	std::sort(log.identifiers_self.begin(), log.identifiers_self.end());
	std::sort(log.identifiers_others.begin(), log.identifiers_others.end(), TxIdentifierCompareFn());
	std::sort(log.new_transactions_self.begin(), log.new_transactions_self.end(), NewSelfTransactionCompareFn());

	//dedup
	for (std::size_t i = 1; i < log.identifiers_self.size(); i++) {
		if (log.identifiers_self[i] == log.identifiers_self[i-1]) {
			log.identifiers_self.erase(log.identifiers_self.begin() + i);
		} else {
			i++;
		}
	}

	auto tx_identifier_eq = [] (const TxIdentifier& a, const TxIdentifier& b) -> bool {
		return (a.owner == b.owner) && (a.sequence_number == b.sequence_number);
	};

	dedup(log.identifiers_others, tx_identifier_eq);

	auto transaction_eq = [] (const SignedTransaction& a, const SignedTransaction& b) -> bool {
		return (a.transaction.metadata.sequenceNumber == b.transaction.metadata.sequenceNumber);
	};

	dedup(log.new_transactions_self, transaction_eq);
}

void 
AccountModificationLog::freeze_and_hash(Hash& hash) {
	std::lock_guard lock(mtx);

	auto timestamp = init_time_measurement();

	modification_log.hash<LogNormalizeFn>(hash);

	float res = measure_time(timestamp);
	modification_log.sz_check();

	*persistable_block = modification_log.template accumulate_values_parallel<AccountModificationBlock>();

	float res2 = measure_time(timestamp);

	std::printf("acct log freeze_and_hash time: hash and normalize %lf accumulate block %lf\n", res, res2);

	INTEGRITY_CHECK_F(
		for (size_t i = 0; i < persistable_block->size(); i ++) {
			if ((*persistable_block)[i].owner == 0 && i != 0) {
				std::printf("i %lu owner not set!\n", i);
				throw std::runtime_error("owner not set during accumulate_values_parallel");
			}
		}
	);
}



void AccountModificationLog::merge_in_log_batch() {
	
	std::lock_guard lock(mtx);

	/*BLOCK_INFO("start merge_in_log_batch");

	for (size_t i = 0; i < local_logs.size(); i++) {

		local_logs[i].sanity_check();

		TrieT t = TrieT((local_logs[i].extract_root()));



		modification_log.merge_in<LogMergeFn>(std::move(t));

		//sanity_check();
	}
	sanity_check();

	BLOCK_INFO("end merge_in_log_batch");*/

	modification_log.template batch_merge_in<LogMergeFn>(cache);

	/*while (modification_log.size() == 0 && local_logs.size() > 0) {
		local_logs.back()._finish_nolock();
		local_logs.pop_back();
	}

	if (local_logs.size() == 0) {
		return;
	}

	
	std::vector<std::unique_ptr<TrieT::TrieT>> converted_logs;
	for (size_t i = 0; i < local_logs.size(); i++) {
		converted_logs.emplace_back(std::unique_ptr(local_logs[i].extract_root()));
		//num_txs_in_log += local_logs[i].get_num_txs_in_log();
	}

	modification_log.template batch_merge_in<LogMergeFn>(std::move(converted_logs));
	std::printf("post batch merge size %lu\n", num_txs_in_log);*/
	
	//check
//	for (size_t i = 0; i < local_logs.size(); i++) {
//		if (converted_logs[i]) {
//			throw std::runtime_error("local log wasn't fully consumed!");
//		}

//		}
}

void AccountModificationLog::detached_clear() {
	
	std::lock_guard lock(mtx);

	deleter.call_delete(/*modification_log.dump_contents_for_detached_deletion_and_clear()*/nullptr, persistable_block.release());

	persistable_block = std::make_unique<AccountModificationBlock>();

	modification_log.clear();
	cache.clear();
	//modification_log.clear();
	
	//auto* ptr = modification_log.dump_contents_for_detached_deletion_and_clear();
	//std::thread([ptr]{ delete ptr; }).detach();
	
	//dirty_accounts.clear();
	//persistable_block.clear();
}

std::unique_ptr<AccountModificationBlock>
AccountModificationLog::persist_block(uint64_t block_number, bool return_block) {
	std::lock_guard lock(mtx);
	//AccountModificationBlock vec = modification_log.template accumulate_values<AccountModificationBlock> ();
	std::printf("saving account log for block %lu\n", block_number);
	if (write_buffer == nullptr) {
		std::printf("write buffer was null!!!\n");
		throw std::runtime_error("null write buffer");
	}
	//save_xdr_to_file_fast(vec, log_name(block_number).c_str());

	//AccountModificationBlock vec = modification_log.template accumulate_values_parallel<AccountModificationBlock> ();

	if ((persistable_block->size() == 0) && (modification_log.size() > 0)) {
		std::printf("forming log in persist_block\n");
		*persistable_block = modification_log.template accumulate_values_parallel<AccountModificationBlock>();
	}

	auto& block_fd = file_preallocator.wait_for_prealloc();
	if (!block_fd) {
		throw std::runtime_error("block wasn't preallocated!!!");
	}

/*
	if (file_alloc_thread.joinable()) {
		BLOCK_INFO("waiting for file alloc thread to return");
		file_alloc_thread.join(); // sets block_fd
	} else {
		if (!block_fd) {
			BLOCK_INFO("file wasn't preallocated, preallocating now");
			auto filename = log_name(block_number);
			block_fd = preallocate_file(filename.c_str());
		}
	}*/

	BLOCK_INFO("block_fd = %d", block_fd.get());

	BLOCK_INFO("persist_block size: %lu\n", persistable_block->size());
	if (persistable_block->size() != modification_log.size()) {
		throw std::runtime_error("must be error in accumulate_values_parallel");
	}
	save_xdr_to_file_fast(*persistable_block, block_fd, write_buffer, BUF_SIZE);
	//save_account_block_fast(*persistable_block, block_fd, write_buffer, BUF_SIZE);
	BLOCK_INFO("done saving mod block");
	block_fd.clear();
//	num_txs_in_log = 0;

	if (return_block) {
		std::unique_ptr<AccountModificationBlock> out{persistable_block.release()};
		persistable_block = std::make_unique<AccountModificationBlock>();
		return out;
	}
	return nullptr;


//	std::thread th([](AccountModificationBlock&& block) {
//		AccountModificationBlock blk = std::move(block);
//		blk.clear();
//	}, std::move(persistable_block));
//	th.detach();
//	persistable_block.clear();
}


void SerialAccountModificationLog::log_self_modification(AccountID owner, uint64_t sequence_number) {
	//AccountModificationLog::TrieT::prefix_t key_buf;

//	unsigned char key_buf [AccountModificationLog::LOG_KEY_LEN];
	//PriceUtils::write_unsigned_big_endian(key_buf, owner);
	modification_log.template insert<LogInsertFn, uint64_t>(owner, sequence_number);

}
void SerialAccountModificationLog::log_other_modification(AccountID tx_owner, uint64_t sequence_number, AccountID modified_account) {
	//AccountModificationLog::TrieT::prefix_t key_buf;
	//PriceUtils::write_unsigned_big_endian(key_buf, modified_account);
	TxIdentifier value{tx_owner, sequence_number};
	modification_log.template insert<LogInsertFn, TxIdentifier>(modified_account, value);
}

void SerialAccountModificationLog::log_new_self_transaction(const SignedTransaction& tx) {
	//AccountModificationLog::TrieT::prefix_t key_buf;
	AccountID sender = tx.transaction.metadata.sourceAccount;
	//PriceUtils::write_unsigned_big_endian(key_buf, tx.transaction.metadata.sourceAccount);
	modification_log.template insert<LogInsertFn, SignedTransaction>(sender, tx);
	//num_txs_in_log++;
}

/*void SerialAccountModificationLog::finish() {
	main_log.merge_in_log(modification_log);
	//modification_log.clear();
//	num_txs_in_log = 0;
}

void SerialAccountModificationLog::_finish_nolock() {
	main_log._merge_in_log_nolock(modification_log);
	//modification_log.clear();
	//num_txs_in_log = 0;
}*/

/*
struct AccountList : public std::vector<AccountID> {

	void add_key(AccountModificationLog::TrieT::prefix_t& key) {
		AccountID account;
		PriceUtils::read_unsigned_big_endian(key, account);
		std::vector<AccountID>::push_back(account);
	}
};*/

/*const std::vector<AccountID>& 
AccountModificationLog::get_dirty_accounts() {
	if (dirty_accounts.empty()) {
		dirty_accounts = modification_log.template accumulate_keys<AccountList>();
	}
	return dirty_accounts;
}*/


void 
AccountModificationLog::diff_with_prev_log(uint64_t block_number) {
	AccountModificationBlock prev;

	auto filename = tx_block_name(block_number);
	if (load_xdr_from_file(prev, filename.c_str())) {
		throw std::runtime_error("couldn't load previous comparison data");
	}

	AccountModificationBlock current = modification_log.template accumulate_values_parallel<AccountModificationBlock>();

	std::printf("prev len %lu current len %lu\n", prev.size(), current.size());

	for (unsigned int i = 0; i < std::min(prev.size(), current.size()); i++) {
		AccountModificationTxListWrapper p = prev[i];
		AccountModificationTxListWrapper c = current[i];

		LogNormalizeFn::apply_to_value(p);
		LogNormalizeFn::apply_to_value(c);

		if (p.new_transactions_self.size() != c.new_transactions_self.size()) {
			std::printf("%u new_transactions_self.size() prev %lu current %lu\n", i, p.new_transactions_self.size(), c.new_transactions_self.size());
		} else {
			bool found_dif = false;

			for (unsigned int j = 0; j < p.new_transactions_self.size(); j++) {
				if (p.new_transactions_self[j].transaction.metadata.sequenceNumber != c.new_transactions_self[j].transaction.metadata.sequenceNumber) {
					found_dif = true;
				}
				if (p.new_transactions_self[j].transaction.metadata.sourceAccount != c.new_transactions_self[j].transaction.metadata.sourceAccount) {
					found_dif = true;
				}
			}
			if (found_dif) {
				for (unsigned int j = 0; j < p.new_transactions_self.size(); j++) {
					std::printf("new_transactions_self_seqnum %lu prev_seqnum %lu\n", p.new_transactions_self[j].transaction.metadata.sequenceNumber, c.new_transactions_self[j].transaction.metadata.sequenceNumber);
					std::printf("new_transactions_self owner %lu pref_wner %lu\n", p.new_transactions_self[j].transaction.metadata.sourceAccount, c.new_transactions_self[j].transaction.metadata.sourceAccount);
				}
			}
		}
		if (p.identifiers_self.size() != c.identifiers_self.size()) {
			std::printf("%u identifiers_self.size() prev %lu current %lu\n", i, p.identifiers_self.size(), c.identifiers_self.size());
		} else {

			bool found_dif = false;
			for (unsigned int j = 0; j < p.identifiers_self.size(); j++) {
				if (std::find(c.identifiers_self.begin(), c.identifiers_self.end(), p.identifiers_self[j]) == c.identifiers_self.end()) {
			//	if (p.identifiers_self[j] != c.identifiers_self[j]) {
					found_dif = true;
				}
			}
			if (found_dif) {
			std::printf("%u (account prev %lu / current %lu) different identifiers_self\n", i, p.owner, c.owner);
			for (unsigned int j = 0; j < p.identifiers_self.size(); j++) {
				//if (p.identifiers_self[j] != c.identifiers_self[j]) {
					std::printf("%u %u identifiers_self_seqnum %lu prev_seqnum %lu\n", i, j, p.identifiers_self[j], c.identifiers_self[j]);
				//}
			}
			}
		}
		if (p.identifiers_others.size() != c.identifiers_others.size()) {
			std::printf("%u identifiers_others.size() prev %lu current %lu\n", i, p.identifiers_others.size(), c.identifiers_others.size());
		}
	}
}

} /* edce */
