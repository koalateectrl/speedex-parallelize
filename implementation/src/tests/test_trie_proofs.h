#include <cxxtest/TestSuite.h>

#include "xdr/trie_proof.h"
#include "xdr/types.h"
#include "merkle_trie.h"

#include "price_utils.h"

#include "proof_utils.h"


#include "simple_debug.h"

using namespace edce;

class TrieProofTestSuite : public CxxTest::TestSuite {

public:
	void test_membership() {
		TEST_START();

		MerkleTrie<1> trie;
		MerkleTrie<1>::prefix_t prefix;
		for (unsigned char i = 0; i < 100; i += 10) {
			prefix[0] = i;
			trie.insert(prefix);
		}
		TS_ASSERT_EQUALS(10, trie.uncached_size());

		Hash h;
		
		trie.freeze_and_hash(h);
		
		MerkleTrie<1>::FrozenT frozen_trie = trie.destructive_freeze();


		PROOF_INFO_F(frozen_trie._log("frozen trie:  "));

		unsigned char key = 20;
		prefix[0] = key;
		auto proof = frozen_trie.generate_proof(prefix);

		TS_ASSERT(validate_trie_proof(proof, h, 8));
		TS_ASSERT(proof.membership_flag);
	}

	void test_nonmembership() {
		TEST_START();
		MerkleTrie<2> trie;

		MerkleTrie<2>::prefix_t key_buf;
		for (uint16_t i = 0; i < 1000; i+=50) {
			PriceUtils::write_unsigned_big_endian(key_buf, i);
			trie.insert(key_buf);
		}

		TS_ASSERT_EQUALS(20, trie.uncached_size());

		Hash h;

		trie.freeze_and_hash(h);
		MerkleTrie<2>::FrozenT frozen_trie = trie.destructive_freeze();

		uint16_t key = 125;

		PriceUtils::write_unsigned_big_endian(key_buf, key);
		//unsigned char key_buf[2];

		//memcpy(key_buf, &key, 2);

		auto proof = frozen_trie.generate_proof(key_buf);
		TS_ASSERT(validate_trie_proof(proof, h, 16));
		TS_ASSERT(!proof.membership_flag);
	}

	void test_bad_proof_membership() {
		TEST_START();
		MerkleTrie<2> trie;
		MerkleTrie<2>::prefix_t key_buf;

		for (uint16_t i = 0; i < 1000; i+=50) {
			PriceUtils::write_unsigned_big_endian(key_buf, i);
			trie.insert(key_buf);
		}

		TS_ASSERT_EQUALS(20, trie.uncached_size());

		Hash h;

		trie.freeze_and_hash(h);
		MerkleTrie<2>::FrozenT frozen_trie = trie.destructive_freeze();

		uint16_t key = 123;
		PriceUtils::write_unsigned_big_endian(key_buf, key);

		//unsigned char key_buf[2];
		//memcpy(key_buf, &key, 2);


		auto proof = frozen_trie.generate_proof(key_buf);
		TS_ASSERT(validate_trie_proof(proof, h, 16));

		proof.membership_flag = 1;
		TS_ASSERT(!validate_trie_proof(proof, h, 16));

		proof.membership_flag = 0;

		key = 100;

		PriceUtils::write_unsigned_big_endian(key_buf, key);

		proof = frozen_trie.generate_proof(key_buf);

		uint16_t key_bad = 124;

		proof.prefix.clear();
		proof.prefix.insert(proof.prefix.end(), (unsigned char*)&key_bad, (unsigned char*)&key_bad + 2);

		TS_ASSERT(!validate_trie_proof(proof, h, 16));
	}

	void test_membership_proof_modification() {
		TEST_START();
		MerkleTrie<2> trie;
		MerkleTrie<2>::prefix_t key_buf;

		for (uint16_t i = 0; i < 1000; i+=50) {
			PriceUtils::write_unsigned_big_endian(key_buf, i);
			trie.insert(key_buf);
		}
		TS_ASSERT_EQUALS(20, trie.uncached_size());

		Hash h;
		trie.freeze_and_hash(h);
		MerkleTrie<2>::FrozenT frozen_trie = trie.destructive_freeze();

		uint16_t key = 100;

		PriceUtils::write_unsigned_big_endian(key_buf, key);



		auto proof = frozen_trie.generate_proof(key_buf);
		TS_ASSERT(validate_trie_proof(proof, h, 16));

		TS_ASSERT(proof.membership_flag);

		proof.membership_flag = 0;
		TS_ASSERT(!validate_trie_proof(proof, h, 16));

		proof.membership_flag = 1;


		uint16_t key_bad = 127;

		proof.prefix.clear();
		proof.prefix.insert(proof.prefix.end(), (unsigned char*)&key_bad, (unsigned char*)&key_bad + 2);

		TS_ASSERT(!validate_trie_proof(proof, h, 16));

		proof.membership_flag = 0;

		TS_ASSERT(!validate_trie_proof(proof, h, 16));


	}
};
