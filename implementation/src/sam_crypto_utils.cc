#include "crypto_utils.h"
#include "sam_crypto_utils.h"

#include <xdrpp/marshal.h>
#include <tbb/parallel_reduce.h>


#include "utils.h"

#include <cstddef>

namespace edce {

bool 
SamBlockSignatureChecker::check_all_sigs(const SerializedBlock& block) {
    SignedTransactionList txs;
    xdr::xdr_from_opaque(block, txs);

    auto checker = SigCheckReduce(management_structures, txs);

    tbb::parallel_reduce(tbb::blocked_range<size_t>(0, txs.size(), 2000), checker);

    return checker.valid;
}

}


