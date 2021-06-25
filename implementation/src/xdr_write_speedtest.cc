
#include "xdr/block.h"

#include "xdr/experiments.h"

#include "utils.h"


using namespace edce;

int main(int argc, char const *argv[])
{


	if (argc != 3) {
		std::printf("usage: <infile (Experiment)> <outfile>\n");
		return 0;
	}

	ExperimentBlock exp;

	load_xdr_from_file(exp, argv[1]);

	save_xdr_to_file(exp, "comparison");

	auto timestamp = init_time_measurement();

	save_xdr_to_file_fast(exp, argv[2]);

	float diff = measure_time(timestamp);

	std::printf("duration: %f\n", diff);

	xdr::xvector<Transaction> txs;

	load_xdr_from_file(txs, argv[2]);
	
}
