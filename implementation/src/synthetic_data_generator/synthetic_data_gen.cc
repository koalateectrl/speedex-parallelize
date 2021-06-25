#include "synthetic_data_gen.h"



namespace edce {

template class GeneratorState<std::minstd_rand>;

template<typename random_generator>
void 
GeneratorState<random_generator>::gen_new_accounts(uint64_t num_new_accounts) {
	for (uint64_t i = 0; i < num_new_accounts; i++) {
		allocate_new_account_id();
	}
}

template<typename random_generator>
void
GeneratorState<random_generator>::dump_account_list() {
	AccountIDList list;
	list.reserve(num_active_accounts);
	std::string accounts_filename = output_directory + "accounts";

	for (AccountID acct : existing_accounts_set) {
		list.push_back(acct);
	}

	if (save_xdr_to_file(list, accounts_filename.c_str())) {
		throw std::runtime_error("could not save accounts list!");
	}
}


template<typename random_generator>
std::vector<double> 
GeneratorState<random_generator>::gen_prices() {

	if (options.price_options.exp_param == 0) {
		std::uniform_real_distribution<> price_dist(options.price_options.min_price, options.price_options.max_price);
		std::vector<double> price_out;
		for (size_t i = 0; i < options.num_assets; i++) {
			price_out.push_back(price_dist(gen));
		}
		return price_out;
	} else {
		std::exponential_distribution<> price_dist(options.price_options.exp_param);
		std::vector<double> price_out;

		double range = options.price_options.max_price - options.price_options.min_price;
		for (size_t i = 0; i < options.num_assets; i++) {
			price_out.push_back(std::fmod(price_dist(gen), range) + options.price_options.min_price);
		}
		return price_out;
	}
}

template<typename random_generator>
void
GeneratorState<random_generator>::modify_prices(std::vector<double>& prices) {
	if (options.price_options.per_block_delta == 0.0) {
		return;
	}
	std::normal_distribution<> delta_dist(0, options.price_options.per_block_delta);

	for (size_t i = 0; i < options.num_assets; i++) {
		double delta = std::exp(delta_dist(gen)); // *= e^{normal(0, 1)}
		prices[i] *= delta;
	}

	/*std::uniform_real_distribution<> adjust_dist(0, options.price_options.per_block_delta * 2);
	for (size_t i = 0; i < options.num_assets; i++) {
		double delta = adjust_dist(gen) - options.price_options.per_block_delta;
		prices[i] *= (1.0 + delta);
	}*/
}

template<typename random_generator>
void
GeneratorState<random_generator>::print_prices(const std::vector<double>& prices) const {
	for (size_t i = 0; i < prices.size(); i++) {
		std::printf("asset %lu has valuation %lf\n", i, prices[i]);
	}
}


//not super efficient but ah well
template<typename random_generator>
std::vector<AssetID> 
GeneratorState<random_generator>::gen_asset_cycle() {

	std::uniform_real_distribution<> size_rand (0.0, 1.0);
	size_t size = options.cycle_dist.get_size(size_rand(gen));

	//if (options.asset_bias != 0) {
		std::vector<AssetID> output;
		for (size_t i = 0; i < size; i++) {
			while(true) {
				AssetID candidate = gen_asset();
				if (std::find(output.begin(), output.end(), candidate) == output.end()) {
					output.push_back(candidate);
					break;
				}
			}
		}
		return output;
	//}

	//std::vector<AssetID> output;
	//for (size_t i = 0; i < options.num_assets; i++) {
	//	output.push_back(i);
	//}

	//std::shuffle(output.begin(), output.end(), gen);

	//return std::vector(output.begin(), output.begin() + size);
}

template<typename random_generator>
int64_t 
GeneratorState<random_generator>::gen_endowment(double price) {
	std::uniform_int_distribution<> endow_dist(options.initial_endow_min, options.initial_endow_max);
	std::uniform_real_distribution<> whale_dist(0, 1);

	int64_t amount = endow_dist(gen);
	if (amount > 1000000) {
		throw std::runtime_error("invalid endow amount!");
	}

	if (options.normalize_values) {
		amount = (100 * amount ) / price;
	}

	if (whale_dist(gen) < options.whale_percentage) {
		amount *= options.whale_multiplier;
	}

	return amount;
}


template<typename random_generator>
AccountID 
GeneratorState<random_generator>::gen_account() {
	std::exponential_distribution<> account_dist(options.account_dist_param);

	uint64_t account_idx = static_cast<uint64_t>(std::floor(account_dist(gen))) % num_active_accounts;
	return existing_accounts_map.at(account_idx);
}

template<typename random_generator>
OperationType
GeneratorState<random_generator>::gen_new_op_type() {
	double tx_type_res = type_dist(gen);

	if (is_payment_op(tx_type_res)) {
		return OperationType::PAYMENT;
	}
	if (is_create_offer_op(tx_type_res)) {
		return OperationType::CREATE_SELL_OFFER;
	}
	if (is_create_account_op(tx_type_res)) {
		return OperationType::CREATE_ACCOUNT;
	}
	std::printf("invalid tx_type_res %lf\n", tx_type_res);
	throw std::runtime_error("invalid tx_type_res");
}

template<typename random_generator>
double 
GeneratorState<random_generator>::gen_tolerance() {
	std::uniform_real_distribution<> tolerance_dist(options.price_options.min_tolerance, options.price_options.max_tolerance);
	return tolerance_dist(gen);
}

template<typename random_generator>
double 
GeneratorState<random_generator>::get_exact_price(const std::vector<double>& prices, const OfferCategory& category) {
	return prices.at(category.sellAsset) / prices.at(category.buyAsset);
}

template<typename random_generator>
Operation 
GeneratorState<random_generator>::make_sell_offer(int64_t amount, double ratio, const OfferCategory& category) {
	Price min_price = PriceUtils::from_double(ratio);
	CreateSellOfferOp op {category, amount, min_price};
	return TxTypeUtils::make_operation(op);
}

template<typename random_generator>
double 
GeneratorState<random_generator>::gen_good_price(double exact_price) {
	return exact_price * (1.0 - gen_tolerance());
}

template<typename random_generator>
double 
GeneratorState<random_generator>::gen_bad_price(double exact_price) {
	return exact_price + (1.0 + gen_tolerance());
}


//does not fill in seq numbers
template<typename random_generator>
std::vector<SignedTransaction> 
GeneratorState<random_generator>::gen_good_tx_cycle(const std::vector<AssetID>& assets, const std::vector<double>& prices) {
	
	std::vector<SignedTransaction> output;

	int64_t endow = gen_endowment(prices[assets[0]]);
	//int64_t start_endow = endow;

	std::vector<std::pair<double, int64_t>> prev_endows;

//	bool error_flag = false;
	for (unsigned int i = 0; i < assets.size(); i++) {
		OfferCategory category(assets.at(i), assets.at((i+1)%assets.size()), OfferType::SELL);
		if (category.buyAsset == category.sellAsset) {
			throw std::runtime_error("shouldn't have identical buy and sell assets");
		}
		double min_price = get_exact_price(prices, category);

		SignedTransaction tx;
		tx.transaction.operations.push_back(make_sell_offer(endow, gen_good_price(min_price), category));
		endow = std::ceil(endow * min_price);

		prev_endows.emplace_back(min_price, endow);

		tx.transaction.metadata.sourceAccount = gen_account();
		output.push_back(tx);
		//if (endow >= 20000000) {
		//	std::printf("invalid amount! %lu %lf\n", endow, min_price);
		//	error_flag = true;
		//}
	}

	/*if (endow > 1000 * start_endow || error_flag) {

		for (auto& pair : prev_endows) {
			std::printf("endow: %lu after mult by price %lf\n", pair.second, pair.first);
		}

		std::printf("start_endow: %ld endow: %ld\n", start_endow, endow);

		int64_t recreate_endow = start_endow;
		for (size_t i = 0; i < assets.size(); i++) {
			OfferCategory category(assets.at(i), assets.at((i+1)%assets.size()), OfferType::SELL);
			double min_price = get_exact_price(prices, category);

			recreate_endow = std::ceil(recreate_endow * min_price);
			std::printf("sell %lf buy %lf\n", prices.at(assets.at(i)), prices.at(assets.at((i+1)% assets.size())));
			std::printf("min price: %lf recreate_endow: %ld\n", min_price, recreate_endow);
		}
		
		throw std::runtime_error("some kind of miscomputation in endow calculation!");
	}*/
	return output;
}

template<typename random_generator>
void
GeneratorState<random_generator>::normalize_asset_probabilities() {
	if (asset_probabilities.size() == 0) {
		return;
	}

	//double sum = 0;
	//for (size_t i = 0; i < asset_probabilities.size(); i++) {
	//	sum += asset_probabilities[i];
	//	std::printf("%lu %lf\n", i, asset_probabilities[i]);
	//}
	double sum = asset_probabilities[asset_probabilities.size() - 1];
	std::printf("asset probs:\n");
	for (size_t i = 0; i < asset_probabilities.size(); i++) {
		asset_probabilities[i] /= sum;
		std::printf("%lu %lf\n",i, asset_probabilities[i]);
	}
	asset_probabilities[asset_probabilities.size() - 1] = 1;
}

template<typename random_generator>
AssetID 
GeneratorState<random_generator>::gen_asset() {
	if (asset_probabilities.size() > 0) {
		double amt = zero_one_dist(gen);

		for (size_t i = 0; i < asset_probabilities.size(); i++) {
			if (amt <= asset_probabilities[i]) {
				return i;
			}
		}
		throw std::runtime_error("invalid asset_probabilities!");
	}

	if (options.asset_bias == 0) {
		std::uniform_int_distribution<> asset_dist(0, options.num_assets - 1);
		return asset_dist(gen);
	}

	else {
		std::exponential_distribution<> asset_dist(options.asset_bias);
		return (static_cast<AssetID>(std::floor(asset_dist(gen)))) % options.num_assets;
	}
}

template<typename random_generator>
SignedTransaction 
GeneratorState<random_generator>::gen_bad_tx(const std::vector<double>& prices) {
	//std::uniform_int_distribution<> asset_dist(0, options.num_assets-1);
	OfferCategory category;
	category.type = OfferType::SELL;
	category.sellAsset = gen_asset();
	category.buyAsset = category.sellAsset;
	while (category.sellAsset == category.buyAsset) {
		category.buyAsset = gen_asset();
	}

	double exact_price = get_exact_price(prices, category);
	double bad_price = gen_bad_price(exact_price);

	SignedTransaction tx;
	tx.transaction.operations.push_back(make_sell_offer(gen_endowment(prices[category.sellAsset]), bad_price, category));
	tx.transaction.metadata.sourceAccount = gen_account();
	return tx;
}

template<typename random_generator>
SignedTransaction
GeneratorState<random_generator>::gen_account_creation_tx() {


	AccountID new_account_id = allocate_new_account_id();
	//AccountID new_account_id = state.num_active_accounts;
	//state.num_active_accounts++;

	SignedTransaction tx;
	tx.transaction.metadata.sourceAccount = gen_account();

	CreateAccountOp create_op;

	create_op.startingBalance = CREATE_ACCOUNT_MIN_STARTING_BALANCE;
	create_op.newAccountId = new_account_id;
	create_op.newAccountPublicKey = signer.get_public_key(new_account_id);

	tx.transaction.operations.push_back(TxTypeUtils::make_operation(create_op));

	for (AssetID i = 0; i < options.num_assets; i++) {
		MoneyPrinterOp money_printer_op;
		money_printer_op.asset = i;
		money_printer_op.amount = options.new_account_balance;
		tx.transaction.operations.push_back(TxTypeUtils::make_operation(money_printer_op));

		PaymentOp payment_op;
		payment_op.asset = i;
		payment_op.receiver = new_account_id;
		payment_op.amount = options.new_account_balance;
		tx.transaction.operations.push_back(TxTypeUtils::make_operation(payment_op));
	}
	return tx;
}

template<typename random_generator>
SignedTransaction 
GeneratorState<random_generator>::gen_payment_tx() {
	AccountID sender = gen_account();
	AccountID receiver = gen_account();
	AssetID asset = gen_asset();
	int64_t amount = gen_endowment(1);
	PaymentOp op;
	op.receiver = receiver;
	op.asset = asset;
	op.amount = amount;
	SignedTransaction tx;
	tx.transaction.operations.push_back(TxTypeUtils::make_operation(op));

	tx.transaction.metadata.sourceAccount = sender;
	
	return tx;
}

template<typename random_generator>
SignedTransaction
GeneratorState<random_generator>::gen_cancel_tx(const SignedTransaction& creation_tx) {
	const CreateSellOfferOp& creation_op = creation_tx.transaction.operations.at(0).body.createSellOfferOp();

	CancelSellOfferOp cancel_op;
	cancel_op.category = creation_op.category;
	cancel_op.offerId = creation_tx.transaction.metadata.sequenceNumber + 0; // +1 to point to the first offer of the transaction
	cancel_op.minPrice = creation_op.minPrice;

	SignedTransaction tx_out;
	tx_out.transaction.metadata.sourceAccount = creation_tx.transaction.metadata.sourceAccount;
	tx_out.transaction.operations.push_back(TxTypeUtils::make_operation(cancel_op));
	return tx_out;
}

template<typename random_generator>
bool 
GeneratorState<random_generator>::bad_offer_cancel() {
	std::uniform_real_distribution<> dist(0, 1);
	return dist(gen) < options.bad_offer_cancel_chance;
}

template<typename random_generator>
bool 
GeneratorState<random_generator>::good_offer_cancel() {
	std::uniform_real_distribution<> dist(0, 1);
	return dist(gen) < options.good_offer_cancel_chance;
}

bool is_cancellable(const SignedTransaction& tx) {
	if (tx.transaction.operations.at(0).body.type() == CREATE_SELL_OFFER) {
		return true;
	}
	return false;
}

template<typename random_generator>
std::pair<
		std::vector<SignedTransaction>,
		std::vector<bool>
	>
GeneratorState<random_generator>::gen_transactions(size_t num_txs, const std::vector<double>& prices) {
	//unsigned int num_txs = options.block_size * num_blocks;//options.num_blocks;

	std::vector<SignedTransaction> output;
	std::vector<bool> cancellation_flags;

	cancellation_flags.resize(num_txs, false);

//	std::vector<double> prices = gen_prices(state);

	int print_freq = 100000;
	int print_count = 0;
	while (output.size() < num_txs) {
		switch(gen_new_op_type()) {
			case OperationType::CREATE_SELL_OFFER:
				if (bad_frac > 1) {
					bad_frac -= 1.0;
					output.push_back(gen_bad_tx(prices));
					cancellation_flags.at(output.size() - 1) = bad_offer_cancel();
					print_count++;
				} else {
					auto asset_cycle = gen_asset_cycle();
					print_count += asset_cycle.size();
					bad_frac += asset_cycle.size() * options.bad_tx_fraction;
					auto tx_cycle = gen_good_tx_cycle(asset_cycle, prices);

					for (size_t i = output.size(); i < output.size() + tx_cycle.size(); i++) {
						if (i >= cancellation_flags.size()) {
							cancellation_flags.resize(output.size() + tx_cycle.size());
						}
						cancellation_flags.at(i) = good_offer_cancel();
					}
					output.insert(output.end(), tx_cycle.begin(), tx_cycle.end());
				}
				break;
			case OperationType::PAYMENT:
				
				output.push_back(gen_payment_tx());
				print_count++;
				break;

			case OperationType::CREATE_ACCOUNT:
				output.push_back(gen_account_creation_tx());
				print_count ++;
				break;

			default:
				throw std::runtime_error("invalid op type!!!");
		}

		if (print_count > print_freq) {
			std::printf("%lu\n", output.size());
			print_count -= print_freq;	
		}
	}
	return std::make_pair(output, cancellation_flags);
}

template<typename random_generator>
void GeneratorState<random_generator>::make_block(const std::vector<double>& prices) {
	normalize_asset_probabilities();

	if (2 * options.block_size < block_state.tx_buffer.size()) {
		std::printf("previous round overfilled block buffer.\n");

	} else {
		size_t new_txs_count = 2 * options.block_size - block_state.tx_buffer.size();

		auto [new_txs, new_cancellation_flags] = gen_transactions(new_txs_count, prices);
		block_state.tx_buffer.insert(block_state.tx_buffer.end(), new_txs.begin(), new_txs.end());
		block_state.cancel_flags.insert(block_state.cancel_flags.end(), new_cancellation_flags.begin(), new_cancellation_flags.end());
	}
		
	std::uniform_int_distribution<> idx_dist (0, options.block_size - 1);

	int num_swaps = options.block_size * options.block_boundary_crossing_fraction;

	for (int swap_cnt = 0; swap_cnt < num_swaps; swap_cnt++) {
		int idx_1 = idx_dist(gen);
		int idx_2 = idx_dist(gen) + options.block_size;

		std::swap(block_state.tx_buffer[idx_1], block_state.tx_buffer[idx_2]);
		std::swap(block_state.cancel_flags[idx_1], block_state.cancel_flags[idx_2]);
	}

	auto& txs = block_state.tx_buffer;
	auto& cancellation_flags = block_state.cancel_flags;

	for (unsigned int i = 0; i < options.block_size; i++) {
		uint64_t prev_seq_num = block_state.sequence_num_map[txs[i].transaction.metadata.sourceAccount];
		txs[i].transaction.metadata.sequenceNumber = (prev_seq_num + 1) << 8;
		block_state.sequence_num_map[txs.at(i).transaction.metadata.sourceAccount] ++;

		if (cancellation_flags.at(i)) {
			add_cancel_tx(gen_cancel_tx(txs.at(i)));
		}
	}

	ExperimentBlock output;
	output.insert(output.end(), txs.begin(), txs.begin() + options.block_size);
	
	std::printf("writing block %lu\n", block_state.block_number);

	std::string filename = output_directory + std::to_string(block_state.block_number) + ".txs";
	block_state.block_number ++;


	if (options.do_shuffle) {
		std::shuffle(output.begin(), output.end(), gen);
	}

	signer.sign_block(output);

	if (save_xdr_to_file(output, filename.c_str())) {
		throw std::runtime_error("was not able to save file!");
	}


	txs.erase(txs.begin(), txs.begin() + options.block_size);
	cancellation_flags.erase(cancellation_flags.begin(), cancellation_flags.begin() + options.block_size);

	auto cancel_txs = dump_current_round_cancel_txs();
	//auto cancel_txs_sz = cancel_txs.size();

	txs.insert(txs.end(), cancel_txs.begin(), cancel_txs.end());
	cancellation_flags.resize(txs.size(), false);
}

template<typename random_generator>
void 
GeneratorState<random_generator>::make_blocks() {
	std::vector<double> prices = gen_prices();

	for (size_t i = 0; i < options.num_blocks; i++) {
		make_block(prices);
		modify_prices(prices);
		print_prices(prices);
	}
}


/*
template<typename random_generator>
void 
GeneratorState<random_generator>::make_blocks() {
	
	std::vector<double> prices = gen_prices(state);

	auto [txs, cancellation_flags] = gen_transactions(2 * options.block_size, prices);
	std::unordered_map<AccountID, uint64_t> sequence_num_map;


	for (size_t i = 1; i <= options.num_blocks; i++) {
		std::printf("starting make block %lu\n", i);
		std::printf("txs size:%lu\n", txs.size());

		std::uniform_int_distribution<> idx_dist (0, options.block_size - 1);
		//for (unsigned int i = 0; i < 2.num_blocks - 1; i++) {
		int num_swaps = options.block_size * options.block_boundary_crossing_fraction;

		for (int swap_cnt = 0; swap_cnt < num_swaps; swap_cnt++) {
			int idx_1 = idx_dist(gen) ;//+ i * options.block_size;
			int idx_2 = idx_dist(gen) + (1) * options.block_size;

			std::swap(txs[idx_1], txs[idx_2]);
			std::swap(cancellation_flags[idx_1], cancellation_flags[idx_2]);
		}
		//}


		for (unsigned int i = 0; i < options.block_size; i++) {
			uint64_t prev_seq_num = sequence_num_map[txs[i].transaction.metadata.sourceAccount];
			txs[i].transaction.metadata.sequenceNumber = (prev_seq_num + 1) << 8;
			sequence_num_map[txs.at(i).transaction.metadata.sourceAccount] ++;

			if (cancellation_flags.at(i)) {
				state.add_cancel_tx(gen_cancel_tx(txs.at(i)));
			}
		}

		ExperimentBlock output;
		output.insert(output.end(), txs.begin(), txs.begin() + options.block_size);

		std::string filename = file_prefix + std::to_string(i) + ".txs";

		std::printf("writing block %lu\n", i);

		if (options.do_shuffle) {
			std::shuffle(output.begin(), output.end(), gen);
		}

		state.signer.sign_block(output);

		if (save_xdr_to_file(output, filename.c_str())) {
			throw std::runtime_error("was not able to save file!");
		}

		txs.erase(txs.begin(), txs.begin() + options.block_size);
		cancellation_flags.erase(cancellation_flags.begin(), cancellation_flags.begin() + options.block_size);

		auto cancel_txs = state.dump_current_round_cancel_txs();
		auto cancel_txs_sz = cancel_txs.size();

		txs.insert(txs.end(), cancel_txs.begin(), cancel_txs.end());
		cancellation_flags.resize(txs.size(), false);
		
		if (options.block_size > cancel_txs_sz) {
			auto [append, append_flags] = gen_transactions(state, options.block_size - cancel_txs_sz, prices);
			txs.insert(txs.end(), append.begin(), append.end());
			cancellation_flags.insert(cancellation_flags.end(), append_flags.begin(), append_flags.end());
		} else {
			//not really an error, but something to be aware of
			std::printf("cancellable sz is bigger than available block size!\n");
		}
	}
} */


} /* edce */


