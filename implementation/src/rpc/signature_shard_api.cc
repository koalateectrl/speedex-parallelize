
#include "rpc/signature_shard_api.h"
#include <xdrpp/marshal.h>
#include <iostream>

#include "simple_debug.h"


#include "crypto_utils.h"
#include "utils.h"

#include "xdr/experiments.h"

#include "edce_management_structures.h"
#include "tbb/global_control.h"

namespace edce {

//rpc
void SignatureShardV1_server::print_hello_world() {
    std::cout << "HELLO WORLD" << std::endl;
}

std::unique_ptr<unsigned int> 
SignatureShardV1_server::init_shard(const SerializedAccountIDWithPK& account_with_pk, 
    const ExperimentParameters& params, 
    uint16_t num_assets, uint8_t tax_rate, uint8_t smooth_mult) {

    EdceManagementStructures management_structures(
        num_assets,
        ApproximationParameters {
          .tax_rate = tax_rate,
          .smooth_mult = smooth_mult
        });

  AccountIDWithPKList account_with_pk_list;
  xdr::xdr_from_opaque(account_with_pk, account_with_pk_list);

  for (size_t i = 0; i < account_with_pk_list.size(); i++) {
      management_structures.db.add_account_to_db(account_with_pk_list[i].account, account_with_pk_list[i].pk);
  }

  management_structures.db.commit(0);

  // TEST CODE HERE

  BlockSignatureChecker checker(management_structures);

  ExperimentBlock block;

  std::string experiment_root = std::string("experiment_data/basic_allvalid");
  std::string block_filename = experiment_root + std::string("/") + std::string(1) + std::string(".txs");

  if (load_xdr_from_file(block, block_filename.c_str())) {
      std::printf("%s\n", block_filename.c_str());
      throw std::runtime_error("failed to load tx block");
  }

  SignedTransactionList tx_list;

  tx_list.insert(tx_list.end(), block.begin(), block.end());

  SerializedBlock serialized_block = xdr::xdr_to_opaque(tx_list);
  
  size_t num_threads = 4;

  tbb::global_control control(
    tbb::global_control::max_allowed_parallelism, num_threads);

  auto timestamp = init_time_measurement();

  if (!checker.check_all_sigs(serialized_block)) {
    throw std::runtime_error("sig checking failed!!!");
  }

  /////


  std::cout << "HELLO WORLD" << std::endl;
  return std::make_unique<unsigned int>(0);
}

}
