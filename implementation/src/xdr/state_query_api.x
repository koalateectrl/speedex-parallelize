
%#include "xdr/types.h"
%#include "xdr/trie_proof.h"

namespace edce {

//too high block ids are interpreted as "give me most recent"
enum QueryStatus {
	PROOF_SUCCESS = 0,
	HEADER_SUCCESS = 1,
	BLOCK_ID_TOO_OLD = 2
};

struct StateQueryResponse {
	union switch (QueryStatus status) {
		case PROOF_SUCCESS:
			Proof result;
		case HEADER_SUCCESS:
			HashedBlock header;
		default:
			void;
	} body;

	uint64 blockId;
};

typedef HashedBlock HashedBlockRange<>;
	
program StateQuery {
	version StateQueryV1 {
		StateQueryResponse account_status(AccountID, uint64) = 1;
		StateQueryResponse offer_status(OfferCategory, Price, AccountID, uint64, uint64) = 2;
		StateQueryResponse transaction_status(AccountID, uint64, uint64) = 3;
		HashedBlockRange get_block_header_range(uint64, uint64) = 4;
		StateQueryResponse get_block_header(uint64) = 5;
	} = 1;
} = 0x13423523;

}