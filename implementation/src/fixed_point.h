#pragma once

#include <cstdint>

namespace edce {

//make it safe from overflows
class FixedPoint {
	unsigned __int128 data_bits;
public:
	FixedPoint(unsigned __int128 data_bits)
		: data_bits(data_bits) {};


};

}


/*
It would be nice to say "either market clears efficiently, or your asset isn't traded much"

Write out some weird case scenarios for bad things that could happen, do it end to end (***) i.e. start with discrete orders

What if we comptued approx prices, then told each order they could execute at (p-eps, p+eps)

How bad can this get?  See if I can come up with a horrible contrived example.

No "cheap talk" i.e. cannot influence prices without having some order execute.




*/