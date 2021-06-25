#include "proof_utils.h"

#include <cstdint>

#include "price_utils.h"

#include "merkle_trie_utils.h"
#include <openssl/sha.h>

#include "simple_debug.h"

namespace edce {


//returns first branch bits beyond prefix_len
	//copied from merkle_trie.h:get_branch_bits
uint8_t proof_branch_bits(const uint8_t* prefix, const PrefixLenBits prefix_len) {
	uint8_t out = prefix[prefix_len.len/8] & (0xF0>>(prefix_len.len % 8));
	out >>= (4-(prefix_len.len % 8)); // 4 = 8 - BRANCH_BITS in the BRANCH_BITS = 4 case
	return out;
}

//uint16_t get_bv(const uint8_t* prefix_len_and_bv, const uint16_t* bv) {
//	PriceUtils::read_unsigned_big_endian<uint16_t>(&prefix_len_and_bv + 2, bv);
//}

int get_expected_hash_idx(const ProofNode& proof_node, const unsigned char* prefix) {

	uint16_t bv = 0;
	PrefixLenBits prefix_length{0};
	PriceUtils::read_unsigned_big_endian(proof_node.prefix_length_and_bv.data(), prefix_length.len);
	PriceUtils::read_unsigned_big_endian(proof_node.prefix_length_and_bv.data() + 2, bv);

	uint8_t branch_bits = proof_branch_bits(prefix, prefix_length);

	uint16_t preceding_indices_mask = (((uint16_t)1) << branch_bits) - 1;
	bv &= preceding_indices_mask;
	return __builtin_popcount(bv);
}


bool validate_trie_root_proof(const Proof& proof, const Hash& root_node_hash) {
	unsigned char buf[36];
	PriceUtils::write_unsigned_big_endian(buf, proof.trie_size);
	memcpy(buf+4, proof.root_node_hash.data(), 32);
	unsigned char hash_buf[32];
	SHA256(buf, 36, hash_buf);
	return memcmp(root_node_hash.data(), hash_buf, 32) == 0;
}


bool validate_trie_leaf_proof(
	const ProofNode& proof_node, 
	const xdr::xvector<uint8_t>& value_bytes,
	const Hash& expected_node_hash, 
	const unsigned char* key, 
	const PrefixLenBits& MAX_KEY_LEN_BITS) {
	
	uint16_t prefix_length = 0, bv = 0;
	PriceUtils::read_unsigned_big_endian(proof_node.prefix_length_and_bv.data(), prefix_length);
	PriceUtils::read_unsigned_big_endian(proof_node.prefix_length_and_bv.data() + 2, bv);

	if (prefix_length != MAX_KEY_LEN_BITS.len || bv != 0) {
		return false;
	}

	int header_bytes = get_header_bytes(MAX_KEY_LEN_BITS);

	int total_len = value_bytes.size() + header_bytes;

	unsigned char* digest_bytes = new unsigned char[total_len];

	write_node_header(digest_bytes, key, MAX_KEY_LEN_BITS);

	memcpy(digest_bytes + header_bytes, value_bytes.data(), value_bytes.size());

	unsigned char hash_buf[32];

	SHA256(digest_bytes, total_len, hash_buf);

	return memcmp(hash_buf, expected_node_hash.data(), 32) == 0;
}

bool 
validate_trie_node_proof(
	const ProofNode& proof_node, 
	const Hash& expected_node_hash, 
	const unsigned char* key, 
	const PrefixLenBits& MAX_KEY_LEN_BITS) {

	PrefixLenBits prefix_length{0};

	uint16_t bv = 0;
	PriceUtils::read_unsigned_big_endian(proof_node.prefix_length_and_bv.data(), prefix_length.len);
	PriceUtils::read_unsigned_big_endian(proof_node.prefix_length_and_bv.data() + 2, bv);

	int num_children = __builtin_popcount(bv);

	//value node
	if (prefix_length == MAX_KEY_LEN_BITS) {
		PROOF_INFO("accidentally checking node proof on a value node");
		return false;
	}

	PROOF_INFO("prefix_len=%u bv=%u, num_children=%d", prefix_length.len, bv, num_children);

	int digest_len = get_header_bytes(prefix_length) + 2 /*for bv*/ + (num_children * 32);

	unsigned char* digest_bytes = new unsigned char[digest_len];
	uint8_t last_byte_mask = (prefix_length.len % 8 == 0)?255:240;

	write_node_header(digest_bytes, key, prefix_length, last_byte_mask);
	int idx = get_header_bytes(prefix_length);

	PriceUtils::write_unsigned_big_endian(digest_bytes + idx, bv);
	idx += 2;

	for (unsigned int i = 0; i < proof_node.hashes.size(); i++) {
		PROOF_INFO("hash %u is %s", i, DebugUtils::__array_to_str(proof_node.hashes[i].data(), 32).c_str());
		memcpy(digest_bytes + idx, proof_node.hashes[i].data(), 32);
		idx += 32;
	}

	unsigned char hash_buf[32];
	SHA256(digest_bytes, digest_len, hash_buf);
	PROOF_INFO("hash input:%s", DebugUtils::__array_to_str(digest_bytes, digest_len).c_str());

	auto result = memcmp(hash_buf, expected_node_hash.data(), 32);
	delete[] digest_bytes;
	return result == 0;
}


bool validate_trie_proof(const Proof& proof, const Hash& top_hash, const uint16_t& _MAX_KEY_LEN_BITS) {

	PrefixLenBits MAX_KEY_LEN_BITS{_MAX_KEY_LEN_BITS};

	PROOF_INFO("starting proof check");
	if (!validate_trie_root_proof(proof, top_hash)) return false;
	PROOF_INFO("root proof success");

	if (proof.prefix.size() != MAX_KEY_LEN_BITS.num_prefix_bytes()) return false;

	PROOF_INFO("prefix present, number of nodes is %u", proof.nodes.size());

	const Hash* parent_hash = &proof.root_node_hash;

	for (unsigned int i = 0; i < proof.nodes.size() - 1; i++) {
		const ProofNode& node = proof.nodes[i];
		if (!validate_trie_node_proof(node, *parent_hash, proof.prefix.data(), MAX_KEY_LEN_BITS)) return false;
		PROOF_INFO("checked node %u", i);
		parent_hash = &node.hashes[get_expected_hash_idx(node, proof.prefix.data())];
	}

	if (proof.membership_flag) {
		PROOF_INFO("checking membership");
		return validate_trie_leaf_proof(proof.nodes.back(), proof.value_bytes, *parent_hash, proof.prefix.data(), MAX_KEY_LEN_BITS);
	} else {
		PROOF_INFO("checking nonmembership");
		return validate_trie_node_proof(proof.nodes.back(), *parent_hash, proof.prefix.data(), MAX_KEY_LEN_BITS);
	}
}


}