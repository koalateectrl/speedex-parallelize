#pragma once
#include <cstddef>

namespace edce {

struct EdceOptions {

	// protocol parameters
	unsigned int tax_rate;
	unsigned int smooth_mult;
	unsigned int num_assets;

	// operational parameters
	unsigned int num_tx_processing_threads;
	unsigned int num_sig_check_threads;

	size_t persistence_frequency;

	void parse_options(const char* configfile);

	void print_options();
};


} /* edce */