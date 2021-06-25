#include "user_account.h"
#include "simple_debug.h"
#include "price_utils.h"
#include "utils.h"

#include <xdrpp/marshal.h>


namespace edce {


inline uint8_t get_seq_num_offset(uint64_t sequence_number, uint64_t last_committed_id) {

	return (((sequence_number - last_committed_id) / MAX_OPS_PER_TX) - 1);
}

inline uint64_t get_seq_num_increment(uint64_t bv) {
	if (bv == 0) return 0;
	return (64 - __builtin_clzll(bv)) * MAX_OPS_PER_TX;
}

//#define SKIP_SEQUENCE_NUMBERS

TransactionProcessingStatus 
UserAccount::reserve_sequence_number(
	uint64_t sequence_number) {

	#ifdef SKIP_SEQUENCE_NUMBERS
	return TransactionProcessingStatus::SUCCESS;
	#endif

	INFO("Reserving sequence_number %lu", sequence_number);

	if (sequence_number <= last_committed_id) {
		return TransactionProcessingStatus::SEQ_NUM_TOO_LOW;
	}

	uint8_t offset = get_seq_num_offset(sequence_number, last_committed_id);

	if (offset >= 64) {
		return TransactionProcessingStatus::SEQ_NUM_TOO_HIGH;
	}


	uint64_t bit_mask = ((uint64_t) 1) << offset;

	uint64_t prev = sequence_number_vec.fetch_or(bit_mask, std::memory_order_relaxed); // was acq_rel

	if ((prev & bit_mask) != 0) {
		//some other tx has already reserved the sequence number
		return TransactionProcessingStatus::SEQ_NUM_TEMP_IN_USE;
	}

	return TransactionProcessingStatus::SUCCESS;


	//uint8_t offset = (sequence_number - last_committed_id - 1);
	
/*	std::lock_guard lock(sequence_number_mtx);

	if (sequence_number <= last_committed_id) {
		return TransactionProcessingStatus::INVALID_OPERATION;
	}

	auto iter = current_committed_ids.find(sequence_number);
	if (iter != current_committed_ids.end()) {
		return TransactionProcessingStatus::INVALID_OPERATION;
	}

	auto iter2 = current_reserved_ids.find(sequence_number);
	if (iter2 != current_reserved_ids.end()) {
		return TransactionProcessingStatus::TRANSIENT_FAILURE;
	}
	current_reserved_ids.insert(sequence_number);
	return TransactionProcessingStatus::SUCCESS; */
}

void UserAccount::release_sequence_number(
	uint64_t sequence_number) {

	#ifdef SKIP_SEQUENCE_NUMBERS
		return;
	#endif


	if (sequence_number <= last_committed_id) {
		throw std::runtime_error("cannot release invalid seq num!");
	}

	uint8_t offset = get_seq_num_offset(sequence_number, last_committed_id);

	if (offset >= 64) {
		throw std::runtime_error("cannot release too far forward seq num!");
	}

	uint64_t bit_mask = ~(((uint64_t) 1) << offset);

	sequence_number_vec.fetch_and(bit_mask, std::memory_order_relaxed);
	
	/*std::lock_guard lock(sequence_number_mtx);
	
	if (sequence_number <= last_committed_id) {
		throw std::runtime_error("cannot release previously committed seq num");
	}

	auto iter = current_reserved_ids.find(sequence_number);
	if (iter == current_reserved_ids.end()) {
		throw std::runtime_error("cannot release nonreserved seq num");
	}

	current_reserved_ids.erase(iter);*/
}

void UserAccount::commit_sequence_number(
	uint64_t sequence_number) {
	/*std::lock_guard lock(sequence_number_mtx);
	
	if (sequence_number <= last_committed_id) {
		throw std::runtime_error("cannot commit previously committed seq num");
	}


	auto iter = current_reserved_ids.find(sequence_number);
	if (iter == current_reserved_ids.end()) {
		throw std::runtime_error("cannot commit nonreserved seq num");
	}

	current_reserved_ids.erase(iter);
	current_committed_ids.insert(sequence_number);

	if (sequence_number >= cur_max_committed_id) {
		cur_max_committed_id = sequence_number;
	}*/
}


// REQUIRES a memory fence in advance of this.
void UserAccount::commit() {
	INFO("committing user");

//	std::lock_guard lock(sequence_number_mtx);
	std::lock_guard lock2(uncommitted_assets_mtx);

	for (auto iter = owned_assets.begin(); iter != owned_assets.end(); iter++) {
		iter->commit();
	}
	int uncommitted_size = uncommitted_assets.size();
	for (int i = 0; i < uncommitted_size; i++) {
		owned_assets.emplace_back(uncommitted_assets[i].commit());
	}

	uncommitted_assets.clear();
	//current_reserved_ids.clear();
	//current_committed_ids.clear();
	//if (last_committed_id != cur_max_committed_id) {
	//	_modified_since_last_commit_production = true;
	//	_modified_since_last_checkpoint = true;
	//}
	last_committed_id += get_seq_num_increment(sequence_number_vec.load(std::memory_order_relaxed)); // was acquire
	sequence_number_vec.store(0, std::memory_order_relaxed); // was release
	//last_committed_id = cur_max_committed_id;
}

/*
TODO - I added locks to various useraccount methods so as to not trigger spurious data race warnings.
These can probably be removed.
*/
void UserAccount::rollback() {
	//std::lock_guard lock(sequence_number_mtx);
	std::lock_guard lock2(uncommitted_assets_mtx);

	INFO("rolling back account");
	for (auto iter = owned_assets.begin(); iter != owned_assets.end(); iter++) {
		iter->rollback(); // iter->second.rollback();
	}
	uncommitted_assets.clear();

	//current_reserved_ids.clear();
	//current_committed_ids.clear();

	sequence_number_vec.store(0, std::memory_order_relaxed);
	//cur_max_committed_id = last_committed_id;
}

bool UserAccount::in_valid_state() {

	//std::lock_guard lock(sequence_number_mtx);
	std::lock_guard lock2(uncommitted_assets_mtx);

	for (auto iter = owned_assets.begin(); iter != owned_assets.end(); iter++) {
		if (!iter->in_valid_state()) {
			return false;
		}
	}
	for (auto iter = uncommitted_assets.begin(); iter != uncommitted_assets.end(); iter++) {
		if (!iter->in_valid_state()) {
			return false;
		}
	}
	return true;
}
/*

bool UserAccount::modified_since_last_commit_production() {
	for (uint8_t i = 0; i < owned_assets.size(); i++) {
		if (owned_assets[i].get_commit_modified()) {
			return true;
		}
	}
	return _modified_since_last_commit_production;
}

void UserAccount::mark_unmodified_since_last_commit_production() {
	_modified_since_last_commit_production = false;
	for (uint8_t i = 0; i < owned_assets.size(); i++) {
		owned_assets[i].clear_commit_modified_flag();
	}
}

bool UserAccount::modified_since_last_checkpoint() {
	for (uint8_t i = 0; i < owned_assets.size(); i++) {
		if (owned_assets[i].get_checkpoint_modified()) {
			return true;
		}
	}
	return _modified_since_last_checkpoint;
}

void UserAccount::mark_unmodified_since_last_checkpoint() {
	_modified_since_last_checkpoint = false;
	for (uint8_t i = 0; i < owned_assets.size(); i++) {
		owned_assets[i].clear_checkpoint_modified_flag();
	}
}

*/


//not threadsafe with commit ofc
/*int UserAccount::serialize_len() {
	int active_cnt = 0;
	for (unsigned int i = 0; i < owned_assets.size(); i++) {
		if (!owned_assets[i].empty()) {
			active_cnt++;
		}
	}
	int header_bytes = crypto_sign_PUBLICKEYBYTES + sizeof(last_committed_id); // for seq number
	return header_bytes + active_cnt * (1 + RevertableAsset::SERIALIZATION_BYTES);
}*/

AccountCommitment UserAccount::produce_commitment() const {
	//std::lock_guard lock(sequence_number_mtx);
	std::lock_guard lock2(uncommitted_assets_mtx);

	AccountCommitment output;
	output.owner = owner;
	for (uint8_t i = 0; i < owned_assets.size(); i++) {
		output.assets.push_back(owned_assets[i].produce_commitment(i));
	}
	output.last_committed_id = last_committed_id;
	output.pk = pk;
//	memcpy(output.pk.data(), pk, crypto_sign_PUBLICKEYBYTES);
	return output;
}

AccountCommitment UserAccount::tentative_commitment() const {
//	std::lock_guard lock(sequence_number_mtx);
	std::lock_guard lock2(uncommitted_assets_mtx);

	AccountCommitment output;
	output.owner = owner;
	for (uint8_t i = 0; i < owned_assets.size(); i++) {
		output.assets.push_back(owned_assets[i].tentative_commitment(i));
	}
	for (uint8_t i = 0; i < uncommitted_assets.size(); i++) {
		output.assets.push_back(uncommitted_assets[i].tentative_commitment(i + owned_assets.size()));
	}
	output.last_committed_id = last_committed_id + get_seq_num_increment(sequence_number_vec.load(std::memory_order_relaxed)); // was acquire
	//output.last_committed_id = cur_max_committed_id;
	output.pk = pk;
//	memcpy(output.pk.data(), pk, crypto_sign_PUBLICKEYBYTES);
	INFO("tentative_commitment for %lu: last_committed = %lu cur_max = %lu", 
		owner, last_committed_id, last_committed_id + get_seq_num_increment(sequence_number_vec.load(std::memory_order_relaxed)));

	return output;
}

dbval UserAccount::produce_lmdb_key(const AccountID& owner) {

	return dbval(&owner, sizeof(AccountID));

	//const uint8_t buf_size = sizeof(AccountID);

	//unsigned char buf[buf_size];
	//PriceUtils::write_unsigned_big_endian(buf, owner);

	//return dbval(buf, buf_size);
}

AccountID UserAccount::read_lmdb_key(const dbval& key) {
	return key.uint64(); // don't copy the database across systems with diff endianness
	//AccountID output;
	//PriceUtils::read_unsigned_big_endian(key.mv_data, output);
	//return output;
}

} /* edce */
