
%#include "xdr/transaction.h"

namespace edce {

program SubmitTransaction {
	version SubmitTransactionV1 {
		void submit_transaction(SignedTransaction) = 1;
	} = 1;
} = 0x10734299;

}
