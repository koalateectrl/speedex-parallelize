#include <cxxtest/TestSuite.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "account_merkle_trie.h"

#include "xdr/transaction.h"

#include "simple_debug.h"

#include <openssl/sha.h>

#include <iostream>

#include "price_utils.h"
#include "xdr/types.h"

using namespace edce;

class AccountMerkleTrieTestSuite : public CxxTest::TestSuite {
public:

	void test_prefix_branch_bits() {
		AccountID query = 0x1234567800000000;

		AccountIDPrefix key_buf {query};

		//PriceUtils::write_unsigned_big_endian(key_buf, query);
		//std::printf("%s\n", key_buf.to_string(key_buf.len()).c_str());

		TS_ASSERT_EQUALS(key_buf.get_branch_bits(PrefixLenBits{0}), 0x1);
		TS_ASSERT_EQUALS(key_buf.get_branch_bits(PrefixLenBits{4}), 0x2);
		TS_ASSERT_EQUALS(key_buf.get_branch_bits(PrefixLenBits{8}), 0x3);
		TS_ASSERT_EQUALS(key_buf.get_branch_bits(PrefixLenBits{12}), 0x4);
		TS_ASSERT_EQUALS(key_buf.get_branch_bits(PrefixLenBits{16}), 0x5);
		TS_ASSERT_EQUALS(key_buf.get_branch_bits(PrefixLenBits{20}), 0x6);
		TS_ASSERT_EQUALS(key_buf.get_branch_bits(PrefixLenBits{24}), 0x7);
		TS_ASSERT_EQUALS(key_buf.get_branch_bits(PrefixLenBits{28}), 0x8);
	}

	void test_prefix_match_len() {
		uint64_t query = 0xF000'0000;

		AccountIDPrefix key_buf{query};

		//PriceUtils::write_unsigned_big_endian(key_buf, query);

		uint64_t query2 = 0xFF00'0000;
		AccountIDPrefix key_buf2{query2};
		//PriceUtils::write_unsigned_big_endian(key_buf2, query2);

		TS_ASSERT_EQUALS(key_buf.get_prefix_match_len(PrefixLenBits{64}, key_buf2, PrefixLenBits{64}), PrefixLenBits{36});
	}


	void test_truncate() {
		uint64_t query = 0x12345678;
		AccountIDPrefix key_buf{query};

		uint64_t truncated = 0x12340000;
		AccountIDPrefix key_buf2{truncated};

		key_buf.truncate(PrefixLenBits{48});

		TS_ASSERT_EQUALS(key_buf, key_buf2);

		truncated = 0x12300000;
		key_buf2 = AccountIDPrefix{truncated};
		key_buf.truncate(PrefixLenBits{44});
		TS_ASSERT_EQUALS(key_buf, key_buf2);
	}


	void test_insert() {
		TEST_START();
		AccountTrie<EmptyValue> trie;

		auto serial_trie = trie.open_serial_subsidiary();

	//	unsigned char input_data[32];

		//serial_trie.log();
		for (uint64_t i = 0; i < 300; i+=11) {
			//std::array<unsigned char, 32> buf;
			//SHA256(&i, 1, buf.data());
			//for (int i = 0; i < 32; i++) {
			//	std::cout<<std::hex<<(int)input_data[i]<<" ";
			//}
			//std::cout<<std::endl;
			//key_buf = AccountIDPrefix{i};
			serial_trie.insert(i, EmptyValue{});
			//std::printf("starting logging %lu \n", i);
			//serial_trie.log();
		}

		TS_ASSERT_EQUALS(28, serial_trie.size());

		trie.merge_in(serial_trie);
		TS_ASSERT_EQUALS(0, serial_trie.size());
		TS_ASSERT_EQUALS(28, trie.size());
	}

	void test_hash() {
		TEST_START();
		AccountTrie<EmptyValue> trie;
		auto serial_trie = trie.open_serial_subsidiary();

		Hash hash1, hash2;

//		unsigned char hash1[32];
//		unsigned char hash2[32];

		for (uint64_t i = 0; i < 1000; i+= 20) {
			//PriceUtils::write_unsigned_big_endian(key_buf, i);
			serial_trie.insert(i, EmptyValue{});
		}
		TS_ASSERT_EQUALS(50, serial_trie.size());
		trie.merge_in(serial_trie);
		TS_ASSERT_EQUALS(50, trie.size());

		trie.hash(hash1);
		
		for (uint64_t i = 0; i < 1000; i+= 20) {
			serial_trie.insert(i, EmptyValue{});
		}
		TS_ASSERT_EQUALS(50, serial_trie.size());
		//serial_trie.log();
		trie.merge_in(serial_trie);
		TS_ASSERT_EQUALS(50, trie.size())
		trie.hash(hash2);

		TS_ASSERT_EQUALS(0, memcmp(hash1.data(), hash2.data(), 32));

		serial_trie.insert(125, EmptyValue{});
		trie.merge_in(serial_trie);
		TS_ASSERT_EQUALS(51, trie.size());
		trie.hash(hash2);
		TS_ASSERT_DIFFERS(0, memcmp(hash1.data(), hash2.data(), 32));
	}

	void test_hash_not_just_size() {
		TEST_START();
		AccountTrie<EmptyValue> trie, trie2;
		auto serial_trie = trie.open_serial_subsidiary();
		auto serial_trie2 = trie2.open_serial_subsidiary();

		Hash hash1, hash2;

		for (uint64_t i = 0; i < 1000; i+= 20) {
			//PriceUtils::write_unsigned_big_endian(key_buf, i);
			serial_trie.insert(i, EmptyValue{});
			serial_trie2.insert(i, EmptyValue{});
		}
		TS_ASSERT_EQUALS(50, serial_trie.size());
		trie.merge_in(serial_trie);
		TS_ASSERT_EQUALS(50, trie.size());
		trie2.merge_in(serial_trie2);

		trie.hash(hash1);
		trie2.hash(hash2);
		
		TS_ASSERT_EQUALS(0, memcmp(hash1.data(), hash2.data(), 32));

		serial_trie.insert(125, EmptyValue{});
		serial_trie2.insert(126, EmptyValue{});
		trie.merge_in(serial_trie);
		trie2.merge_in(serial_trie2);
		TS_ASSERT_EQUALS(51, trie.size());
		TS_ASSERT_EQUALS(51, trie2.size());
		trie.hash(hash1);
		trie2.hash(hash2);
		TS_ASSERT_DIFFERS(0, memcmp(hash1.data(), hash2.data(), 32));
	}

	void check_equality(AccountTrie<EmptyValue>& t1, AccountTrie<EmptyValue>& t2) {
		Hash hash1, hash2;

		t1.hash(hash1);
		t2.hash(hash2);
		TS_ASSERT_EQUALS(0, memcmp(hash1.data(), hash2.data(), 32));
		TS_ASSERT_EQUALS(t1.size(), t2.size());
	}

	void test_merge_some_shared_keys() {
		TEST_START();
		AccountTrie<EmptyValue> trie;
		auto trie_builder = trie.open_serial_subsidiary();
		auto mergein = trie.open_serial_subsidiary();
		AccountTrie<EmptyValue> expect;
		auto expect_builder = expect.open_serial_subsidiary();



		uint64_t key = 0xFF00'0000'0000'0000;
		trie_builder.insert(key, EmptyValue{});
		mergein.insert(key, EmptyValue{});
		expect_builder.insert(key, EmptyValue{});		

		//Full match (case 0)
		trie.merge_in(trie_builder);
		expect.merge_in(expect_builder);
		trie.merge_in(mergein);
		check_equality(trie, expect);

		// a branch (case 4)
		key = 0xF000'0000'0000'0000;
		//key[0] = 0xF0;
		mergein.insert(key, EmptyValue{});
		expect_builder.insert(key, EmptyValue{});

		expect.merge_in(expect_builder);
		trie.merge_in(mergein);

		check_equality(trie, expect);

		//trie._log(std::string("TRIE:  "));
		//expect._log(std::string("EXPC:  "));
		// case 2
		key = 0xF100'0000'0000'0000;
		mergein.insert(key, EmptyValue{});
		expect_builder.insert(key, EmptyValue{});

		trie.merge_in(mergein);
		expect.merge_in(expect_builder);
		check_equality(trie, expect);

		//trie._log(std::string("TRIE:  "));
		//expect._log(std::string("EXPC:  "));
		// case 3
		key = 0xA000'0000'0000'0000;
		mergein.insert(key, EmptyValue{});
		expect_builder.insert(key, EmptyValue{});
		key = 0xA100'0000'0000'0000;
		mergein.insert(key, EmptyValue{});
		expect_builder.insert(key, EmptyValue{});
		key = 0xA200'0000'0000'0000;
		mergein.insert(key, EmptyValue{});
		expect_builder.insert(key, EmptyValue{});

		trie.merge_in(mergein);
		expect.merge_in(expect_builder);
		check_equality(trie, expect);

		//trie._log(std::string("TRIE:  "));
		//expect._log(std::string("EXPC:  "));
		//case 1
		key = 0xA100'0000'0000'0000;
		mergein.insert(key, EmptyValue{});
		expect_builder.insert(key, EmptyValue{});

		key = 0xA300'0000'0000'0000;
		mergein.insert(key, EmptyValue{});
		expect_builder.insert(key, EmptyValue{});

		trie.merge_in(mergein);
		expect.merge_in(expect_builder);
		check_equality(trie, expect);
	}

};
