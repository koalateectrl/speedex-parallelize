
#include "rpc/signature_check_api.h"
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

void
SignatureCheckV1_server::print_hello_world()
{
  std::cout << "Hello World" << std::endl;
}

std::unique_ptr<unsigned int>
SignatureCheckV1_server::check_all_signatures(const std::string& experiment_name, 
  const SerializedBlock& block, const SerializedPKs& pks, const uint64& num_threads)
{
  DeterministicKeyGenerator key_gen;

  ExperimentParameters params;

  std::string experiment_root = std::string("experiment_data/") + experiment_name;

  std::string params_filename = experiment_root + std::string("/params");

  if (load_xdr_from_file(params, params_filename.c_str())) {
    throw std::runtime_error("failed to load params file");
  }

  EdceManagementStructures management_structures(
        20,
        ApproximationParameters {
            .tax_rate = 10,
            .smooth_mult = 10
        });

  std::printf("num accounts: %u\n", params.num_accounts);

  AccountIDList account_id_list;

  auto accounts_filename = experiment_root + std::string("/accounts");
  if (load_xdr_from_file(account_id_list, accounts_filename.c_str())) {
    throw std::runtime_error("failed to load accounts list " + accounts_filename);
  }

  std::vector<PublicKey> pks;
  pks.resize(account_id_list.size());
  tbb::parallel_for(
    tbb::blocked_range<size_t>(0, account_id_list.size()),
    [&key_gen, &account_id_list, this](auto r) {
      for (size_t i = r.begin(); i < r.end(); i++) {
        auto [_, pk] = key_gen.deterministic_key_gen(account_id_list[i]);
        pks[i] = pk;
      }
    });

  for (int32_t i = 0; i < params.num_accounts; i++) {

    //std::printf("%lu %s\n", account_id_list[i], DebugUtils::__array_to_str(pks.at(i).data(), pks[i].size()).c_str());
    management_structures.db.add_account_to_db(account_id_list[i], pks[i]);
  }

  management_structures.db.commit(0);

  BlockSignatureChecker checker(management_structures);






  SamBlockSignatureChecker sam_checker;

  if (!sam_checker.check_all_sigs(block, pks)) {
    std::cout << "FAILED" << std::endl;
  } else {
    std::cout << "SUCCESS" << std::endl;
  }



  tbb::global_control control(
    tbb::global_control::max_allowed_parallelism, num_threads);

  if (!checker.check_all_sigs(block)) {
    return std::make_unique<unsigned int>(1);
  }

  return std::make_unique<unsigned int>(0);
}


}
