#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

#include "../xdr/types.h"
#include "../xdr/transaction.h"

#include "../xdr/transaction_submission_api.h"
#include "../xdr/server_control_api.h"

#include "transaction_builder.h"

#include <sodium.h>

#include <xdrpp/srpc.h>

namespace txgen {

struct CachedAccount {
	std::vector<int64_t> assets;
	edce::SigningKey sk;
	uint64_t seq_num;

	CachedAccount(int num_assets, edce::SigningKey sk, int64_t default_amount = 0)
		: assets(), sk(sk), seq_num(1) {
			for (int i = 0; i < num_assets; i++) {
				assets.push_back(default_amount);
			}
		}
};

class AccountManager {

	std::unordered_map<edce::AccountID, CachedAccount> accounts;

	edce::SigningKey& global_root_sk;
	edce::AccountID local_root_account;

	TransactionBuilder builder;

	int num_assets;


public:

	static constexpr int64_t NEW_ACCOUNT_WITHDRAW_AMT = 1000000;
	static constexpr int64_t LOCAL_ROOT_WITHDRAW_AMT = NEW_ACCOUNT_WITHDRAW_AMT * 1000000;

	AccountManager(
		edce::SigningKey& root_sk, 
		edce::AccountID local_root_account,
		int num_assets) 
	: accounts(), global_root_sk(root_sk), local_root_account(local_root_account), builder(), num_assets(num_assets) {};

	void init(xdr::srpc_client<edce::SubmitTransactionV1>& client, uint64_t& global_root_seqnum);

	void generate_transactions_underlying_prices(int num_txs);

	void create_accounts(int num_new_accounts, edce::AccountID starting_id, xdr::srpc_client<edce::SubmitTransactionV1>& client);
};

}