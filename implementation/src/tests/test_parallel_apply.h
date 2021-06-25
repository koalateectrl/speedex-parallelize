#include <cxxtest/TestSuite.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "merkle_trie.h"
#include "merkle_work_unit.h"

#include "xdr/transaction.h"
#include "xdr/types.h"

#include "simple_debug.h"

#include "price_utils.h"

using namespace edce;
struct IntWrapper {
	uint32_t value;
	uint32_t operator++(int) {
		value++;
		return value;
	}

	IntWrapper(uint32_t val = 0) : value(val) {}

	bool operator==(IntWrapper other) const {
		return other.value == value;
	}

	void serialize() const {};

	void copy_data(std::vector<unsigned char>& buf) const {}
};


using Value = IntWrapper;

class ParallelApplyTestSuite : public CxxTest::TestSuite {

public:

	struct ApplyFunc {
		void operator() (Value& val) {
			val++;
		}
	};

	void test_parallel_apply() {
		TEST_START();
		MerkleTrie<4, Value> trie;
		MerkleTrie<4, Value>::prefix_t key_buf;

		uint32_t test_size = 1000000;

		for (uint32_t i = 0; i < test_size; i++) {
			PriceUtils::write_unsigned_big_endian(key_buf, i);
			trie.template insert(key_buf, (Value) 0);
		}

		TS_ASSERT_EQUALS(trie.size(), test_size);
		ApplyFunc func{};
		trie.parallel_apply(func);

		for (uint32_t i = 0; i < test_size; i++) {
			PriceUtils::write_unsigned_big_endian(key_buf, i);
			auto res = trie.get_value(key_buf);
			TS_ASSERT(res);
			TS_ASSERT_EQUALS(*res, 1);
		}
	}

	void test_parallel_accumulate_values() {
		TEST_START();
		MerkleTrie<4, Value> trie;
		MerkleTrie<4, Value>::prefix_t key_buf;

		uint32_t test_size = 1000000;

		for (uint32_t i = 0; i < test_size; i++) {
			PriceUtils::write_unsigned_big_endian(key_buf, i);
			trie.insert(key_buf, Value(i));
		}

		TS_ASSERT_EQUALS(trie.size(), test_size);
		auto res = trie.template accumulate_values_parallel<std::vector<Value>>();

		for (uint32_t i = 0; i < test_size; i++) {
			TS_ASSERT_EQUALS(res.at(i), i);
		}
	}

};

