#pragma once

#include "rpc/state_query_api.h"
#include <memory>
#include "frozen_data_cache.h"

namespace edce {

class StateQueryV1_server {
	FrozenDataCache& cache;
public:
  using rpc_interface_type = StateQueryV1;

  StateQueryV1_server(FrozenDataCache& cache) : cache(cache) {};

  std::unique_ptr<StateQueryResponse> account_status(const AccountID& owner, const uint64& block_number);
  std::unique_ptr<StateQueryResponse> offer_status(
  	const OfferCategory& category,
  	const Price& min_price,
  	const AccountID& owner,
  	const uint64& offer_id,
  	const uint64& block_number);
  std::unique_ptr<StateQueryResponse> transaction_status(const AccountID& owner, const uint64& offer_id, const uint64& block_number);
  std::unique_ptr<HashedBlockRange> get_block_header_range(const uint64& starting_block, const uint64& ending_block);
  std::unique_ptr<StateQueryResponse> get_block_header(const uint64 &block_number);
};

}
