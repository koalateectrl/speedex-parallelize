#pragma once

#include "xdr/types.h"
#include "xdr/block.h"

#include <sodium.h>
#include <array>

#include "edce_management_structures.h"

namespace edce {

class SamBlockSignatureChecker {
public:
	SamBlockSignatureChecker() {
		if (sodium_init() == -1) {
			throw std::runtime_error("coud not init sodium");
		}
	}

	bool check_all_sigs(const SerializedBlock& block, const SerializedPKs& pks);
};

class BlockSignatureChecker {

	EdceManagementStructures& management_structures;

public:
	BlockSignatureChecker(EdceManagementStructures& management_structures) 
	: management_structures(management_structures) {
		if (sodium_init() == -1) {
			throw std::runtime_error("could not init sodium");
		}
	}

	bool check_all_sigs(const SerializedBlock& block);
};


struct DeterministicKeyGenerator {
	using SecretKey = std::array<unsigned char, 64>;

	DeterministicKeyGenerator() {
		if (sodium_init() == -1) {
			throw std::runtime_error("could not init sodium");
		}
	}

	std::pair<SecretKey, PublicKey> 
	deterministic_key_gen(AccountID account);

	std::pair<std::vector<SecretKey>, std::vector<PublicKey>>
	gen_key_pair_list(AccountID num_accounts);
};



} /* edce */