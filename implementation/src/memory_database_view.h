#pragma once

#include "user_account.h"

#include <unordered_map>
#include <vector>
#include <cstdint>

#include "xdr/types.h"
#include "xdr/transaction.h"
#include "memory_database.h"
#include "lmdb_wrapper.h"

namespace edce {

class MemoryDatabase;

//There needs to be differential behavior for new accounts 
//vs existing accounts
//We still can't let buffer get negative, ofc, but
// we don't want to send a query to the main db for an acct that
	//doesn't yet exist. TODO

class UserAccountView {
	UserAccount& main;
	//should be always positive.
	//How much additional asset to add to escrow/available accounts
	//if view is successfully committed.
	//std::unordered_map<AssetID, int64_t> escrow_buffer;
	std::unordered_map<AssetID, int64_t> available_buffer;

	//should be always negative.
	//How much asset was taken out of accounts during view lifetime,
	// should be returned to owner if view is unwound.
	//std::unordered_map<AssetID, int64_t> escrow_side_effects;
	std::unordered_map<AssetID, int64_t> available_side_effects;

public:

	UserAccountView(UserAccount& main)
		: main(main) {}

	TransactionProcessingStatus conditional_escrow(
		AssetID asset, int64_t amount);

	//TransactionProcessingStatus transfer_escrow(
	//	AssetID asset, int64_t amount);
	TransactionProcessingStatus transfer_available(
		AssetID asset, int64_t amount);

	int64_t lookup_available_balance(AssetID asset);

	// View should not be used after commit/unwind.
	void commit();
	void unwind();
};

class AccountCreationView {
protected:
	MemoryDatabase& main_db;
	const account_db_idx db_size;
	std::unordered_map<
		account_db_idx, std::pair<AccountID, MemoryDatabase::DBEntryT>> new_accounts;

	std::unordered_map<AccountID, account_db_idx> temporary_idxs;

	void commit();

	AccountCreationView(MemoryDatabase& db)
		: main_db(db),
		db_size(db.size()),
		new_accounts(),
		temporary_idxs() {}

public:
	
	bool lookup_user_id(AccountID account, account_db_idx* index_out);
	TransactionProcessingStatus create_new_account(
		AccountID account, const PublicKey pk, account_db_idx* out);

	TransactionProcessingStatus reserve_sequence_number(account_db_idx idx, uint64_t sequence_number) {
		return main_db.reserve_sequence_number(idx, sequence_number);
	}

	void commit_sequence_number(account_db_idx idx, uint64_t sequence_number) {
		main_db.commit_sequence_number(idx, sequence_number);
	}

};

class BufferedMemoryDatabaseView : public AccountCreationView {


protected:
	std::unordered_map<account_db_idx, UserAccountView> accounts;

	UserAccountView& get_existing_account(account_db_idx account);

public:

	BufferedMemoryDatabaseView(MemoryDatabase& main_db)
		: AccountCreationView(main_db) {}

	void commit();
	void unwind();

	TransactionProcessingStatus escrow(
		account_db_idx account, AssetID asset, int64_t amount);
	TransactionProcessingStatus transfer_available(
		account_db_idx account, AssetID asset, int64_t amount);

	using AccountCreationView::reserve_sequence_number;
	using AccountCreationView::commit_sequence_number;
};

class UnbufferedMemoryDatabaseView : public AccountCreationView {

public:

	UnbufferedMemoryDatabaseView(
		MemoryDatabase& main_db)
		: AccountCreationView(main_db) {}

	TransactionProcessingStatus escrow(
		account_db_idx account, AssetID asset, int64_t amount);
	TransactionProcessingStatus transfer_available(
		account_db_idx account, AssetID asset, int64_t amount);

	uint64_t get_persisted_round_number() {
		return main_db.get_persisted_round_number();
	}

	using AccountCreationView::commit;
	using AccountCreationView::reserve_sequence_number;
	using AccountCreationView::commit_sequence_number;
};

class LoadLMDBMemoryDatabaseView : public LMDBLoadingWrapper<UnbufferedMemoryDatabaseView> {

	using LMDBLoadingWrapper<UnbufferedMemoryDatabaseView> :: generic_do;
public:

	LoadLMDBMemoryDatabaseView(
		uint64_t current_block_number,
		MemoryDatabase& main_db) : LMDBLoadingWrapper<UnbufferedMemoryDatabaseView>(current_block_number, main_db) {}
	
	TransactionProcessingStatus escrow(
		account_db_idx account, AssetID asset, int64_t amount) {
		return generic_do<&UnbufferedMemoryDatabaseView::escrow>(account, asset, amount);
	}
	TransactionProcessingStatus transfer_available(
		account_db_idx account, AssetID asset, int64_t amount) {
		return generic_do<&UnbufferedMemoryDatabaseView::transfer_available>(account, asset, amount);
	}

	void commit() {
		return generic_do<&UnbufferedMemoryDatabaseView::commit>();
	}

	bool lookup_user_id(AccountID account, account_db_idx* index_out) {
		return generic_do<&UnbufferedMemoryDatabaseView::lookup_user_id>(account, index_out);
	}
	TransactionProcessingStatus create_new_account(
		AccountID account, const PublicKey pk, account_db_idx* out) {
		return generic_do<&UnbufferedMemoryDatabaseView::create_new_account>(account, pk, out);
	}

	TransactionProcessingStatus reserve_sequence_number(account_db_idx idx, uint64_t sequence_number) {
		return generic_do<&UnbufferedMemoryDatabaseView::reserve_sequence_number>(idx, sequence_number);
	}

	void commit_sequence_number(account_db_idx idx, uint64_t sequence_number) {
		generic_do<&UnbufferedMemoryDatabaseView::commit_sequence_number>(idx, sequence_number);
	}
};


//for experiments

class UnlimitedMoneyBufferedMemoryDatabaseView : public BufferedMemoryDatabaseView {

public:

	UnlimitedMoneyBufferedMemoryDatabaseView(MemoryDatabase& main_db)
		: BufferedMemoryDatabaseView(main_db) {}

	TransactionProcessingStatus escrow(
		account_db_idx account, AssetID asset, int64_t amount);
	TransactionProcessingStatus transfer_available(
		account_db_idx account, AssetID asset, int64_t amount);
};

} /* edce */