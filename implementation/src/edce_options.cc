#include "edce_options.h"
#include <stdexcept>

namespace edce {

extern "C" {
	#include <libfyaml.h>

	bool _parse_options(const char* filename,
		unsigned int* tax_rate, unsigned int* smooth_mult, unsigned int* num_assets, unsigned int* num_tx_processing_threads, unsigned int* num_sig_check_threads, long unsigned int* persistence_frequency) {

		struct fy_document* fyd = fy_document_build_from_file(NULL, filename);
		
		if (fyd == NULL) {
			return false;
		}

		int count = 0;
		count += fy_document_scanf(
			fyd,
			"/protocol/tax_rate %u "
			"/protocol/smooth_mult %u "
			"/protocol/num_assets %u",
			tax_rate, smooth_mult, num_assets);

		count += fy_document_scanf(
			fyd,
			"/edce-node/num_tx_processing_threads %u "
			"/edce-node/num_sig_check_threads %u "
			"/edce-node/persistence_frequency %lu",
			num_tx_processing_threads, num_sig_check_threads, persistence_frequency);

		return count == 6;
	}
}


void EdceOptions::parse_options(const char* filename) {

	std::printf("foo\n");

	if (!_parse_options(filename, &tax_rate, &smooth_mult, &num_assets, &num_tx_processing_threads, &num_sig_check_threads, &persistence_frequency)) {
		throw std::runtime_error("Error parsing options (did you forget the .yaml?)");
	}

	std::printf("after\n");
}

void EdceOptions::print_options() {
	std::printf("tax_rate=%u smooth_mult=%u num_assets=%u\n", tax_rate, smooth_mult, num_assets);
}

}
