#pragma once

#include <array>
#include <sodium.h>
#include <vector>
#include "xdr/sam_types.h"

namespace sam_edce {

struct DeterministicKeyGenerator {
    using SecretKey = std::array<unsigned char, 64>;
    
    DeterministicKeyGenerator() {
        if (sodium_init() < 0) {
            throw std::runtime_error("could not init sodium");
        }
    }

    std::pair<SecretKey, PublicKey> deterministic_key_gen(AccountID account);

    void gen_key_pair_list();
};


class BlockSignatureChecker {
public:
    BlockSignatureChecker() {
        if (sodium_init() < 0) {
            throw std::runtime_error("could not init sodium");
        }
    }

    void check_all_sigs();
};

}
