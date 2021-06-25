// Scaffolding originally generated from xdr/transaction_submission_api.x.
// Edit to add functionality.

#include "rpc/transaction_submission_api.h"
#include <xdrpp/marshal.h>
namespace edce {

void
SubmitTransactionV1_server::submit_transaction(const SignedTransaction &arg)
{
  if (!current_buffer) {
    current_buffer = buffer_manager.get_empty_buffer();
  }
  bool full = current_buffer->insert(arg);
  if (full) {
    buffer_manager.return_full_buffer(std::move(current_buffer));
    current_buffer = buffer_manager.get_empty_buffer();
  }  
}

} /* edce */
