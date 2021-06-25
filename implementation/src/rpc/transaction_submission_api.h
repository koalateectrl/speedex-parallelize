// -*- C++ -*-
// Scaffolding originally generated from xdr/transaction_submission_api.x.
// Edit to add functionality.

#pragma once

#include "xdr/transaction_submission_api.h"

#include <sodium.h>

#include "database.h"
#include "merkle_work_unit_manager.h"
#include "transaction_buffer_manager.h"
#include "signature_check.h"

namespace edce {

class SubmitTransactionV1_server {

	TransactionBufferManager& buffer_manager;

	TransactionBufferManager::buffer_ptr current_buffer;

public:
  using rpc_interface_type = SubmitTransactionV1;

  SubmitTransactionV1_server(TransactionBufferManager& buffer_manager) 
  	: buffer_manager(buffer_manager), current_buffer() {}

  void submit_transaction(const SignedTransaction &arg);
};

}

