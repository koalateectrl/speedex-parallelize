%#include "xdr/sam_transaction.h"


namespace sam_edce {

const MAX_TRANSACTIONS_PER_BLOCK = 1000000;

typedef SignedTransaction SignedTransactionList<MAX_TRANSACTIONS_PER_BLOCK>;

typedef opaque SerializedBlock<>;

}