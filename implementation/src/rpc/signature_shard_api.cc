
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

  std::cout << "HELLO WORLD" << std::endl;
  return std::make_unique<unsigned int>(0);
}



std::unique_ptr<unsigned int>
SignatureShardV1_server::check_all_signatures(const SerializedBlockWithPK& block_with_pk, 
  const uint64& num_threads)
{

  auto timestamp = init_time_measurement();

  SamBlockSignatureChecker sam_checker;

  tbb::global_control control(
    tbb::global_control::max_allowed_parallelism, num_threads);

  if (!sam_checker.check_all_sigs(block_with_pk)) {
    return std::make_unique<unsigned int>(1);
  } 

  float res = measure_time(timestamp);
  std::cout << "Total time for check_all_signatures RPC call: " << res << std::endl;

  return std::make_unique<unsigned int>(0);

}

}
