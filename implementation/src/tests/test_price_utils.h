#include <cxxtest/TestSuite.h>

#include <cstdint>
#include <cstdio>

#include "price_utils.h"
#include "simple_debug.h"

#include "xdr/transaction.h"

using namespace edce;

class PriceUtilsTestSuite : public CxxTest::TestSuite {

public:

	void test_write_value_big_endian() {
		TEST_START();
		uint64_t value = 0;
		unsigned char buf[8];
		unsigned char expect[8];

		memset(expect, 0, 8);

		value = 1;
		expect[7] = 1;
		PriceUtils::write_unsigned_big_endian(buf, value);
		TS_ASSERT_EQUALS(0, memcmp(buf, expect, 8));
		memset(buf, 0, 8);

		value = (((uint64_t)39) << 24)
			  + (((uint64_t)139) << 56);
		memset(expect, 0, 8);
		expect[0] = 139;
		expect[4] = 39;

		PriceUtils::write_unsigned_big_endian(buf, value);
		TS_ASSERT_EQUALS(0, memcmp(buf, expect, 8));
	}

	void test_write_price_big_endian() {
		TEST_START();
		Price value = 0;
		unsigned char buf[PriceUtils::PRICE_BYTES];
		unsigned char expect[PriceUtils::PRICE_BYTES];

		memset(expect, 0, PriceUtils::PRICE_BYTES);

		value = 1;
		expect[PriceUtils::PRICE_BYTES-1] = 1;
		PriceUtils::write_price_big_endian(buf, value);
		TS_ASSERT_EQUALS(0, memcmp(buf, expect, PriceUtils::PRICE_BYTES));
		memset(buf, 0, PriceUtils::PRICE_BYTES);

		value = (((uint64_t)39) << 16)
			  + (((uint64_t)139) << 40);
		memset(expect, 0, PriceUtils::PRICE_BYTES);
		expect[0] = 139;
		expect[3] = 39;

		PriceUtils::write_price_big_endian(buf, value);
		TS_ASSERT_EQUALS(0, memcmp(buf, expect, PriceUtils::PRICE_BYTES));
	}

	void test_readwrite_price() {
		TEST_START();
		Price value = 0;
		unsigned char buf[PriceUtils::PRICE_BYTES];

		PriceUtils::write_price_big_endian(buf, value);
		TS_ASSERT_EQUALS(value, PriceUtils::read_price_big_endian(buf));

		value = 123456789;

		PriceUtils::write_price_big_endian(buf, value);
		TS_ASSERT_EQUALS(value, PriceUtils::read_price_big_endian(buf));
	}

	void test_readwrite_unsigned() {
		TEST_START();

		uint16_t value = 63;

		unsigned char buf[2];
		PriceUtils::write_unsigned_big_endian(buf, value);
		uint16_t read_value = 0;
		PriceUtils::read_unsigned_big_endian(buf, read_value);

		TS_ASSERT_EQUALS(value, read_value);
	}

	void test_multiply_a_over_b() {
		TEST_START();
		
		Price a = 1, b = 2;
		uint128_t value = 1;

		TS_ASSERT_EQUALS(1, PriceUtils::wide_multiply_val_by_a_over_b(value, a, a));
		TS_ASSERT_EQUALS(2, PriceUtils::wide_multiply_val_by_a_over_b(value, b, a));

		TS_ASSERT_EQUALS(800, PriceUtils::wide_multiply_val_by_a_over_b(100, 800, 100));



		TS_ASSERT_EQUALS(1600, PriceUtils::wide_multiply_val_by_a_over_b(200, 800, 100));

		value = ((uint128_t)1)<<65;
		uint128_t result = value * 8;

		TS_ASSERT_EQUALS(result, PriceUtils::wide_multiply_val_by_a_over_b(value, 800, 100));

		value = ((uint128_t)1)<<65;
		result = value / 8;

		TS_ASSERT_EQUALS(result, PriceUtils::wide_multiply_val_by_a_over_b(value, 100, 800));


	}

	void test_tat_big_multiply() {
		TEST_START();

		uint128_t a = ((uint128_t) 1) << 55;
		uint128_t b = ((uint128_t) 1) << 55;

		Price c = ((Price) 1) << 20;

		TS_ASSERT_EQUALS(c, PriceUtils::safe_multiply_and_drop_lowbits(a, b, 90));

 		a = ((uint128_t) 1) << 80;
		b = ((uint128_t) 1) << 80;

		c = ((Price) 1) << 35;
		TS_ASSERT_EQUALS(c, PriceUtils::safe_multiply_and_drop_lowbits(a, b, 125));

		a = ((uint128_t) (0xabcd)) << 60;
		b = ((uint128_t) (0xabcd)) << 60;

		c = ((Price) 0x734b8229) << 5;
		TS_ASSERT_EQUALS(c, PriceUtils::safe_multiply_and_drop_lowbits(a, b, 115));

		a = ((uint128_t) 1) << 80;
		b = ((uint128_t) 1) << 80;

		c = ((Price) 1) << 10;
		TS_ASSERT_EQUALS(c, PriceUtils::safe_multiply_and_drop_lowbits(a, b, 150));
	}
};