#include "memory_database.h"

#include "simple_debug.h"
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include "price_utils.h"

#include "lmdb_wrapper.h"

#include <atomic>

namespace edce {

UserAccount& MemoryDatabase::find_account(account_db_idx user_index) {
	uint64_t db_size = database.size();
	if (user_index >= db_size) {
		std::printf("invalid db access: %lu of %lu\n", user_index, db_size);
		throw std::runtime_error("invalid db idx access");
	}
	return database[user_index];
}

void MemoryDatabase::transfer_available(
	account_db_idx user_index, AssetID asset_type, int64_t change) {
	find_account(user_index).transfer_available(asset_type, change);
}
void MemoryDatabase::transfer_escrow(
	account_db_idx user_index, AssetID asset_type, int64_t change) {
	find_account(user_index).transfer_escrow(asset_type, change);
}
void MemoryDatabase::escrow(
	account_db_idx user_index, AssetID asset_type, int64_t change) {
	find_account(user_index).escrow(asset_type, change);
}

bool MemoryDatabase::conditional_transfer_available(
	account_db_idx user_index, AssetID asset_type, int64_t change) {
	return find_account(user_index).conditional_transfer_available(asset_type, change);
}

bool MemoryDatabase::conditional_escrow(
	account_db_idx user_index, AssetID asset_type, int64_t change) {
	return find_account(user_index).conditional_escrow(asset_type, change);
}

TransactionProcessingStatus MemoryDatabase::reserve_sequence_number(
	account_db_idx user_index, uint64_t sequence_number) {
	return find_account(user_index).reserve_sequence_number(sequence_number);
}

void MemoryDatabase::release_sequence_number(
	account_db_idx user_index, uint64_t sequence_number) {
	find_account(user_index).release_sequence_number(sequence_number);
}

void MemoryDatabase::commit_sequence_number(
	account_db_idx user_index, uint64_t sequence_number) {
	find_account(user_index).commit_sequence_number(sequence_number);
}


//TODO this is not used in normal block processing, but seems generally useful
account_db_idx MemoryDatabase::add_account_to_db(AccountID user_id, const PublicKey pk) {
	auto idx_itr = user_id_to_idx_map.find(user_id);
	if (idx_itr != user_id_to_idx_map.end()) {
		return idx_itr -> second;
	}

	std::lock_guard lock(uncommitted_mtx);
	idx_itr = uncommitted_idx_map.find(user_id);
	if (idx_itr != uncommitted_idx_map.end()) {
		return idx_itr -> second;
	}
	auto idx = database.size() + uncommitted_db.size();
	uncommitted_idx_map[user_id] = idx;
	uncommitted_db.emplace_back(user_id, pk);
	return idx;
}

int64_t MemoryDatabase::lookup_available_balance(
	account_db_idx user_index, AssetID asset_type) {
	return find_account(user_index).lookup_available_balance(asset_type);
}

void MemoryDatabase::clear_internal_data_structures() {
	uncommitted_db.clear();
	uncommitted_idx_map.clear();
	reserved_account_ids.clear();
}

struct CommitValueLambda {
	MemoryDatabase& db;

	template<typename Applyable>
	void operator() (const Applyable& work_root) {

		auto lambda = [this] (const AccountModificationTxList& commitment) {
			AccountID owner = commitment.owner;
			account_db_idx idx;
			if (db.lookup_user_id(owner, &idx)) {
				db._commit_value(idx);
			} else {
				//was a new value
				std::printf("couldn't lookup possibly new acct %lu\n", owner);
			}
		};

		work_root . apply(lambda);
	}
};

void MemoryDatabase::commit_values(const AccountModificationLog& dirty_accounts) {	
	std::lock_guard lock(committed_mtx);
	CommitValueLambda lambda{*this};
	dirty_accounts.parallel_iterate_over_log(lambda);
}

void MemoryDatabase::commit_values() {
	std::lock_guard lock(committed_mtx);

	int db_size = database.size();

	//std::atomic_thread_fence(std::memory_order_release);
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, db_size, 10000),
		[this] (auto r) {
			//std::atomic_thread_fence(std::memory_order_acquire);
			for (auto  i = r.begin(); i < r.end(); i++) {
				database[i].commit();
			}
			//std::atomic_thread_fence(std::memory_order_release);
		});
	//std::atomic_thread_fence(std::memory_order_acquire);
}
void MemoryDatabase::rollback_values() {
	std::lock_guard lock(committed_mtx);
	int db_size = database.size();
	
	//std::atomic_thread_fence(std::memory_order_release);
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, db_size, 10000),
		[this] (auto r) {
			//std::atomic_thread_fence(std::memory_order_acquire);
			for (auto  i = r.begin(); i < r.end(); i++) {
				database[i].rollback();
			}
		//	std::atomic_thread_fence(std::memory_order_release);
		});
	//std::atomic_thread_fence(std::memory_order_acquire);

}

void MemoryDatabase::commit_new_accounts(uint64_t current_block_number) {
	std::lock_guard lock1(db_thunks_mtx);
	std::lock_guard lock2(committed_mtx);
	std::lock_guard lock3(uncommitted_mtx);

	if ((account_creation_thunks.size() == 0 && account_lmdb_instance.get_persisted_round_number() + 1 != current_block_number)
		|| (account_creation_thunks.size() > 0 && account_creation_thunks.back().current_block_number + 1 != current_block_number)) {

		if (!(current_block_number == 0 && account_lmdb_instance.get_persisted_round_number() ==0)) {
			BLOCK_INFO("mismatch: current_block_number = %lu account_lmdb_instance.get_persisted_round_number() = %lu",
					current_block_number, account_lmdb_instance.get_persisted_round_number());
			if (account_creation_thunks.size()) {
				BLOCK_INFO("account_creation_thunks.back().current_block_number:%lu",
						account_creation_thunks.back().current_block_number);
			}
			BLOCK_INFO("uncommitted db size: %lu", uncommitted_db.size());
			std::fflush(stdout);
			throw std::runtime_error("account creation thunks block number error");
		}
	}

	auto uncommitted_db_size = uncommitted_db.size();
	database.reserve(database.size() + uncommitted_db_size);
	for (uint64_t i = 0; i < uncommitted_db_size; i++) {
		uncommitted_db[i].commit();
		database.emplace_back(std::move(uncommitted_db[i]));

		AccountID owner = database.back().get_owner();

		DBStateCommitmentTrie::prefix_t key_buf;
		MemoryDatabase::write_trie_key(key_buf, owner);
		//database.back().commit();
		commitment_trie.insert(key_buf, DBStateCommitmentValueT(database.back().produce_commitment()));
	}
	user_id_to_idx_map.insert(uncommitted_idx_map.begin(), uncommitted_idx_map.end());

	account_creation_thunks.push_back(AccountCreationThunk{current_block_number, uncommitted_db_size});
	clear_internal_data_structures();
}

void MemoryDatabase::rollback_new_accounts(uint64_t current_block_number) {
	std::lock_guard lock1(db_thunks_mtx);
	std::lock_guard lock2(committed_mtx);
	std::lock_guard lock3(uncommitted_mtx);
	rollback_new_accounts_(current_block_number);
}


void MemoryDatabase::rollback_new_accounts_(uint64_t current_block_number) {
	for (size_t i = 0; i < account_creation_thunks.size();) {
		auto& thunk = account_creation_thunks[i];
		if (thunk.current_block_number > current_block_number) {
			size_t db_size = database.size();
			for (auto idx = db_size - thunk.num_accounts_created; idx < db_size; idx++) {

				auto owner = database.at(idx).get_owner();
				user_id_to_idx_map.erase(owner);
				
				DBStateCommitmentTrie::prefix_t key_buf;
				MemoryDatabase::write_trie_key(key_buf, owner);
				commitment_trie.perform_deletion(key_buf);
			}

			database.erase(database.begin() + (db_size - thunk.num_accounts_created), database.end());

			account_creation_thunks.erase(account_creation_thunks.begin() + i);
		} else {
			i++;
		}
	}
	clear_internal_data_structures();
}

/*
void MemoryDatabase::_rollback() {
	int db_size = database.size();

	for (int i = 0; i < db_size; i++) {
		database[i].rollback();
	}
	clear_internal_data_structures();
}
*/

/*
void MemoryDatabase::rollback_for_validation(uint64_t num_accounts_to_remove) {
	std::lock_guard lock(committed_mtx);
	std::lock_guard lock2(uncommitted_mtx);

	auto db_size = database.size();

	for (auto i = db_size - num_accounts_to_remove; i < db_size; i++) {

		auto owner = database.at(i).get_owner();
		user_id_to_idx_map.erase(owner);

		//unsigned char key_buf[TRIE_KEYLEN];
		
		DBStateCommitmentTrie::prefix_t key_buf;
		MemoryDatabase::write_trie_key(key_buf, owner);
		commitment_trie.perform_deletion(key_buf);
	}

	database.erase(database.begin() + (db_size - num_accounts_to_remove), database.end());

	_rollback();
}

uint64_t MemoryDatabase::tentative_commit_for_validation() {
	std::lock_guard lock(committed_mtx);
	std::lock_guard lock2(uncommitted_mtx);
	
	auto uncommitted_db_size = uncommitted_db.size();
	database.reserve(database.size() + uncommitted_db_size);
	for (uint64_t i = 0; i < uncommitted_db_size; i++) {
		database.emplace_back(std::move(uncommitted_db[i]));
	}

	user_id_to_idx_map.insert(uncommitted_idx_map.begin(), uncommitted_idx_map.end());

	clear_internal_data_structures();

	return uncommitted_db_size;
}*/

struct ValidateStateReduce {
	std::vector<MemoryDatabase::DBEntryT>& database;
	bool valid;

	void operator() (const tbb::blocked_range<std::size_t>& r) {

		//std::atomic_thread_fence(std::memory_order_acquire);
		bool local_valid = true;
		for (auto i = r.begin(); i < r.end(); i++) {
			auto result = database[i].in_valid_state();
			if (!result) {
				local_valid = false;
				return;
			}
		}
		valid = local_valid && valid;
	//	std::atomic_thread_fence(std::memory_order_release);
	}

	ValidateStateReduce(ValidateStateReduce& x, tbb::split) : database(x.database), valid(x.valid) {}

	void join(ValidateStateReduce& x) {
		valid = valid && x.valid;
	}

	ValidateStateReduce(std::vector<MemoryDatabase::DBEntryT>& database) : database(database), valid(true) {}
};

struct ValidityCheckLambda {
	MemoryDatabase& db;
	std::atomic_flag& error_found;
	template<typename Applyable>
	void operator() (const Applyable& work_root) {

		auto lambda = [this] (const AccountModificationTxList& commitment) {
			AccountID owner = commitment.owner;
			account_db_idx idx;
			db.lookup_user_id(owner, &idx);
			if (!db._check_valid(idx)) {
				error_found.test_and_set();
			}
		};

		work_root . apply(lambda);
	}
};

bool MemoryDatabase::check_valid_state(const AccountModificationLog& dirty_accounts) {
	std::shared_lock lock(committed_mtx);
	std::shared_lock lock2(uncommitted_mtx);

	std::atomic_flag error_found = ATOMIC_FLAG_INIT;

	ValidityCheckLambda lambda{*this, error_found};

	dirty_accounts.parallel_iterate_over_log(lambda);

	if (error_found.test_and_set()) {
		return false;
	}

	size_t uncommitted_db_size = uncommitted_db.size();
	for (size_t i = 0; i < uncommitted_db_size; i++) {
		if (!uncommitted_db[i].in_valid_state()) {
			return false;
		}
	}
	return true;
}

//checks uncommitted state as well as committed.
//The idea is you call this before committing.
bool MemoryDatabase::check_valid_state() {
	std::shared_lock lock(committed_mtx);
	std::shared_lock lock2(uncommitted_mtx);
	auto db_size = database.size();

	ValidateStateReduce validator(database);

//	std::atomic_thread_fence(std::memory_order_release);
	tbb::parallel_reduce(tbb::blocked_range<std::size_t>(0, db_size), validator);
//	std::atomic_thread_fence(std::memory_order_acquire);

	if (!validator.valid) {
		return false;
	}
/*	for (int i = 0; i < db_size; i++) {
		if (!database[i].in_valid_state()) {
			return false;
		}
	}*/

	
	int uncommitted_db_size = uncommitted_db.size();
	for (int i = 0; i < uncommitted_db_size; i++) {
		if (!uncommitted_db[i].in_valid_state()) {
			return false;
		}
	}
	return true;
}

bool MemoryDatabase::account_exists(AccountID account) {
	return user_id_to_idx_map.find(account) != user_id_to_idx_map.end();
}

//returns index of user id.
bool MemoryDatabase::lookup_user_id(AccountID account, uint64_t* index_out) const {
	INFO("MemoryDatabase::lookup_user_id on account %ld", account);

	auto idx_itr = user_id_to_idx_map.find(account);

	if (idx_itr != user_id_to_idx_map.end()) {
		*index_out = idx_itr -> second;
		return true;
	}
	//INFO("not found, remaining is:");
	//for (auto iter = user_id_to_idx_map.begin(); iter != user_id_to_idx_map.end(); iter++) {
	//	INFO("account id:%d %d", iter->first, iter->second);
	//}
/*	std::shared_lock<std::shared_mutex> lock(uncommitted_mtx);
	idx_itr = uncommitted_map.find(account);
	if (idx_itr != uncommitted_map.end()) {
		*index_out = idx_itr -> second;
		return true;
	}*/
	return false;
}

TransactionProcessingStatus MemoryDatabase::reserve_account_creation(const AccountID account) {
	if (user_id_to_idx_map.find(account) != user_id_to_idx_map.end()) {
		return TransactionProcessingStatus::NEW_ACCOUNT_ALREADY_EXISTS;
	}
	std::lock_guard<std::shared_mutex> lock(uncommitted_mtx);
	if (reserved_account_ids.find(account) != reserved_account_ids.end()) {
		return TransactionProcessingStatus::NEW_ACCOUNT_TEMP_RESERVED;
	}
	reserved_account_ids.insert(account);
	return TransactionProcessingStatus::SUCCESS;
}

void MemoryDatabase::release_account_creation(const AccountID account) {
	std::lock_guard lock(uncommitted_mtx);
	reserved_account_ids.erase(account);
}

void MemoryDatabase::commit_account_creation(const AccountID account_id, DBEntryT&& account_data) {
	std::lock_guard lock(uncommitted_mtx);
	account_db_idx new_idx = uncommitted_db.size() + database.size();
	uncommitted_idx_map.emplace(account_id, new_idx);
	uncommitted_db.push_back(std::move(account_data));
	//uncommitted_trie_entries.push_back(
	//	std::make_pair(account_id, UserAccountWrapper(this, new_idx)));
}

std::optional<PublicKey> MemoryDatabase::get_pk(AccountID account) const {
	std::shared_lock lock(committed_mtx);
	return get_pk_nolock(account);
}

std::optional<PublicKey> MemoryDatabase::get_pk_nolock(AccountID account) const {
	//std::shared_lock lock(committed_mtx);
	auto iter = user_id_to_idx_map.find(account);
	if (iter == user_id_to_idx_map.end()) {
		return std::nullopt;
	}
	return database[iter->second].get_pk();
}

/*
void
MemoryDatabase::tentative_produce_state_commitment(Hash& hash, const std::vector<AccountID>& dirty_accounts) {
	std::shared_lock lock(committed_mtx);

	const auto dirty_accounts_sz = dirty_accounts.size();
	const size_t num_blocks = 40;
	const auto block_size = (dirty_accounts_sz/num_blocks) + 1;
	//std::printf("dirty accounts size:%lu\n num_blocks:%lu\n", dirty_accounts_sz, num_blocks);
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_blocks),
		[this, &dirty_accounts, &block_size, &dirty_accounts_sz](auto r) {
			DBStateCommitmentTrie::prefix_t key_buf;
			//unsigned char key_buf[TRIE_KEYLEN];
			//One thing to try would be to use parallel insert but still with the blocks.
			//std::printf("begin %lu end %lu\n", r.begin(), r.end());
			for (auto i = r.begin(); i < r.end(); i++) {
				DBStateCommitmentTrie trie;

				auto start_idx = i * block_size;
				auto end_idx = std::min((i+1)*block_size, dirty_accounts_sz);
				for (auto idx = start_idx; idx < end_idx; idx++) {
					MemoryDatabase::write_trie_key(key_buf, dirty_accounts[idx]);
					trie.insert(key_buf, DBStateCommitmentValueT(database.at(user_id_to_idx_map.at(dirty_accounts[idx])).tentative_commitment()));
				}
				commitment_trie.parallel_merge_in(std::move(trie));
			}
		});
	commitment_trie.freeze_and_hash(hash);
}*/

/*
void
MemoryDatabase::tentative_produce_state_commitment(Hash& hash, const std::vector<AccountID>& dirty_accounts) {
	std::shared_lock lock(committed_mtx);
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, dirty_accounts.size()),
		[this, &dirty_accounts](auto r) {
			unsigned char key_buf[TRIE_KEYLEN];
			for (auto i = r.begin(); i < r.end(); i++) {
				MemoryDatabase::write_trie_key(key_buf, dirty_accounts[i]);
				commitment_trie.parallel_insert(key_buf, DBStateCommitmentValueT(database[user_id_to_idx_map[dirty_accounts[i]]].tentative_commitment()));
			}
		});	

	BLOCK_INFO("tentative state modified count = %lu\n", dirty_accounts.size());

	commitment_trie.freeze_and_hash(hash.data());

	INFO_F(commitment_trie._log("tent. db "));
	INFO_F(log());
}
*/
//rollback_for_validation should be called in advance of this
void 
MemoryDatabase::rollback_produce_state_commitment(const AccountModificationLog& log) {
	std::lock_guard lock(committed_mtx);
	/*tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, database.size()),
		[this] (auto r) {
			DBStateCommitmentTrie::prefix_t key_buf;
			for (auto i = r.begin(); i < r.end(); i++) {
				if (database[i].modified_since_last_commit_production()) {
					MemoryDatabase::write_trie_key(key_buf, database[i].get_owner());
					commitment_trie.parallel_insert(key_buf, DBStateCommitmentValueT(database[i].produce_commitment()));
					//database[i].mark_unmodified_since_last_commit_production();
				}
			}
		});*/
	set_trie_commitment_to_user_account_commits(log);
}

void 
MemoryDatabase::finalize_produce_state_commitment() {
	/*std::lock_guard lock(committed_mtx);

	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, database.size()),
		[this](auto r) {
			for (auto i = r.begin(); i < r.end(); i++) {
				database[i].mark_unmodified_since_last_commit_production();
			}
		});
*/
}

struct TentativeValueModifyLambda {
//MemoryDatabase::DBStateCommitmentTrie& commitment_trie;
	std::vector<MemoryDatabase::DBEntryT>& database;
	const MemoryDatabase::index_map_t& user_id_to_idx_map;

	void operator() (AccountID owner, MemoryDatabase::DBStateCommitmentValueT& value) {

		//TODO change this if LogValueT starts storing AccountIDs (it might be good to also store account_db_idxs?)
		//AccountID id;
		//PriceUtils::read_unsigned_big_endian(prefix, id);

		account_db_idx idx = user_id_to_idx_map.at(owner);
		value = database[idx].tentative_commitment();
		//database[idx].mark_unmodified_since_last_checkpoint();
		//commitment_trie.parallel_insert(prefix, database[idx].produce_commitment());
	}
};

struct ProduceValueModifyLambda {
	//relies on the fact that MemoryDatabase and AccountLog use the same key space
	//MemoryDatabase::DBStateCommitmentTrie& commitment_trie;
	std::vector<MemoryDatabase::DBEntryT>& database;
	const MemoryDatabase::index_map_t& user_id_to_idx_map;

	void operator() (AccountID owner, MemoryDatabase::DBStateCommitmentValueT& value) {

		//TODO change this if LogValueT starts storing AccountIDs (it might be good to also store account_db_idxs?)
		//AccountID id;
		//PriceUtils::read_unsigned_big_endian(prefix, id);

		account_db_idx idx = user_id_to_idx_map.at(owner);
		value = database.at(idx).produce_commitment();
		//database.at(idx).mark_unmodified_since_last_checkpoint();
		//commitment_trie.parallel_insert(prefix, database[idx].produce_commitment());
	}
};

template<typename Lambda>
struct ParallelApplyLambda {
	MemoryDatabase::DBStateCommitmentTrie& commitment_trie;
	Lambda& modify_lambda;

	template<typename Applyable>
	void operator() (const Applyable& work_root) {

		using prefix_t = typename MemoryDatabase::DBStateCommitmentTrie::prefix_t;

		//std::printf("starting an apply\n");
		/*if (work_root == nullptr) {
			std::printf("workroot is nullptr!\n");
			throw std::runtime_error("work root is nullptr!");
		}*/

		//Must guarantee no concurrent modification of commitment_trie (other than values)

		auto* commitment_trie_subnode = commitment_trie.get_subnode_ref_nolocks(prefix_t{work_root . get_prefix_bytes()}, work_root . get_prefix_len());

		if (commitment_trie_subnode == nullptr) {
			throw std::runtime_error("get_subnode_ref_nolocks should not return nullptr ever");
		}

		//std::printf("got committment trie subnode\n");

		//std::printf("work root prefix: %s %d\n subnode prefix: %s %d\n", 
		//	DebugUtils::__array_to_str(work_root->get_prefix(), __num_prefix_bytes(work_root -> get_prefix_len())).c_str(), work_root -> get_prefix_len(),
		//	DebugUtils::__array_to_str(commitment_trie_subnode->get_prefix(), __num_prefix_bytes(commitment_trie_subnode -> get_prefix_len())).c_str(), commitment_trie_subnode -> get_prefix_len());


		auto apply_lambda = [this, commitment_trie_subnode] (const AccountModificationLog::LogValueT& log_value) {

			//std::printf("apply lambda to: %s %d\n subnode prefix: %s %d\n", 
			//	DebugUtils::__array_to_str(prefix, __num_prefix_bytes(64)).c_str(), 64,
			//	DebugUtils::__array_to_str(commitment_trie_subnode->get_prefix(), __num_prefix_bytes(commitment_trie_subnode -> get_prefix_len())).c_str(), commitment_trie_subnode -> get_prefix_len());

			AccountID owner = log_value.owner;

			auto modify_lambda_wrapper = [this, owner] (MemoryDatabase::DBStateCommitmentValueT& commitment_value_out) {
				modify_lambda(owner, commitment_value_out);
			};

			MemoryDatabase::DBStateCommitmentTrie::prefix_t prefix;
			PriceUtils::write_unsigned_big_endian(prefix, owner);

			commitment_trie_subnode -> modify_value_nolocks(prefix, modify_lambda_wrapper);
		};

		work_root . apply(apply_lambda);
		commitment_trie.invalidate_hash_to_node_nolocks(commitment_trie_subnode);
	}
};

/*struct ParallelInsertLambda {
	//relies on the fact that MemoryDatabase and AccountLog use the same key space
	MemoryDatabase::DBStateCommitmentTrie& commitment_trie;
	std::vector<MemoryDatabase::DBEntryT>& database;
	std::unordered_map<AccountID, account_db_idx>& user_id_to_idx_map;

	void operator() (const unsigned char* prefix, const typename AccountModificationLog::LogValueT& _unused) {

		//TODO change this if LogValueT starts storing AccountIDs (it might be good to also store account_db_idxs?)
		AccountID id;
		PriceUtils::read_unsigned_big_endian(prefix, id);

		account_db_idx idx = user_id_to_idx_map.at(id);
		commitment_trie.parallel_insert(prefix, database[idx].produce_commitment());
	}
};*/

void MemoryDatabase::tentative_produce_state_commitment(Hash& hash, const AccountModificationLog& log) {
	std::lock_guard lock(committed_mtx);

	TentativeValueModifyLambda func{database, user_id_to_idx_map};

	ParallelApplyLambda<TentativeValueModifyLambda> apply_lambda{commitment_trie, func};

	std::printf("starting tentative_produce_state_commitment, size =%lu\n", commitment_trie.size());

	log.parallel_iterate_over_log(apply_lambda);

	commitment_trie.freeze_and_hash(hash);
}

void MemoryDatabase::set_trie_commitment_to_user_account_commits(const AccountModificationLog& log) {

	ProduceValueModifyLambda func{database, user_id_to_idx_map};

	ParallelApplyLambda<ProduceValueModifyLambda> apply_lambda{commitment_trie, func};

	std::printf("starting produce_state_commitment, size =%lu\n", commitment_trie.size());

	log.parallel_iterate_over_log(apply_lambda);
}


void MemoryDatabase::produce_state_commitment(Hash& hash, const AccountModificationLog& log) {

	std::lock_guard lock(committed_mtx);

	set_trie_commitment_to_user_account_commits(log);

	commitment_trie.freeze_and_hash(hash);

	//commitment_trie._log("db trie: ");

}
/*
void MemoryDatabase::produce_state_commitment(Hash& hash, const std::vector<AccountID>& dirty_accounts) {
	std::lock_guard lock(committed_mtx);
	_produce_state_commitment(hash, dirty_accounts);
}

void MemoryDatabase::_produce_state_commitment(Hash& hash, const std::vector<AccountID>& dirty_accounts) {
	const auto dirty_accounts_sz = dirty_accounts.size();
	const size_t num_blocks = 40;
	const auto block_size = (dirty_accounts_sz/num_blocks) + 1;
	//std::printf("dirty accounts size:%lu\n num_blocks:%lu\n", dirty_accounts_sz, num_blocks);
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, num_blocks),
		[this, &dirty_accounts, &block_size, &dirty_accounts_sz](auto r) {
			DBStateCommitmentTrie::prefix_t key_buf;//TRIE_KEYLEN];
			//std::printf("begin %lu end %lu\n", r.begin(), r.end());
			for (auto i = r.begin(); i < r.end(); i++) {
				DBStateCommitmentTrie trie;

				auto start_idx = i * block_size;
				auto end_idx = std::min((i+1)*block_size, dirty_accounts_sz);
				for (auto idx = start_idx; idx < end_idx; idx++) {
					MemoryDatabase::write_trie_key(key_buf, dirty_accounts.at(idx));
					trie.insert(key_buf, DBStateCommitmentValueT(database.at(user_id_to_idx_map.at(dirty_accounts[idx])).produce_commitment()));
				}
				commitment_trie.parallel_merge_in(std::move(trie));
			}
		});
	commitment_trie.freeze_and_hash(hash);
} */

void
MemoryDatabase::_produce_state_commitment(Hash& hash) {
	std::printf("_produce_state_commitment\n");

	std::atomic_int32_t state_modified_count = 0;

	const auto block_size = database.size() / 200;

	//std::atomic_thread_fence(std::memory_order_release);
	tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, database.size(), block_size),
		[this, &state_modified_count](auto r) {
			//std::atomic_thread_fence(std::memory_order_acquire);
			int tl_state_modified_count = 0;
			DBStateCommitmentTrie::prefix_t key_buf;
			DBStateCommitmentTrie local_trie;
			for (auto i = r.begin(); i < r.end(); i++) {
				//if (database[i].modified_since_last_commit_production()) {
					tl_state_modified_count ++;
					MemoryDatabase::write_trie_key(key_buf, database.at(i).get_owner());
					//TODO the problem is here - use a better iterator, not parallel_insert
					local_trie.insert(key_buf, DBStateCommitmentValueT(database.at(i).produce_commitment()));
				//	database[i].mark_unmodified_since_last_commit_production();
				//}
			}
			commitment_trie.merge_in(std::move(local_trie));
			state_modified_count.fetch_add(tl_state_modified_count, std::memory_order_relaxed);
			//std::atomic_thread_fence(std::memory_order_release);
		});
	//std::atomic_thread_fence(std::memory_order_acquire);

	BLOCK_INFO("state modified count = %d", state_modified_count.load());

	commitment_trie.freeze_and_hash(hash);

	INFO_F(commitment_trie._log("db commit"));
	INFO_F(log());
}

/*
void DBPersistenceThunkThreadLocal::operator() (TrieT* work_node) {
	auto lambda = [this](const unsigned char* prefix, AccountModificationLog::LogValueT& account_log) {
		AccountID account = account_log.owner;

		account_db_idx idx;
		if (!db.lookup_user_id(account, &idx)) {
			throw std::runtime_error("can't commit invalid account");
		}
		//account_db_idx idx = user_id_to_idx_map.at(account);

		AccountCommitment commitment = db.produce_commitment(idx);

		kvs.emplace_back(account, std::move(xdr::xdr_to_msg(commitment)));

		//auto commitment_buf = xdr::xdr_to_msg(commitment);
		//dbval val = dbval{commitment_buf->data(), commitment_buf->size()};
		//dbval key = UserAccount::produce_lmdb_key(account);

		//auto bytes = val.bytes();

		//write_txn.put(account_lmdb_instance.dbi, key, val);
		//database[idx].mark_unmodified_since_last_checkpoint();
	};

	work_node -> apply(lambda);
}*/



void MemoryDatabase::add_persistence_thunk(uint64_t current_block_number, AccountModificationLog& log) {
	std::lock_guard lock(db_thunks_mtx);
	persistence_thunks.emplace_back(*this, current_block_number);
	log.template parallel_accumulate_values<DBPersistenceThunk>(persistence_thunks.back());
}

void MemoryDatabase::clear_persistence_thunks_and_reload(uint64_t expected_persisted_round_number) {
	std::lock_guard lock1(db_thunks_mtx);
	std::lock_guard lock2(committed_mtx);
	std::lock_guard lock3(uncommitted_mtx);
	
	rollback_new_accounts_(expected_persisted_round_number);

	if (expected_persisted_round_number != account_lmdb_instance.get_persisted_round_number()) {
		throw std::runtime_error("invalid load!");
	}

	for (size_t i = persistence_thunks.size(); i != 0; i--) {
		auto& thunk = persistence_thunks.at(i-1);

		if (thunk.current_block_number > expected_persisted_round_number) {
		//	std::atomic_thread_fence(std::memory_order_release);

			tbb::parallel_for(tbb::blocked_range<size_t>(0, thunk.kvs->size(), 10000),
				[this, &thunk] (auto r) {
					//std::atomic_thread_fence(std::memory_order_acquire);
					auto rtx = account_lmdb_instance.rbegin();
					for (auto idx = r.begin(); idx < r.end(); idx++) {
						dbval key = UserAccount::produce_lmdb_key(thunk.kvs->at(idx).key);

						auto res = rtx.get(account_lmdb_instance.get_data_dbi(), key);
						
						if (res) {
							AccountCommitment commitment;
							dbval_to_xdr(*res, commitment);

							auto iter = user_id_to_idx_map.find(thunk.kvs->at(idx).key);
						
							if (iter == user_id_to_idx_map.end()) {
								throw std::runtime_error("invalid lookup to user_id_to_idx_map!");
							}
							database[iter->second] = UserAccount(commitment);
						}
					}
					//std::atomic_thread_fence(std::memory_order_release);
				});
			//std::atomic_thread_fence(std::memory_order_acquire);
		}
	}
	persistence_thunks.clear();
}

void MemoryDatabase::commit_persistence_thunks(uint64_t max_round_number) {

	std::vector<DBPersistenceThunk> thunks_to_commit;
	{
		std::lock_guard lock(db_thunks_mtx);
		for (size_t i = 0; i < persistence_thunks.size();) {
			auto& thunk = persistence_thunks.at(i);
			if (thunk.current_block_number <= max_round_number) {
				thunks_to_commit.emplace_back(std::move(thunk));
				persistence_thunks.erase(persistence_thunks.begin() + i);
			} else {
				i++;
			}
		}
	}

	if (thunks_to_commit.size() == 0) {
		BLOCK_INFO("NO THUNKS TO COMMIT");
		return;
	}


	//new thunks are pushed on the back, so iterating from the front is ok so long as we lock access to thunks
	// to prevent reference invalication on a realloc
	auto current_block_number = get_persisted_round_number();

	dbenv::wtxn write_txn{nullptr};
	if (account_lmdb_instance)
	{
		write_txn = account_lmdb_instance.wbegin();
	}

	for (size_t i = 0; i < thunks_to_commit.size(); i++) {

		// this lets blocks add thunks during commits
		auto& thunk = thunks_to_commit.at(i);

		if (thunk.current_block_number > max_round_number) {
			continue;
		}

		if (thunk.current_block_number != current_block_number + 1) {
			std::printf("i = %lu thunks[i].current_block_number= %lu current_block_number = %lu\n", i, thunk.current_block_number, current_block_number);
			throw std::runtime_error("can't persist blocks in wrong order!!!");
		}

		size_t thunk_sz = thunk.kvs ->size();
		for (size_t i = 0; i < thunk_sz; i++) {

		//for (auto iter = thunk.kvs->begin(); iter != thunk.kvs->end(); iter++) {
			auto& kv = (*thunk.kvs)[i];
		
			dbval key = UserAccount::produce_lmdb_key(kv.key);

			if (!(kv.msg.size())) {
				std::printf("missing value for kv %lu\n", kv.key);
				std::printf("thunk.kvs.size() = %lu\n", thunk.kvs->size());
				throw std::runtime_error("failed to accumulate value in persistence thunk");
			}

			//if ((*iter).msg->size() == 0) {
			//	throw std::runtime_error("msg size was zero!!!");
			//}
			dbval val = dbval{kv.msg.data(), kv.msg.size()};
			
			if (account_lmdb_instance) {
				write_txn.put(account_lmdb_instance.dbi, key, val);
			}
		}
		current_block_number = thunk.current_block_number;
	}

	account_lmdb_instance.commit_wtxn(write_txn, current_block_number, false);

	{
		std::lock_guard lock(db_thunks_mtx);
		for (size_t i = 0; i < account_creation_thunks.size();) {
			auto& thunk = account_creation_thunks.at(i);
			if (thunk.current_block_number <= max_round_number) {
				account_creation_thunks.erase(account_creation_thunks.begin() + i);
			} else {
				i++;
			}
		}
	}

	for (auto& thunk : thunks_to_commit) {
		thunk.detached_clear();
	}
	

	if (current_block_number != max_round_number) {
		std::printf("Missing a round commitment!\n");
		throw std::runtime_error("missing commitment");
	}
	if (account_lmdb_instance) {
		auto stats = account_lmdb_instance.stat();
		BLOCK_INFO("db size: %lu", stats.ms_entries);
	}
}

	/*
	size_t start_sz = 0;
	{
		std::lock_guard lock(db_thunks_mtx);

		start_sz = persistence_thunks.size();

		if (start_sz == 0) {
			return;
		}
	}

	//if (account_lmdb_instance) 
	{

		//new thunks are pushed on the back, so iterating from the front is ok so long as we lock access to thunks
		// to prevent reference invalication on a realloc
		auto current_block_number = get_persisted_round_number();

		dbenv::wtxn write_txn{nullptr};
		if (account_lmdb_instance)
		{
			write_txn = account_lmdb_instance.wbegin();
		}

		for (size_t i = 0; i < start_sz; i++) {

			// this lets blocks add thunks during commits
			std::lock_guard lock(committed_mtx);
			if ( i >= persistence_thunks.size()) {
				throw std::runtime_error("persistence thunks shrunk in size???");
			}
			auto& thunk = persistence_thunks[i];

			if (thunk.current_block_number > max_round_number) {
				continue;
			}

			if (thunk.current_block_number != current_block_number + 1) {
				std::printf("i = %lu thunks[i].current_block_number= %lu current_block_number = %lu\n", i, thunk.current_block_number, current_block_number);
				throw std::runtime_error("can't persist blocks in wrong order!!!");
			}

			for (auto iter = thunk.kvs.begin(); iter != thunk.kvs.end(); iter++) {
				dbval key = UserAccount::produce_lmdb_key((*iter).key);

				if (!((*iter).msg)) {
					std::printf("missing value for kv %lu\n", (*iter).key);
					std::printf("thunk.kvs.size() = %lu\n", thunk.kvs.size());
					throw std::runtime_error("failed to accumulate value in persistence thunk");
				}

				if ((*iter).msg->size() == 0) {
					throw std::runtime_error("msg size was zero!!!");
				}
				dbval val = dbval{(*iter).msg->data(), (*iter).msg -> size()};
				
				if (account_lmdb_instance) {
					write_txn.put(account_lmdb_instance.dbi, key, val);
				}
			}
			current_block_number = thunk.current_block_number;
		}

		account_lmdb_instance.commit_wtxn(write_txn, current_block_number);
		

		if (current_block_number != max_round_number) {
			std::printf("Missing a round commitment!\n");
			throw std::runtime_error("missing commitment");
		}
		if (account_lmdb_instance) {
			auto stats = account_lmdb_instance.stat();
			BLOCK_INFO("db size: %lu", stats.ms_entries);
		}
	}

	std::lock_guard lock(committed_mtx);
	for (size_t i = 0; i < persistence_thunks.size();) {
		auto& thunk = persistence_thunks[i];
		if (thunk.current_block_number <= max_round_number) {
			persistence_thunks.erase(persistence_thunks.begin() + i);
		} else {
			i++;
		}
	}


}
*/

/*
std::optional<dbenv::wtxn> MemoryDatabase::persist_lmdb(uint64_t current_block_number, AccountModificationLog& log, bool lazy_commit) {
	std::shared_lock lock(committed_mtx);

	DBPersistenceThunk thunk(*this, current_block_number);

	log.template parallel_accumulate_values<DBPersistenceThunk>(thunk);

	//log.parallel_iterate_over_log_threadlocal_acc(thunk);

	size_t modified = 0;

	auto write_txn = account_lmdb_instance.wbegin();
	for (auto iter = thunk.kvs->begin(); iter != thunk.kvs->end(); iter++) {
		//for (auto iter2 = (*iter).begin(); iter2 != (*iter).end(); iter2++) {
			dbval key = UserAccount::produce_lmdb_key((*iter).key);
			dbval val = dbval{(*iter).msg->data(), (*iter).msg -> size()};

			write_txn.put(account_lmdb_instance.dbi, key, val);
			modified ++;
		//}
	}

	if (modified != log.size()) {
		throw std::runtime_error("trie apply error?");
	}
	BLOCK_INFO("modified count = %d", log.size());

	if (lazy_commit) {
		return write_txn;
	} else {
		finish_persist_lmdb(std::move(write_txn), current_block_number);
		return std::nullopt;
	}
}*/

//THIS VERSION HAS TO BE DONE EVERY ROUND, or else dirty_accounts needs to be accumulated
/*std::optional<dbenv::wtxn> MemoryDatabase::persist_lmdb(uint64_t current_block_number, AccountModificationLog& log, bool lazy_commit) {
	std::shared_lock lock(committed_mtx);

	int modified = 0;

	auto write_txn = account_lmdb_instance.wbegin();

	for (auto iter = log.begin(); !iter.at_end(); ++iter) {
		modified ++;

		AccountID account = (*iter).second.get().owner;
		account_db_idx idx = user_id_to_idx_map.at(account);

		AccountCommitment commitment = database[idx].produce_commitment();

		auto commitment_buf = xdr::xdr_to_msg(commitment);
		dbval val = dbval{commitment_buf->data(), commitment_buf->size()};
		dbval key = UserAccount::produce_lmdb_key(account);

		auto bytes = val.bytes();

		write_txn.put(account_lmdb_instance.dbi, key, val);
		database[idx].mark_unmodified_since_last_checkpoint();

	}
	BLOCK_INFO("modified count = %d", log.size());
	if (modified != log.size()) {
		throw std::runtime_error("trie iterator error");
	}

	if (lazy_commit) {
		return write_txn;
	}
	else {
		finish_persist_lmdb(std::move(write_txn), current_block_number);
		return std::nullopt;
	}
}*/

/*
//THIS VERSION HAS TO BE DONE EVERY ROUND, or else dirty_accounts needs to be accumulated.
std::optional<dbenv::wtxn> MemoryDatabase::persist_lmdb(uint64_t current_block_number, const std::vector<AccountID>& dirty_accounts, bool lazy_commit) {
	std::shared_lock lock(committed_mtx);

	auto write_txn = account_lmdb_instance.wbegin();

	int modified_count = 0;
	for (std::size_t i = 0; i < dirty_accounts.size(); i++) {
		modified_count++;

		AccountID account = dirty_accounts[i];

		account_db_idx idx = user_id_to_idx_map.at(account);

		AccountCommitment commitment = database[idx].produce_commitment();

		auto commitment_buf = xdr::xdr_to_opaque(commitment);
		dbval val = dbval{commitment_buf.data(), commitment_buf.size()};
		dbval key = UserAccount::produce_lmdb_key(account);

		//auto bytes = val.bytes();

		write_txn.put(account_lmdb_instance.dbi, key, val);
	//	database[idx].mark_unmodified_since_last_checkpoint();
	}

	BLOCK_INFO("modified count = %d", dirty_accounts.size());

	if (lazy_commit) {
		return write_txn;
	}
	else {
		finish_persist_lmdb(std::move(write_txn), current_block_number);
		return std::nullopt;
	}

}

// MUST BE CALLED FROM SAME THREAD AS persist_lmdb
void MemoryDatabase::finish_persist_lmdb(dbenv::wtxn write_txn, uint64_t current_block_number) {
	account_lmdb_instance.commit_wtxn(write_txn, current_block_number);

	auto stats = account_lmdb_instance.stat();
	BLOCK_INFO("db size: %lu", stats.ms_entries);
	INFO_F(log());
}
*/

void MemoryDatabase::persist_lmdb(uint64_t current_block_number) {
	std::shared_lock lock(committed_mtx);

	auto write_txn = account_lmdb_instance.wbegin();

	std::printf("writing entire database\n");


	int modified_count = 0;
	for (account_db_idx i = 0; i < database.size(); i++) {
		//if (database[i].modified_since_last_checkpoint()) {
		if (i % 100000 == 0) {
			std::printf("%lu\n", i);
		}

			modified_count++;

			AccountCommitment commitment = database[i].produce_commitment();

			auto commitment_buf = xdr::xdr_to_opaque(commitment);
			dbval val = dbval{commitment_buf.data(), commitment_buf.size()};
			dbval key = UserAccount::produce_lmdb_key(database[i].get_owner());

			//auto bytes = val.bytes();
			//std::printf("writing data: key %lu (%lu) value %s mv_size %lu \n", key.uint64(), database[i].get_owner(), DebugUtils::__array_to_str(bytes.data(), bytes.size()).c_str(), val.mv_size);

			write_txn.put(account_lmdb_instance.dbi, key, val);
		//	database[i].mark_unmodified_since_last_checkpoint();


	//	}
	}

	BLOCK_INFO("modified count = %d", modified_count);

	account_lmdb_instance.commit_wtxn(write_txn, current_block_number);

	auto stats = account_lmdb_instance.stat();
	BLOCK_INFO("db size: %lu", stats.ms_entries);
	INFO_F(log());
}

void MemoryDatabase::load_lmdb_contents_to_memory() {
	std::lock_guard lock(committed_mtx);

	auto stats = account_lmdb_instance.stat();

	std::printf("db size: %lu\n", stats.ms_entries);

	//unsigned char key_buf[sizeof(AccountID)];

	auto rtx = account_lmdb_instance.rbegin();

	auto cursor = rtx.cursor_open(account_lmdb_instance.get_data_dbi());

	cursor.get(MDB_FIRST);
	while (cursor) {
//	for (auto iter = cursor.begin(); iter != cursor.end(); ++iter) {
		auto& kv = *cursor;

		auto account_owner = UserAccount::read_lmdb_key(kv.first);

//		std::printf("account %lu\n", account_owner);
		AccountCommitment commitment;

		//MemoryDatabase::write_trie_key(key_buf, database[i].get_owner());
		//commitment_trie.parallel_insert(key_buf, DBStateCommitmentValueT(database[i].produce_commitment()));

		auto bytes = kv.second.bytes();

//		std::printf("bytes size: %lu\n", bytes.size());

//		std::printf("value:%s\n", std::string(DebugUtils::__array_to_str(bytes.data(), bytes.size())).c_str());
		dbval_to_xdr(kv.second, commitment);

		auto owner = commitment.owner;
		if (account_owner != owner) {
			throw std::runtime_error("key read error");
		}
		user_id_to_idx_map.emplace(owner, database.size());
		database.emplace_back(commitment);
		++cursor;
	}

	rtx.commit();
	Hash hash;
	_produce_state_commitment(hash);
}

void MemoryDatabase::log() {
	std::printf("current database log: size %lu\n", database.size());
	//hacky
	/*for (AccountID i = 0; i < database.size(); i++) {
		auto iter = user_id_to_idx_map.find(i);
		if (iter == user_id_to_idx_map.end()) {
			throw std::runtime_error("accounts aren't generated sequentially, logging won't work otherwise");
		}

		database[iter->second].log();
	}*/
	commitment_trie._log("trie: ");
}

void MemoryDatabase::values_log() {
	for (size_t i = 0; i < database.size(); i++) {
		std::printf("%lu account %lu  ", i, database[i].get_owner());
		database[i].stringify();
		std::printf("\n");
	}

	std::printf("uncommitted\n");
	for (size_t i = 0; i < uncommitted_db.size(); i++) {
		std::printf("%lu account %lu  ", i, uncommitted_db[i].get_owner());
		uncommitted_db[i].stringify();
		std::printf("\n");
	}
}

void KVAssignment::operator=(const AccountModificationLog::LogValueT& log) {
	AccountID account = log.owner;

	account_db_idx idx;
	if (!db.lookup_user_id(account, &idx)) {
		std::printf("invalid account: %lu\n", log.owner);
		throw std::runtime_error("can't commit invalid account");
	}
	AccountCommitment commitment = db.produce_commitment(idx);
	kv.key = account;
	kv.msg = xdr::xdr_to_opaque(commitment);
} 

}
