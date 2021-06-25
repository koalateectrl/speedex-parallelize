// Scaffolding originally generated from xdr/server_control_api.x.
// Edit to add functionality.

#include "rpc/server_control_api.h"
#include "database.h"

namespace edce {

std::unique_ptr<SigningKey>
ServerControlV1_server::get_root_sk()
{
	SigningKey sk_out;
	memcpy(sk_out.data(), root_sk.data(), crypto_sign_SECRETKEYBYTES);

	std::printf("get_root_sk called\n");

  	return std::make_unique<SigningKey>(sk_out);
}

void
ServerControlV1_server::add_sig_check_threads(const int &arg)
{
  main_edce.add_signature_check_threads(arg); 
}

void
ServerControlV1_server::add_tx_processing_threads(const int &arg)
{
  main_edce.add_tx_processing_threads(arg); 
}

void
ServerControlV1_server::generate_block()
{
	main_edce.build_block(price_workspace);
}

}
