#include "tx_gen_mock.h"

namespace edce {

std::vector<Transaction> TxGenMock::stream_transactions(int num_transactions) {
	std::uniform_int_distribution<> asset_dist (0, num_assets-1);
	std::uniform_int_distribution<> endow_dist (10, 100);
	std::uniform_int_distribution<> account_dist (0, num_agents-1);

	auto& db = management_structures.db;

	std::vector<Transaction> output;
	output.reserve(num_transactions);

	for (int i = 0; i < num_transactions; i++) {
		int order_sz = endow_dist(gen);
		int sell_asset = asset_dist(gen);
		int buy_asset = sell_asset;
		while (buy_asset == sell_asset) {
			buy_asset = asset_dist(gen);
		}
		AccountID account = account_dist(gen);

		account_db_idx idx;
		db.lookup_user_id(account, &idx);

		db.transfer_available(idx, sell_asset, order_sz);

		SignedTransaction signed_tx;
		auto& tx = signed_tx.transaction;
		tx.metadata.sourceAccount = account;
		//todo maybe complaitns here
		tx.metadata.sequenceNumber = (used_sequence_numbers[account]->fetch_add(1, std::memory_order_relaxed))<<8;

		CreateSellOfferOp op;
		op.category = TxTypeUtils::make_category(sell_asset, buy_asset, OfferType::SELL);

		op.amount = order_sz;

		double mean_valuation = PriceUtils::to_double(underlying_prices[sell_asset])/PriceUtils::to_double(underlying_prices[buy_asset]);

		std::normal_distribution<> dist(mean_valuation, 3);
		double price = -1;
		while (price <= 0) {
			price = dist(gen);
		}
		op.minPrice = PriceUtils::from_double(price);

		tx.operations.push_back(TxTypeUtils::make_operation(op));

		TransactionResult result;

		auto status = tx_processor.process_transaction(signed_tx, result);
		if (status != TransactionProcessingStatus::SUCCESS) {
			std::printf("tx creation returned non-success account=%lu status=%d (in order of likelihood: bad seq num, nonexistent account, bad asset amount)\n", account, status);
		}
		output.push_back(tx);
	}
	tx_processor.finish();
	return output;
}

}