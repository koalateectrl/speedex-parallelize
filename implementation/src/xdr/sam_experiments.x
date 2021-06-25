%#include "xdr/sam_transaction.h"
%#include "xdr/sam_block.h"

namespace sam_edce {

struct ExperimentParameters {
    int tax_rate;
    int smooth_mult;
    int num_threads;
    int num_assets;
    int num_accounts;
    int persistence_frequency;
    int num_blocks;
};

typedef AccountID AccountIDList<>;

typedef SignedTransaction ExperimentBlock<>;

}
