#pragma once

#include "../xdr/transaction.h"
#include "../xdr/types.h"

#include <sodium.h>

#include <stdexcept>
#include <cstdint>

#include <xdrpp/marshal.h>

namespace txgen {

struct TransactionBuilder {

	TransactionBuilder() {
		if (sodium_init() != 0) {
			throw std::runtime_error("failed to init sodium");
		}
	}

	edce::SignedTransaction create_new_account_tx(
		edce::SigningKey& creator_sk, 
		edce::AccountID creator_id, 
		uint64_t& creator_seqnum,
		edce::PublicKey new_pk, 
		edce::AccountID new_id,
		int64_t default_amount,
		int num_assets) {

		edce::SignedTransaction output;

		auto& tx = output.transaction;
		auto& metadata = tx.metadata;
		metadata.sourceAccount = creator_id;
		metadata.sequenceNumber = creator_seqnum<<8;

		edce::CreateAccountOp create_op(default_amount, new_id, new_pk);

		edce::Operation op;
		op.body.type(edce::OperationType::CREATE_ACCOUNT);
		op.body.createAccountOp() = create_op;

		tx.operations.push_back(op);

		for (int i = 1; i < num_assets; i++) {
			edce::PaymentOp payment(new_id, i, default_amount);
			op.body.type(edce::OperationType::PAYMENT);
			op.body.paymentOp() = payment;
			tx.operations.push_back(op);
		}

		output.signature = sign(tx, creator_sk);

		creator_seqnum++;

		return output;
	}

	edce::SignedTransaction empty_transaction(
		edce::SigningKey& creator_sk, 
		edce::AccountID creator_id, 
		uint64_t& creator_seqnum) {

		edce::SignedTransaction output;

		auto& tx = output.transaction;
		auto& metadata = tx.metadata;
		metadata.sourceAccount = creator_id;
		metadata.sequenceNumber = creator_seqnum<<8;

		creator_seqnum++;

		output.signature = sign(tx, creator_sk);

		return output;
	}

	edce::Signature sign(const edce::Transaction& tx, edce::SigningKey& sk) {
		auto buf = xdr::xdr_to_msg(tx);

		const unsigned char* msg = (const unsigned char*) buf->data();
		auto msg_len = buf->size();

		edce::Signature output;
		//unsigned long long msg_len_out;

		crypto_sign_detached(output.data(), nullptr, msg, msg_len, sk.data());

		return output;
	}

};

}

