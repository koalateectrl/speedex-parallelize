#include "account_manager.h"

namespace txgen {

void AccountManager::generate_transactions_underlying_prices(int num_txs) {

}

void AccountManager::create_accounts(int num_new_accounts, edce::AccountID starting_id, xdr::srpc_client<edce::SubmitTransactionV1>& client) {

	auto& local_root = accounts.at(local_root_account);

	for (int i = 0; i < num_new_accounts; i++) {
		edce::PublicKey pk;
		edce::SigningKey sk;

		crypto_sign_keypair(sk.data(), pk.data());

		auto tx = builder.create_new_account_tx(local_root.sk, local_root_account, local_root.seq_num, pk, starting_id + i, NEW_ACCOUNT_WITHDRAW_AMT, num_assets);

		client.submit_transaction(tx);

		accounts.insert({starting_id + i, CachedAccount(num_assets, sk, NEW_ACCOUNT_WITHDRAW_AMT)});
	}
}

void AccountManager::init(xdr::srpc_client<edce::SubmitTransactionV1>& client, uint64_t& global_root_seqnum) {

	edce::PublicKey pk;
	edce::SigningKey sk;
	crypto_sign_keypair(sk.data(), pk.data());

	auto tx = builder.create_new_account_tx(global_root_sk, 0, global_root_seqnum, pk, local_root_account, LOCAL_ROOT_WITHDRAW_AMT, num_assets);

	client.submit_transaction(tx);

	accounts.insert({local_root_account, CachedAccount(num_assets, sk, LOCAL_ROOT_WITHDRAW_AMT)});

	for (int i = 1; i < 10000; i++) {
		client.submit_transaction(builder.empty_transaction(global_root_sk, 0, global_root_seqnum));
	}
}


}