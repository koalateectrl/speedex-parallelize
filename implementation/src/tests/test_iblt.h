#include <cxxtest/TestSuite.h>
#include "iblt.h"
#include "xdr/iblt_wire.h"

#include <cstdint>

#include "simple_debug.h"

using namespace edce;

class IBLTTestSuite : public CxxTest::TestSuite {

public:

	using default_iblt_t = IBLT<4, 32, 100, 3>; 
	void test_recover_insertions() {
		TEST_START();
		default_iblt_t iblt;

		std::set<uint32_t> expected;

		for (uint32_t i = 0; i < 50; i++) {
			iblt.insert_key((unsigned char*)&i);
			expected.insert(i);
		}

		default_iblt_t::key_list_t pos_key_list, neg_key_list;

		TS_ASSERT(iblt.decode(pos_key_list, neg_key_list));

		TS_ASSERT_EQUALS(pos_key_list.size(), 50);
		TS_ASSERT_EQUALS(neg_key_list.size(), 0);

		for (unsigned int i = 0; i < pos_key_list.size(); i++) {
			uint32_t key = *((uint32_t*)(pos_key_list[i].data()));
			TS_ASSERT(expected.find(key) != expected.end());
		}
	}

	void test_serialize() {
		TEST_START();
		default_iblt_t iblt;

		std::set<uint32_t> expected;

		for (uint32_t i = 0; i < 50; i++) {
			iblt.insert_key((unsigned char*)&i);
			expected.insert(i);
		}

		auto wire = iblt.serialize();

		default_iblt_t neutral, subtraction_result;

		neutral.compute_difference(wire, subtraction_result);

		default_iblt_t::key_list_t pos_key_list, neg_key_list;

		TS_ASSERT(subtraction_result.decode(pos_key_list, neg_key_list));

		TS_ASSERT_EQUALS(pos_key_list.size(), 0);
		TS_ASSERT_EQUALS(neg_key_list.size(), 50);

		for (unsigned int i = 0; i < neg_key_list.size(); i++) {
			uint32_t key = *((uint32_t*)(neg_key_list[i].data()));
			TS_ASSERT(expected.find(key) != expected.end());
		}
	}

	void test_difference() {
		TEST_START();
		
		default_iblt_t iblt_pos, iblt_neg;

		std::set<uint32_t> expected_pos, expected_neg;

		//for (uint32_t i = 100; i < 500; i++) {
		//	iblt_pos.insert_key((unsigned char*)&i);
	//	iblt_neg.insert_key((unsigned char*)&i);
	//	}

		unsigned int sz = 48;

		for (uint32_t i = 500; i < 500+sz; i++) {
			iblt_pos.insert_key((unsigned char*)&i);
			expected_pos.insert(i);
		}
		for (uint32_t i = 600; i < 600+sz; i ++) {
			iblt_neg.insert_key((unsigned char*)&i);
			expected_neg.insert(i);
		}

		auto wire = iblt_neg.serialize();

		default_iblt_t diff;

		iblt_pos.compute_difference(wire, diff);

		default_iblt_t::key_list_t pos_key_list, neg_key_list;

		TS_ASSERT(diff.decode(pos_key_list, neg_key_list));

		TS_ASSERT_EQUALS(pos_key_list.size(), sz);
		TS_ASSERT_EQUALS(neg_key_list.size(), sz);

		for (unsigned int i = 0; i < pos_key_list.size(); i++) {
			uint32_t key = *((uint32_t*)(pos_key_list[i].data()));
			TS_ASSERT(expected_pos.find(key) != expected_pos.end());
		}
		for (unsigned int i = 0; i < neg_key_list.size(); i++) {
			uint32_t key = *((uint32_t*)(neg_key_list[i].data()));
			TS_ASSERT(expected_neg.find(key) != expected_neg.end());
		}
	}

};
