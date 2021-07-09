#include "crypto_utils.h"

#include <xdrpp/marshal.h>
#include <tbb/parallel_reduce.h>


#include "utils.h"

#include <cstddef>

namespace edce {

template<typename xdr_type>
bool sig_check(const xdr_type& data, const Signature& sig, const PublicKey& pk) {
	auto buf = xdr::xdr_to_opaque(data);

	return crypto_sign_verify_detached(sig.data(), buf.data(), buf.size(), pk.data()) == 0;
}

class SamSigCheckReduce {
	const SignedTransactionList& block;
	const PublicKeyList& pks;
public:

	bool valid = true;
	void operator() (const tbb::blocked_range<size_t> r) {
		if (!valid) return;

		bool temp_valid = true;

		for (size_t i = r.begin(); i < r.end(); i++) {
			auto sender_acct = block[i].transaction.metadata.sourceAccount;

			// -------DELETE THIS CODE------------
			std::cout << "SAM CHECK" << std::endl;
			std::printf("%lu\n", block[i].transaction.metadata.sourceAccount);

			size_t sig_size = sizeof(block[i].signature) /sizeof(block[i].signature[0]);
			for (size_t j = 0; j < sig_size; j++) {
				std::printf("%lu", block[i].signature[j]);
			}

			size_t pks_size = sizeof(pks[i]) /sizeof(pks[i][0]);
			for (size_t j = 0; j < sig_size; j++) {
				std::printf("%lu", pks[i][j]);
			}
			// -----------------------------------

			if (!sig_check(block[i].transaction, block[i].signature, *pks[i])) {
				std::printf("tx %lu failed, %lu\n", i, sender_acct);
				temp_valid = false;
				break;
			}
		}

		valid = valid && temp_valid;
	}

	SamSigCheckReduce(
		const SignedTransactionList& block,
		const PublicKeyList& pks)
	: block(block)
	, pks(pks) {}

	SamSigCheckReduce(SamSigCheckReduce& other, tbb::split)
	: block(other.block)
	, pks(other.pks) {}

	void join(SamSigCheckReduce& other) {
		valid = valid && other.valid;
	}

};

bool
SamBlockSignatureChecker::check_all_sigs(const SerializedBlock& block, const SerializedPKs& pks) {
	SignedTransactionList txs;
	xdr::xdr_from_opaque(block, txs);

	PublicKeyList pk_list;
	xdr::xdr_from_opaque(pks, pk_list);

	auto checker = SamSigCheckReduce(txs, pk_list);

	tbb::parallel_reduce(tbb::blocked_range<size_t>(0, 5, 2000), checker); // change 5 to txs.size()

	return checker.valid;
}

class SigCheckReduce {
	const EdceManagementStructures& management_structures;
	const SignedTransactionList& block;

public:

	bool valid = true;

	void operator() (const tbb::blocked_range<size_t> r) {
		if (!valid) return;

		bool temp_valid = true;

		for (size_t i = r.begin(); i < r.end(); i++) {
			auto sender_acct = block[i].transaction.metadata.sourceAccount;
			auto pk_opt =  management_structures.db.get_pk_nolock(sender_acct);
			if (!pk_opt) {

				std::printf("no pk! account %lu\n", sender_acct);
				temp_valid = false;
				break;
			}

			// -------DELETE THIS CODE------------
			std::cout << "ORIGINAL CHECK" << std::endl;
			std::printf("%lu\n", block[i].transaction.metadata.sourceAccount);

			size_t sig_size = sizeof(block[i].signature) /sizeof(block[i].signature[0]);
			for (size_t j = 0; j < sig_size; j++) {
				std::printf("%lu", block[i].signature[j]);
			}

			size_t pks_size = sizeof(pks[i]) /sizeof(pks[i][0]);
			for (size_t j = 0; j < sig_size; j++) {
				std::printf("%lu", pks[i][j]);
			}

			std::printf("%lu\n", *pk_opt);
			// -----------------------------------

			if (!sig_check(block[i].transaction, block[i].signature, *pk_opt)) {
				std::printf("tx %lu failed, %lu\n", i, sender_acct);
				temp_valid = false;
				break;
			}
		}
		valid = valid && temp_valid;
	}

	SigCheckReduce(
		const EdceManagementStructures& management_structures,
		const SignedTransactionList& block)
	: management_structures(management_structures)
	, block(block) {}

	SigCheckReduce(SigCheckReduce& other, tbb::split)
	: management_structures(other.management_structures)
	, block(other.block) {}

	void join(SigCheckReduce& other) {
		valid = valid && other.valid;
	}
};


bool 
BlockSignatureChecker::check_all_sigs(const SerializedBlock& block) {
	SignedTransactionList txs;
	xdr::xdr_from_opaque(block, txs);

	auto checker = SigCheckReduce(management_structures, txs);

	tbb::parallel_reduce(tbb::blocked_range<size_t>(0, 5, 2000), checker); // change from 5 to txs.size()

	return checker.valid;
}




std::pair<std::vector<DeterministicKeyGenerator::SecretKey>, std::vector<PublicKey>>
DeterministicKeyGenerator::gen_key_pair_list(size_t num_accounts) {
	std::vector<SecretKey> sks;
	std::vector<PublicKey> pks;
	sks.resize(num_accounts);
	pks.resize(num_accounts);
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, num_accounts),
		[this, &sks, &pks] (auto r) {
			for (auto i = r.begin(); i < r.end(); i++) {
	//for (size_t i = 0; i < num_accounts; i++) {
				auto [sk, pk] = deterministic_key_gen(i);
				sks[i] = sk;
				pks[i] = pk;
			}
		});
	return std::make_pair(sks, pks);
}


// Clearly, a real-world system wouldn't generate keys all on the central server
std::pair<DeterministicKeyGenerator::SecretKey, PublicKey> 
DeterministicKeyGenerator::deterministic_key_gen(AccountID account) {
	std::array<uint64_t, 4> seed; // 32 bytes
	seed.fill(0);
	seed[0] = account;


	SecretKey sk;
	PublicKey pk;

	if (crypto_sign_seed_keypair(pk.data(), sk.data(), reinterpret_cast<unsigned char*>(seed.data()))) {
		throw std::runtime_error("sig gen failed!");
	}

	return std::make_pair(sk, pk);
}



} /* edce */
