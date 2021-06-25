
#include "rpc/state_query_api.h"

namespace edce {

std::unique_ptr<StateQueryResponse>
StateQueryV1_server::account_status(const AccountID& owner, const uint64& block_number) {

  return cache.get_account_proof(owner, block_number);
}

std::unique_ptr<StateQueryResponse>
StateQueryV1_server::offer_status(const OfferCategory& category, const Price& min_price, const AccountID& owner, const uint64& offer_id, const uint64& block_number)
{
  return cache.get_offer_proof(category, min_price, owner, offer_id, block_number);
}

std::unique_ptr<StateQueryResponse>
StateQueryV1_server::transaction_status(const AccountID& owner, const uint64& offer_id, const uint64& block_number)
{
  return cache.get_transaction_proof(owner, offer_id, block_number);
}

std::unique_ptr<HashedBlockRange>
StateQueryV1_server::get_block_header_range(const uint64& starting_block, const uint64& ending_block)
{
  return cache.get_block_header_range(starting_block, ending_block);
}

std::unique_ptr<StateQueryResponse>
StateQueryV1_server::get_block_header(const uint64 &block_number)
{
  
  return cache.get_block_header(block_number);
}

}
