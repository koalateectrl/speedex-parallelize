#pragma once

#include "database.h"
#include "transaction_buffer_manager.h"

#include <sodium.h>
#include <xdrpp/marshal.h>

#include "xdr/transaction.h"

namespace edce {

class SignatureCheck {
	MemoryDatabase& db;

public:
	SignatureCheck(MemoryDatabase& db) : db(db) {
		if (sodium_init() == -1) {
			throw std::runtime_error("Sodium init failed!!!");
		}
	}

	bool check(const SignedTransaction& arg);

};

class BufferedSignatureCheck : public SignatureCheck {

	TransactionBufferManager& unchecked_sigs;
	TransactionBufferManager& checked_sigs;

public:
	BufferedSignatureCheck(
		MemoryDatabase& db, 
		TransactionBufferManager& unchecked_sigs, 
		TransactionBufferManager& checked_sigs) 
	: SignatureCheck(db), unchecked_sigs(unchecked_sigs), checked_sigs(checked_sigs){}

	void run(); //runs forever
};

} /* edce */
