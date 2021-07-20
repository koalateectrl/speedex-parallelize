#include "xdr/signature_check_api.h"
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
    const ExperimentParameters& params, uint16_t ip_idx, uint16_t checker_begin_idx, uint16_t checker_end_idx,
    uint16_t num_assets, uint8_t tax_rate, uint8_t smooth_mult) {

    _ip_idx = ip_idx;
    _checker_begin_idx = checker_begin_idx;
    _checker_end_idx = checker_end_idx;

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

  std::cout << "SUCCESSFULLY LOADED ACCOUNTS " << std::endl;
  return std::make_unique<unsigned int>(0);
}



std::unique_ptr<unsigned int>
SignatureShardV1_server::check_block(const SerializedBlockWithPK& block_with_pk, 
  const uint64& num_threads)
{
  auto timestamp = init_time_measurement();

  SignedTransactionWithPKList tx_with_pk_list;
  
  xdr::xdr_from_opaque(block_with_pk, tx_with_pk_list);

  size_t num_child_machines = 2;

  std::vector<SignedTransactionWithPKList> tx_with_pk_split_list;

  split_transaction_block(tx_with_pk_list, num_child_machines, tx_with_pk_split_list);

  size_t num_threads_lambda = num_threads;

  tbb::parallel_for(
        tbb::blocked_range<size_t>(0, num_child_machines),
        [&](auto r) {
            for (size_t i = r.begin(); i != r.end(); i++) {
                SerializedBlockWithPK serialized_block_with_pk = xdr::xdr_to_opaque(tx_with_pk_split_list[i]);
                if (poll_node(i + 3, serialized_block_with_pk, num_threads_lambda) == 1) {
                    throw std::runtime_error("sig checking failed!!!");
                }
            }
        });

  float res = measure_time(timestamp);
  std::cout << "Total time for check_all_signatures RPC call: " << res << std::endl;

  return std::make_unique<unsigned int>(0);

}

//not rpc 

void SignatureShardV1_server::split_transaction_block(const SignedTransactionWithPKList& orig_vec, 
    const size_t num_child_machines, std::vector<SignedTransactionWithPKList>& split_vec) {
    
    size_t length = orig_vec.size() / num_child_machines;
    size_t remain = orig_vec.size() % num_child_machines;
    size_t begin = 0;
    size_t end = 0;

    for (size_t i = 0; i < std::min(num_child_machines, orig_vec.size()); i++) {
        end += (remain > 0) ? (length + !!(remain--)) : length;
        split_vec.push_back(SignedTransactionWithPKList(orig_vec.begin() + begin, orig_vec.begin() + end));
        begin = end;
    }
}

uint32_t
SignatureShardV1_server::poll_node(int idx, const SerializedBlockWithPK& block_with_pk, 
    const uint64& num_threads) {
    
    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_CHECK_PORT);
    auto client = xdr::srpc_client<SignatureCheckV1>(fd.get());

    // if works return 0 else if failed return 1
    uint32_t return_value = *client.check_all_signatures(block_with_pk, num_threads);
    std::cout << return_value << std::endl;
    return return_value;
}

std::string SignatureShardV1_server::hostname_from_idx(int idx) {
    return std::string("10.10.1.") + std::to_string(idx);
}



}
