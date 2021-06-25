#pragma once

#include <cstdint>
#include <iostream>

#include "xdr/types.h"

namespace edce {

class InternalFractionalValue {

	InternalFractionalValue(uint64_t highbits, uint64_t lowbits) : lowbits(lowbits), highbits(highbits) {};

public:

	uint64_t lowbits;
	uint64_t highbits;

	//Value is (highbits<<64 + lowbits)/2^32


	const static uint64_t low_mask = 0x00000000FFFFFFFF;
	const static uint64_t high_mask = 0xFFFFFFFF00000000;

	static InternalFractionalValue from_price(Price p) {
		return InternalFractionalValue(0, p);
	}

	static InternalFractionalValue from_amount(uint64_t amount) {
		return InternalFractionalValue(amount >> 32, amount << 32);
	}

	static InternalFractionalValue zero() {
		return InternalFractionalValue(0, 0);
	}

	const static InternalFractionalValue one() {
	 	const static InternalFractionalValue one(0, ((uint64_t) 1)<<32);
	 	return one;
	}

	InternalFractionalValue one_over_smooth_mult(uint8_t smooth_mult) const {
		uint64_t highbits_new = (highbits<<smooth_mult) | (lowbits >> (64-smooth_mult));
		uint64_t lowbits_new = (lowbits<<smooth_mult);
		return InternalFractionalValue(highbits_new, lowbits_new);
	}

	InternalFractionalValue one_over_value(uint8_t value) const {

		//multiply by 1/2^(value), i.e. 1/epsilon

		uint64_t highbits_new = highbits >> value;
		uint64_t lowbits_new = (lowbits>>value) | (highbits << (64-value));

		return InternalFractionalValue(highbits_new, lowbits_new);
	}

	InternalFractionalValue impose_tax(uint8_t tax_rate) const {
		//multiply by 1-1/(2^tax_rate)
		return (*this) - ((*this).one_over_value(tax_rate));

	}

 	bool leq_price(const Price& price) const {
 		return (!highbits) && (lowbits <= price);
 	}

 	bool lt_price(const Price& price) const {
 		return (!highbits) && (lowbits < price);
 	}


	static int count_zeroes(uint64_t x) {

		#ifdef __GNUC__

		// this takes performance_test_work_unit from ~185 to ~175
		return __builtin_clzll((unsigned long long) x);
		#endif
    	int num_zeroes = 0;
    	while (!(x & 0xff00000000000000)) { 
    		num_zeroes += 8;
    		x <<= 8;
    	}	
    	return num_zeroes;
	}

	//This rounds down, so it should be safe to multiply this by an e.g. sell amount to get a buy amount.

	//Pushing this into a .cc induced a 5% performance hit, as measured by performance_test_work_unit (measured ~206 vs ~196)
	static InternalFractionalValue divide(const Price& top, const Price& bot) {
		uint64_t top_numerator = top;
		uint64_t bot_numerator = bot;
		
		int place_value_count = 32;


/*
		// Doesn't appear to add anything.
		#ifdef __GNUC__
		int reduce = std::min(__builtin_ctzll((unsigned long long) top.numerator), __builtin_ctzll((unsigned long long) bot.numerator));
		top_numerator>>= reduce;
		bot_numerator>>=reduce;
		#endif
		*/

		uint64_t main_bits = top_numerator / bot_numerator;
		uint64_t top_bit = (uint64_t)1 << 63;

		uint64_t remainder = (top_numerator - main_bits * bot_numerator);

		uint64_t highbits = main_bits >> 32;
		uint64_t lowbits = ((main_bits & low_mask) << 32);

		while (place_value_count >0 && remainder) {

			while (remainder < bot_numerator && place_value_count > 0) {

				if (remainder & top_bit) {
					place_value_count--;
					lowbits |= ((uint64_t)1<<place_value_count);
					remainder = (remainder <<1) + (UINT64_MAX & ~bot_numerator) + 1;
				} else {
					int leading_zeroes = std::max(std::min(count_zeroes(remainder), place_value_count), 1);
					remainder <<=leading_zeroes;
					place_value_count-=leading_zeroes;
				}

			}
			uint64_t quotient = remainder / bot_numerator;



			lowbits += (quotient << place_value_count);
			remainder %= bot_numerator;//-= quotient * bot.numerator;
		}
		return InternalFractionalValue(highbits, lowbits);
	}

	InternalFractionalValue drop_lowest_bits(uint8_t bits) {
		// bits < 64

		return InternalFractionalValue(highbits, lowbits & (UINT64_MAX << bits));
	}

	//Drops bits in place values beyond 64
	uint64_t round_up_drop_upper_bits() const {

		return ((highbits<<32) | (lowbits >> 32)) + ((lowbits & low_mask) != 0);
	}

	uint64_t round_down_drop_upper_bits() const {
		return (highbits<<32) | (lowbits >> 32);
	}


	//various math operations
	InternalFractionalValue operator - (const InternalFractionalValue& other) const {

		//requires other < self

		uint64_t highbits_new = highbits - other.highbits - (other.lowbits > lowbits);
		uint64_t lowbits_new = lowbits - other.lowbits;
		return InternalFractionalValue(highbits_new, lowbits_new);
	}

	InternalFractionalValue operator + (const InternalFractionalValue& other) const {
		uint64_t highbits_new = highbits + other.highbits + (UINT64_MAX - lowbits < other.lowbits);
		return InternalFractionalValue(highbits_new, lowbits + other.lowbits);
	}

	bool operator > (const InternalFractionalValue& other) const {
		return (highbits > other.highbits) || (highbits == other.highbits && lowbits > other.lowbits); 
	}
	bool operator >= (const InternalFractionalValue& other) const {
		return (highbits > other.highbits) || (highbits == other.highbits && lowbits >= other.lowbits);
	}

	bool operator <= (const InternalFractionalValue& other) const {
		return ((highbits < other.highbits) || (highbits == other.highbits && lowbits <= other.lowbits));
	}

	bool operator <(const InternalFractionalValue& other) const {
		return ((highbits < other.highbits) || (highbits == other.highbits && lowbits < other.lowbits));
	}

	InternalFractionalValue operator * (const InternalFractionalValue& other) const {
		InternalFractionalValue lowbits_prod = this->mult_by_price(other.lowbits);
		uint64_t highbits_low = highbits & low_mask;
		//uint64_t highbits_high = (highbits & high_mask)>>32;

		uint64_t lowbits_low = lowbits & low_mask;
		uint64_t lowbits_high = (lowbits & high_mask)>>32;

		uint64_t other_highbits_low = other.highbits & low_mask;
		uint64_t other_highbits_high = (other.highbits & high_mask)>>32;

		uint64_t new_pv_1 = other_highbits_low * lowbits_low;
		uint64_t new_highbits = (new_pv_1>>32)
							  + other_highbits_high * lowbits_low
							  + other_highbits_low * lowbits_high
							  + ((other_highbits_high * highbits_low)<<32)
							  + ((other_highbits_low * lowbits_high)<<32);

		return InternalFractionalValue(new_highbits, new_pv_1<<32) + lowbits_prod;
	}

	bool operator == (const InternalFractionalValue& other) const {
		return highbits == other.highbits && lowbits == other.lowbits;
	}

	InternalFractionalValue mult_by_price(const Price& price) const {
		uint64_t highbits_low = highbits & low_mask;
		uint64_t highbits_high = (highbits & high_mask)>>32;

		uint64_t lowbits_low = lowbits & low_mask;
		uint64_t lowbits_high = (lowbits & high_mask)>>32;

		uint64_t price_low = price & low_mask;
		uint64_t price_high = (price & high_mask)>>32;

		uint64_t new_pv_0 = price_low * lowbits_low; // value / 2^64

		uint64_t new_pv_0_1 = price_low * lowbits_high;
		uint64_t new_pv_1_0 = price_high * lowbits_low;

		uint64_t new_pv_1 = new_pv_1_0 + new_pv_0_1; // value / 2^32
		bool pv_1_overlow = (UINT64_MAX - new_pv_0_1 < new_pv_1_0); // 0 or 2^32

		uint64_t new_pv_0_2 = price_low * highbits_low;
		uint64_t new_pv_1_1 = price_high * lowbits_high;

		uint64_t new_pv_2 = new_pv_1_1 + new_pv_0_2; // value
		bool pv_2_overlow = (UINT64_MAX - new_pv_0_2 < new_pv_1_1); // 0 or 2^64

		uint64_t new_pv_0_3 = price_low * highbits_high;
		uint64_t new_pv_1_2 = price_high * highbits_low;

		uint64_t new_pv_3 = new_pv_1_2 + new_pv_0_3; // value * 2^32
		//bool pv_3_overlow = (UINT64_MAX - new_pv_0_3 < new_pv_1_2); // 0 or 2^96

		uint64_t new_pv_4 = price_high * highbits_high; // value * 2^64

		uint64_t lowbits_new_0 = (new_pv_0>>32) + new_pv_1;

		uint64_t lowbits_new_1 = (new_pv_2 & low_mask)<<32;

		bool lowbits_new_overflow = (UINT64_MAX - lowbits_new_1 < lowbits_new_0);

		uint64_t lowbits_new = lowbits_new_0 + lowbits_new_1;

		//TODO check the overflow on this

		// This should be bits between 2^32 and (2^96 - 1)
		uint64_t highbits_new = pv_1_overlow
							  + lowbits_new_overflow
							  + (new_pv_2>>32)
							  + new_pv_3
							  + (((uint64_t) pv_2_overlow)<< 32)
							  + (new_pv_4<<32);

		return InternalFractionalValue(highbits_new, lowbits_new);


	}

	InternalFractionalValue mult_by_uint64_t(const uint64_t amount) const {
		uint64_t highbits_low = highbits & low_mask;
		uint64_t highbits_high = (highbits & high_mask)>>32;

		uint64_t lowbits_low = lowbits & low_mask;
		uint64_t lowbits_high = (lowbits & high_mask)>>32;

		uint64_t amount_low = amount & low_mask;
		uint64_t amount_high = (amount & high_mask)>>32;

		uint64_t new_pv_0_1 = amount_low * lowbits_high;
		uint64_t new_pv_1_0 = amount_high * lowbits_low;

		uint64_t new_pv_1 = new_pv_1_0 + new_pv_0_1;
		uint64_t pv_1_overlow = (UINT64_MAX - new_pv_0_1 < new_pv_1_0);

		uint64_t new_pv_0 = amount_low * lowbits_low;

		uint64_t lowbits_new = new_pv_0 + (new_pv_1<<32);

		uint64_t highbits_new = (pv_1_overlow<<32)
							  + (new_pv_1>>32)
							  + (highbits_low * amount_low)
							  + (lowbits_high * amount_high)
							  + ((highbits_high * amount_low)<<32)
							  + ((highbits_low * amount_high)<<32);

		return InternalFractionalValue(highbits_new, lowbits_new);

	}

	static Price mult_price_by_fine_adjust(const Price& price, const uint64_t fine_adjust, const int fine_adjust_radix) {
		InternalFractionalValue generic = InternalFractionalValue::from_price(price).mult_by_uint64_t(fine_adjust);

		//std::cout<<std::hex<<generic<<std::endl;

		if (generic.highbits & (UINT64_MAX<<(fine_adjust_radix))) {
			return (UINT64_MAX);
		}

		if (! (generic.highbits | generic.lowbits)) {
			return ((uint64_t)1);
		}

		//divide by 2^radix
		return ((generic.highbits<<(64-fine_adjust_radix)) | (generic.lowbits >> fine_adjust_radix)); 
	}


	double to_double() const {
		return ((double) highbits) * ((double) ((uint64_t)1<<32)) + ((double) lowbits) / ((double) ((uint64_t)1<<32));
	}

	//multiply by an order amount
	InternalFractionalValue mult_by_uint32_t(const uint32_t amount) const {
		uint64_t highbits_low = highbits & low_mask;
		uint64_t highbits_high = (highbits & high_mask)>>32;

		uint64_t lowbits_low = lowbits & low_mask;
		uint64_t lowbits_high = (lowbits & high_mask)>>32;

		uint64_t new_pv_0 = amount * lowbits_low;
		uint64_t new_pv_1 = amount * lowbits_high;
		uint64_t new_pv_2 = amount * highbits_low;
		uint64_t new_pv_3 = amount * highbits_high;

		uint64_t lowbits_new = new_pv_0 + (new_pv_1<<32);

		uint64_t highbits_new = (UINT64_MAX - new_pv_0 < (new_pv_1<<32))
							  + ((new_pv_1 & high_mask)>>32)
							  + new_pv_2
							  + (new_pv_3<<32);


		return InternalFractionalValue(highbits_new, lowbits_new);

	}

	InternalFractionalValue& operator += (const InternalFractionalValue& other) {
		highbits += other.highbits + (UINT64_MAX - lowbits < other.lowbits);
		lowbits += other.lowbits;
		return *this;
	}

	void accumulator_add(const uint32_t amount, const Price& p) {
		uint64_t top = amount * ((p & high_mask)>>32);
		uint64_t bot = amount * (p & low_mask);
		uint64_t lowbits_add = bot + ((top & low_mask)<<32);
		uint64_t lowbits_add_overflow = (UINT64_MAX-bot < ((top & low_mask)<<32));
		lowbits_add_overflow += (UINT64_MAX - lowbits_add < lowbits);
		lowbits += lowbits_add;
		highbits += ((top & high_mask)>>32) + lowbits_add_overflow;
	}

	friend std::ostream& operator<<(std::ostream& os, const InternalFractionalValue& value) {
		os<<value.highbits<<" "<<value.lowbits;
		return os;
	}

};

}