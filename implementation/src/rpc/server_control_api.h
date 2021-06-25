// -*- C++ -*-
// Scaffolding originally generated from xdr/server_control_api.x.
// Edit to add functionality.

#pragma once

#include "xdr/server_control_api.h"
#include "xdr/types.h"

#include "edce.h"

#include <sodium.h>

namespace edce {

class ServerControlV1_server {
	Edce& main_edce;
	Price* price_workspace;
	std::vector<unsigned char> root_sk;

public:
  using rpc_interface_type = ServerControlV1;

  ServerControlV1_server(
  	Edce& main_edce, 
  	Price* price_workspace, 
  	unsigned char* sk) 
  : main_edce(main_edce), 
  price_workspace(price_workspace),
  root_sk() {
  	if (sodium_init() == -1) {
  		throw std::runtime_error("failed to init sodium when starting servercontrolV1");
  	}
  	root_sk.insert(root_sk.end(), sk, sk+crypto_sign_SECRETKEYBYTES);
  }

  std::unique_ptr<SigningKey> get_root_sk();
  void add_sig_check_threads(const int &arg);
  void add_tx_processing_threads(const int &arg);
  void generate_block();
};

}

