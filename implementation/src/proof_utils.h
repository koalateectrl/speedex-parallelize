#pragma once

#include "xdr/trie_proof.h"
#include "xdr/types.h"

namespace edce {

bool validate_trie_proof(const Proof& proof, const Hash& top_hash, const uint16_t& _MAX_KEY_LEN_BITS);

}