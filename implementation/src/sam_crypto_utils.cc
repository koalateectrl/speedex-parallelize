#include <iostream>
#include "sam_crypto_utils.h"

namespace sam_edce {

std::pair<DeterministicKeyGenerator::SecretKey, PublicKey>
DeterministicKeyGenerator::deterministic_key_gen(AccountID account) {
    std::array<uint64_t, 4> seed;
    seed.fill(0);
    seed[0] = account;

    SecretKey sk;
    PublicKey pk;

    if (crypto_sign_seed_keypair(pk.data(), sk.data(), reinterpret_cast<unsigned char*>(seed.data()))) {
        throw std::runtime_error("sig gen failed!");
    }

    return std::make_pair(sk, pk);
}

void
DeterministicKeyGenerator::gen_key_pair_list() {
    return;
}


void
BlockSignatureChecker::check_all_sigs() {
    return;
}

}


