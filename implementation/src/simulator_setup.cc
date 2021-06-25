#include "simulator_setup.h"
#include <random>
#include "price_utils.h"
#include <cstdio>

#include "xdr/types.h"
#include "xdr/transaction.h"


#include "simple_debug.h"


#include "database.h"

namespace edce {

FORCE_TEMPLATE_INSTANTIATION(SimulatorSetup);

AccountID make_account(int account_num) {
	return account_num;
}

template <typename Database>
void SimulatorSetup<Database>::initialize_database(int num_accounts, int num_assets) {
	std::mt19937 gen(0);


	std::uniform_int_distribution<> asset_dist (0, num_assets);

	std::uniform_int_distribution<> endow_dist (10, 10000);

	std::uniform_int_distribution<> num_owned_dist (1, num_assets/3 + 1);

	auto db_view = UnbufferedMemoryDatabaseView(db);

	for (int i = 0; i < num_accounts; i++) {
		auto account = make_account(i);
		account_db_idx temp;
		db_view.create_new_account(account, nullptr, &temp); // SIMULATOR SO PK IS NULL
	}
	db_view.commit();
	db.commit();
	for (int i = 0; i < num_accounts; i++) {
		account_db_idx idx;
		db.lookup_user_id(make_account(i), &idx);
		int num_owned = num_owned_dist(gen);


		for (int j = 0; j < num_owned; j++) {
			int asset = asset_dist(gen);
			int endow = endow_dist(gen);
			db.transfer_available(idx, asset, endow);
			INFO("Giving account %d (idx: %d) asset %d units %d", i, idx, asset, endow);
		}
	}
	db.commit();
}

template<typename Database>
xdr::xvector<Transaction>
SimulatorSetup<Database>::generate_random_valid_block(
 	int num_txs, 
 	int num_accounts, 
 	int num_assets) {

	xdr::xvector<Transaction> tx_block;
	std::mt19937 gen(0);

	std::vector<double> prices;
	std::uniform_real_distribution price_dist(0.01, 100.0);
	for (int i = 0; i < num_assets; i++) {
		prices.push_back(price_dist(gen));
	}

	std::uniform_int_distribution<> account_dist(0, num_accounts-1);
	std::uniform_int_distribution<> big_dist(0, 100000);
	std::uniform_real_distribution<> error_dist(0.8, 1.2);
	std::uniform_int_distribution<> asset_dist(0, num_assets-1);


	for (int i = 0; i < num_txs; i++) {
		AccountID account = make_account(account_dist(gen));
		account_db_idx idx;
		bool result = db.lookup_user_id(account, &idx);

		INFO("lookup result:%d", result);
		INFO("looked up account %ld with index %ld", account, idx);

		Transaction tx;

		TransactionMetadata metadata;
		metadata.sourceAccount = account;
		metadata.sequenceNumber = ((uint64_t)(i+1))<<8;

		tx.metadata = metadata;
		for (int j = 0; j < num_assets; j++) {
			auto endow = db.lookup_available_balance(idx, j);
			if (endow < 0) {
				throw std::runtime_error("what the fuck");
			}
			if (endow > 0) {
				int64_t amount = big_dist(gen) % endow;
				int buy_good = j;
				while (buy_good == j) {
					buy_good = asset_dist(gen);
				}

				double price_d = (prices[j]/prices[buy_good])
					* error_dist(gen);

				CreateSellOfferOp offer;
				offer.category.sellAsset = j;
				offer.category.buyAsset = buy_good;
				INFO("create sell offer selling %ld buying %ld", offer.category.sellAsset, offer.category.buyAsset);
				offer.category.type = OfferType::SELL;
				offer.amount = amount;
				offer.minPrice = PriceUtils::from_double(price_d);

				Operation op;
				op.body.type(OperationType::CREATE_SELL_OFFER);
				op.body.createSellOfferOp() = offer;
				tx.operations.push_back(op);
				db.escrow(idx, j, amount);
			}
		}
		tx_block.push_back(tx);

		INFO("added tx %d from account %ld", i, tx_block[i].metadata.sourceAccount);
	}
	TRACE("finished transaction block generation");
	db.rollback();
	return tx_block;
}

}