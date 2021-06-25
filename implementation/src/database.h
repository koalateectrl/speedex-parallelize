#pragma once

#include "xdr/types.h"

#include <cstdint>

#include "memory_database.h"

#include "database_types.h"

#define FORCE_TEMPLATE_INSTANTIATION(classname) template class classname<MemoryDatabase>;

namespace edce
{

typedef uint64_t account_db_idx;

/*template <typename T>
struct _force_compilation{
	using type = T;
};

template <template<class> class Class_T>
class Instantiate_DB {
	typedef  Class_T<MemoryDatabase> _internal;
	_force_compilation<_internal> foo;
};*/


/*
template <template<class> class Class_T, typename Database_T>
struct _force_compilation{
	using type = Class_T<Database_T>;
};



template<typename T>
struct _pass_type {
	using type = T;
};

template <typename... T>
struct _fake_set{};

template <template<class> class Class_T, typename Head, typename... Tail>
void _apply_set(_pass_type<Head> head, _fake_set<Tail...> tail) {
	_force_compilation<Class_T, Head> {};
	_apply_set<Class_T, Tail...>(tail);
}

template <template<class> class Class_T>
void _apply_set<Class_T>(_fake_set<> tail) {}

template<template<class> class Class_T>
struct Instantiate_DB {
	Instantiate_DB() {
		_apply_set<Class_T, MemoryDatabase>(_fake_set<MemoryDatabase>());
	}
};*/



/*

class Database {
public:

	const static AssetID NATIVE_ASSET = 0;


//Adding an account that already exists simply returns the db idx.
//This makes the operation commutative.



	virtual account_db_idx add_account_to_db(AccountID account) = 0;

	virtual void rollback() = 0;
	virtual void commit() = 0;

	virtual bool lookup_user_id(
		AccountID account, account_db_idx* index_out) = 0;

	//input to index is what is returned from lookup
	virtual void transfer_available(
		account_db_idx user_index, 
		AssetID asset_type, 
		int64_t change) = 0;
	virtual void transfer_escrow(
		account_db_idx user_index,
		AssetID asset_type,
		int64_t change) = 0;
	virtual void escrow(
		account_db_idx user_index, 
		AssetID asset_type, 
		int64_t change) = 0;

	virtual void lock_transfer_available(
		account_db_idx user_index, 
		AssetID asset_type, 
		int64_t change) = 0;
	virtual void lock_transfer_escrow(
		account_db_idx user_index,
		AssetID asset_type,
		int64_t change) = 0;
	virtual void lock_escrow(
		account_db_idx user_index, 
		AssetID asset_type, 
		int64_t change) = 0;

	virtual bool conditional_transfer_available(
		account_db_idx user_index, 
		AssetID asset_type, 
		int64_t change) = 0;
	virtual bool conditional_transfer_escrow(
		account_db_idx user_index, 
		AssetID asset_type, 
		int64_t change) = 0;
	virtual bool conditional_escrow(
		account_db_idx user_index, 
		AssetID asset_type, 
		int64_t change) = 0;

	virtual TransactionProcessingStatus reserve_sequence_number(
		account_db_idx user_index,
		uint64_t sequence_number) = 0;

	virtual void release_sequence_number(
		account_db_idx user_index,
		uint64_t sequence_number) = 0;

	virtual void commit_sequence_number(
		account_db_idx user_index,
		uint64_t sequence_number) = 0;

	//Obv not threadsafe with logging asset changes
	//not threadsafe with creating accounts or committing or rollbacking
	//not threadsafe with anything, probably. 
	virtual bool check_valid_state() = 0;

	virtual int64_t lookup_available_balance(
		account_db_idx user_index,
		AssetID asset_type) = 0;

};
*/

}