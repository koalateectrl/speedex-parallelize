#include "xdr/block.h"
#include "xdr/header_summary.h"
#include "xdr/experiments.h"


#include "utils.h"
#include "price_utils.h"

#include <cstddef>

#include "work_unit_manager_utils.h"
#include "work_unit_state_commitment.h"

using namespace edce;

HeaderSummary summarize_header(const HashedBlock& header, const ExperimentBlockResults& block_results) {
	HeaderSummary out;

	const auto& prices = header.block.prices;
	size_t num_assets = prices.size();
	out.activated_supplies.resize(num_assets);

	for (size_t i = 0; i < num_assets; i++) {
		out.prices.push_back(PriceUtils::to_double(prices[i]));
		out.activated_supplies[i] = 0;
	}




	const auto& clearingDetails = header.block.internalHashes.clearingDetails;

	WorkUnitStateCommitmentChecker commitment_checker(clearingDetails, prices, header.block.feeRate);

	for (size_t i = 0; i < commitment_checker.size(); i++) {
		double activation = commitment_checker[i].fractionalSupplyActivated().to_double();
		auto category = WorkUnitManagerUtils::category_from_idx(i, num_assets);

		out.activated_supplies[category.sellAsset] += activation;
	}

	out.tat_timeout = block_results.block_creation_measurements.tat_timeout_happened;
	out.last_mempool_block = block_results.last_block_added_to_mempool;

	return out;
}

int main(int argc, char const *argv[])
{
	if (argc != 4) {
		std::printf("usage: ./blah <header_folder> <results_file>  <summary_out>\n");
		return 1;
	}

	ExperimentSummary summary;
	ExperimentResultsUnion results;

	std::string header_folder(argv[1]);
	std::string results_file(argv[2]);
	std::string outfile(argv[3]);

	if (load_xdr_from_file(results, results_file.c_str())) {
		throw std::runtime_error("failed to load file " + results_file);
	}

	size_t idx = 1;
	while(true) {
		std::string filename = header_folder + std::to_string(idx) + ".header";
		
		HashedBlock header;
		if (load_xdr_from_file(header, filename.c_str())) {
			std::printf("hit experiment end after %lu headers, finishing\n", idx);
			break;
		}

		summary.headers.push_back(summarize_header(header, results.block_results[idx-1].productionResults()));
		idx++;
	}

	if (save_xdr_to_file(summary, outfile.c_str())) {
		throw std::runtime_error("couldn't save file " + outfile);
	}
	return 0;
}
