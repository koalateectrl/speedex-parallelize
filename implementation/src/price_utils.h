#pragma once

#include <cmath>

#include "xdr/types.h"

#include "simple_debug.h"
#include <concepts>

namespace edce {

typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;

class PriceUtils {
	constexpr static uint64_t low_mask = 0x00000000FFFFFFFF;
	constexpr static uint64_t high_mask = 0xFFFFFFFF00000000;

public:

	constexpr static uint8_t PRICE_RADIX = 24;
	constexpr static uint8_t PRICE_BIT_LEN = 2 * PRICE_RADIX;
	constexpr static uint8_t PRICE_BYTES = (PRICE_BIT_LEN) / 8;

	constexpr static Price MAX_PRICE = (((uint64_t) 1) << (PRICE_BIT_LEN)) - 1; // 0xffffff.ffffff

	static_assert(PRICE_RADIX % 4 == 0, 
		"be warned that making price len a fractional number of bytes makes working with prices harder");

	constexpr static Price PRICE_ONE = ((uint64_t)1)<<PRICE_RADIX;

	constexpr static Price PRICE_MAX = (PRICE_ONE<<PRICE_RADIX) - 1;


	PriceUtils() = delete;

	static double to_double(const Price& price) {
		return (double)price / (double) ((uint64_t)1<<PRICE_RADIX);
	}

	static Price from_double(const double price_d) {
		return (uint64_t)(price_d * (((uint64_t)1)<<PRICE_RADIX));
	}

	static double amount_to_double(const uint128_t& value, unsigned int radix = 0) {
		uint64_t top = value >> 64;
		uint64_t bot = value & UINT64_MAX;
		return ((((double) top * (((double) (UINT64_MAX)) + 1)) + ((double) bot))) / (double) (((uint128_t)1)<<radix);
	}

	static double tax_to_double(const uint8_t tax_rate) {
		double tax_d = tax_rate;
		return (1.0) - std::exp2f(-tax_d);
	}


	template<typename ArrayType>
	static void write_price_big_endian(ArrayType& buf, const Price& price) {

		for (uint8_t loc = 0; loc < PRICE_BYTES; loc++) {
			//uint64_t mask = (((uint64_t)0xFF) << ((PRICE_BYTES - loc) * 8));
			uint8_t offset = ((PRICE_BYTES - loc - 1) * 8);
			//INFO("mask: %lx, price: %lx, ", mask, price);
			buf.at(loc) = (price >> offset) & 0xFF;
			//buf[loc] = price & (((uint64_t)0xFF) << ((PRICE_BYTES - loc) * 8));
		}
		//buf[0] is highest bits, etc;
	}

	static void write_price_big_endian(unsigned char* buf, const Price& price) {
		for (uint8_t loc = 0; loc < PRICE_BYTES; loc++) {
			//uint64_t mask = (((uint64_t)0xFF) << ((PRICE_BYTES - loc) * 8));
			uint8_t offset = ((PRICE_BYTES - loc - 1) * 8);
			//INFO("mask: %lx, price: %lx, ", mask, price);
			buf[loc] = (price >> offset) & 0xFF;
			//buf[loc] = price & (((uint64_t)0xFF) << ((PRICE_BYTES - loc) * 8));
		}
		//buf[0] is highest bits, etc;
	}

	static Price impose_bounds(const uint128_t& val) {
		if (val > PRICE_MAX) {
			//std::printf("%lu %f\n", PRICE_MAX, PriceUtils::to_double(PRICE_MAX));
			return PRICE_MAX;
		}
		if (val == 0) {
			return 1;
		}
		return val;
	}

	static bool is_valid_price(const Price& price) {
		return price <= PRICE_MAX && price != 0;
	}

	template<size_t ARRAY_LEN>
	static Price read_price_big_endian(const std::array<unsigned char, ARRAY_LEN>& buf) {
		static_assert(ARRAY_LEN >= PRICE_BYTES, "not enough bytes to read price");
		return read_price_big_endian(buf.data());
	}

	static Price read_price_big_endian(const unsigned char* buf) {
		Price p = 0;
		for (uint8_t loc = 0; loc < PRICE_BYTES; loc++) {
			p <<=8;
			p += buf[loc];
		}
		return p;
	}

	template<typename ArrayType>
	static Price read_price_big_endian(const ArrayType& buf) {
		Price p = 0;
		for (uint8_t loc = 0; loc < PRICE_BYTES; loc++) {
			p <<= 8;
			p += buf[loc];
		}
		return p;
	}

	template<typename array, typename T, typename std::enable_if<std::is_unsigned<T>::value, T>::type = 0>
	static void write_unsigned_big_endian(array& buf, const T& value, const size_t offset = 0) {
		constexpr size_t sz = sizeof(T);
		constexpr size_t buf_sz = sizeof(buf);//.size();

		static_assert((sz-1)*8 <= UINT8_MAX, "if this happens we need to account for overflows on mask shift");
		static_assert(sz <= buf_sz, "insufficient buffer size!");

		for (uint8_t loc = 0; loc < sz; loc++) {
			uint8_t shift = ((sz - loc - 1) * 8);
			uint8_t byte = (((value>>shift) & 0xFF));
			buf.at(loc + offset) =  byte;
//			buf.at(loc) = (((value>>offset) & 0xFF));
			//buf[loc] = (unsigned char) ((value>>offset) & 0xFF);
			//buf[loc] = value & (((uint64_t)0xFF) << ((sz - loc) * 8));
		}
	}

	template<typename T, typename std::enable_if<std::is_unsigned<T>::value, T>::type = 0>
	static void append_unsigned_big_endian(std::vector<unsigned char>& buf, const T& value) {
		constexpr size_t sz = sizeof(T);

		static_assert((sz-1)*8 <= UINT8_MAX, "if this happens we need to account for overflows on mask shift");
		for (uint8_t loc = 0; loc < sz; loc++) {
			uint8_t offset = ((sz - loc - 1) * 8);
			buf.push_back(((value>>offset) & 0xFF));
			//buf[loc] = (unsigned char) ((value>>offset) & 0xFF);
			//buf[loc] = value & (((uint64_t)0xFF) << ((sz - loc) * 8));
		}
	}

	template<typename T, typename std::enable_if<std::is_unsigned<T>::value, T>::type = 0>
	static void write_unsigned_big_endian(unsigned char* buf, const T& value) {
		constexpr size_t sz = sizeof(T);

		static_assert((sz-1)*8 <= UINT8_MAX, "if this happens we need to account for overflows on mask shift");
		for (uint8_t loc = 0; loc < sz; loc++) {
			uint8_t offset = ((sz - loc - 1) * 8);
			buf[loc] = (unsigned char) ((value>>offset) & 0xFF);
			//buf[loc] = value & (((uint64_t)0xFF) << ((sz - loc) * 8));
		}
	}

	/*template<typename T, typename std::enable_if<std::is_unsigned<T>::value, T>::type = 0>
	static void write_unsigned_big_endian(unsigned char* buf, const T& value, int& idx) {
		write_unsigned_big_endian(buf + idx, value);
		idx += sizeof(T);
	}*/

	template<typename T, typename std::enable_if<std::is_unsigned<T>::value, T>::type = 0>
	static void read_unsigned_big_endian(const unsigned char* buf, T& output) {
		constexpr size_t sz = sizeof(T);
		output = 0;
		for (uint8_t loc = 0; loc < sz; loc++) {
			output<<=8;
			output+=buf[loc];
		}
	}

	template<typename T, size_t ARRAY_LEN>
	static void read_unsigned_big_endian(const std::array<unsigned char, ARRAY_LEN>& buf, T& output) {
		static_assert(sizeof(T) <= ARRAY_LEN, "not enough bytes to read");
		read_unsigned_big_endian(buf.data(), output);
	}

	template<typename ArrayLike, std::unsigned_integral T>
	static void read_unsigned_big_endian(const ArrayLike& buf, T& output) {
		constexpr size_t sz = sizeof(T);
		output = 0;
		for (uint8_t loc = 0; loc < sz; loc++) {
			output<<=8;
			output+=buf[loc];
		}
	}

	/*
	To save myself the trouble of rethinking this through all the time:

	Prices are set to be "sell my asset if the price of my asset is at least this given price"

	That is, I want to sell if i.e. the price of the thing i'm selling is at least 3x the price of what I'm buying.

	That means that my utility for the thing i'm buying is 1/3.
	*/
	static double log_utility(const Price& price) {
		return -std::log(to_double(price));
		//return -(std::log(((double)price)/(double) ((uint64_t)1 <<32)));
	}

	static double utility(const Price& price) {
		return 1.0/to_double(price);
	}

	static Price smooth_mult(const Price& price, const uint8_t smooth_mult) {
		return (price - (price>>smooth_mult));
		//subtract 1/2^smooth_mult
	}

	static bool a_over_b_leq_c(const Price& a, const Price& b, const Price& c) {
		return (((uint128_t)a)<<PRICE_RADIX) <= ((uint128_t) b) * ((uint128_t) c);

		//uint64_t top_bits = ((high_mask & b)>>32) * ((high_mask & c)>>32);
		//uint64_t mid_bits = ((high_mask & b)>>32) * ((low_mask & c)) + ((low_mask & b)) * ((high_mask & c)>>32);
		//uint64_t low_bits = (low_mask & b) * (low_mask & c);
		//return ((top_bits & high_mask) != 0) || (a <= (top_bits << 32) + mid_bits + (low_bits>>32));
	}

	static bool a_over_b_lt_c(const Price& a, const Price& b, const Price &c) {
		return (((uint128_t)a)<<PRICE_RADIX) < ((uint128_t) b) * ((uint128_t) c);
	}

	static uint128_t wide_multiply_val_by_a_over_b(const uint128_t value, const Price& a, const Price& b) {
		uint128_t denom = b;
		uint128_t numer = a;
		uint128_t modulo = (value/denom) * numer;
		uint128_t remainder = ((value % denom) * numer) / denom;
		return modulo + remainder;
	}


	//it's not 100% accurate - there's some carries that get lost, but ah well.
	static Price 
	safe_multiply_and_drop_lowbits(const uint128_t& first, const uint128_t& second, const uint64_t& lowbits_to_drop) {

		if (lowbits_to_drop < 64 || lowbits_to_drop > 196) {
			throw std::runtime_error("unimplemented");
		}

		uint128_t first_low = first & UINT64_MAX;
		uint128_t first_high = (first >> 64) & UINT64_MAX;
		uint128_t second_low = second & UINT64_MAX;
		uint128_t second_high = (second >> 64) & UINT64_MAX;

		uint128_t low_low = first_low * second_low;

		uint128_t low_high = first_low * second_high;
		uint128_t high_low = first_high * second_low;
		uint128_t high_high = first_high * second_high;

		uint128_t out = 0;

		//constexpr uint128_t price_mask = ((uint128_t) MAX_PRICE);
		if (lowbits_to_drop < 128) {
			out += (low_low >> lowbits_to_drop);
		}
		//std::printf("out lowlow: %lx\n", ((uint64_t) (out & UINT64_MAX)));

		uint64_t lowhigh_offset = lowbits_to_drop - 64;

		out += (low_high >> lowhigh_offset) + (high_low >> lowhigh_offset);

		//std::printf("out lowhigh: %lx\n", ((uint64_t) (out & UINT64_MAX)));

		if (lowbits_to_drop <= 128) {

			uint64_t used_lowbits = 128-lowbits_to_drop;
			uint64_t used_highbits = (used_lowbits <= PRICE_BIT_LEN) ? PRICE_BIT_LEN - used_lowbits : 0;

			//std::printf("used highbits: %lu\n", used_highbits);
			//std::printf("used lowbits: %lu\n", used_lowbits);

			uint64_t max_highbits = (((uint64_t)1) << used_highbits) - 1;
			
			//std::printf("high_high %lx\n", ((uint64_t) (high_high & UINT64_MAX)));

			if (high_high > max_highbits) {
				high_high = max_highbits;
			}

			//std::printf("max_highbits %lx\n", max_highbits);
			//std::printf("capped high_high %lx\n",((uint64_t) (high_high & UINT64_MAX)));

			out += (high_high << (used_lowbits));

			if (out > PRICE_MAX) {
				out = PRICE_MAX;
			}
		} else {
			uint64_t highbits_offset = lowbits_to_drop - 128;

			out += (high_high >> highbits_offset);

			if (out > PRICE_MAX) {
				out = PRICE_MAX;
			}

		}
		return out;
	}
};

}
