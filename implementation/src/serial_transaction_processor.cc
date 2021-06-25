#include "serial_transaction_processor.h"

#include "database.h"


namespace edce {

template class SerialTransactionProcessor<BufferedMemoryDatabaseView>;
template class SerialTransactionProcessor<UnlimitedMoneyBufferedMemoryDatabaseView>;

template class SerialTransactionValidator<MerkleWorkUnitManager>;
template class SerialTransactionValidator<LoadLMDBManagerView>;

template bool SerialTransactionValidator<MerkleWorkUnitManager>::validate_transaction<>(
	const SignedTransaction&, 
	BlockStateUpdateStatsWrapper& stats, 
	SerialAccountModificationLog& serial_account_log);
template bool SerialTransactionValidator<LoadLMDBManagerView>::validate_transaction<uint64_t>(
	const SignedTransaction&, 
	BlockStateUpdateStatsWrapper& stats, 
	SerialAccountModificationLog& serial_account_log, 
	uint64_t round_number);
//template<typename SerialManager, typename _BufferedMemoryDatabaseView>
//bool SerialTransactionHandler<SerialManager, _BufferedMemoryDatabaseView>::

bool check_tx_format_parameters(Transaction tx) {
	if (tx.metadata.sequenceNumber & RESERVED_SEQUENCE_NUM_LOWBITS) {
		return false;
	}
	return true;
}

/*
bool has_result(OperationType type) {
	switch(type) {
		default:
			return false;
	}
}*/

// Should only be called on committed transactions
template<typename SerialManager>
void SerialTransactionHandler<SerialManager>::log_modified_accounts(const SignedTransaction& signed_tx, SerialAccountModificationLog& serial_account_log) {
	if (!SerialManager::maintain_account_log) {
		return;
	}
	serial_account_log.log_new_self_transaction(signed_tx);

	auto& tx = signed_tx.transaction;

	int tx_op_count = tx.operations.size();

	for (int i = 0; i < tx_op_count; i++) {
		uint64_t op_seq_number = tx.metadata.sequenceNumber + i;

		auto op_type = tx.operations[i].body.type();

		switch(op_type) {
			case CREATE_ACCOUNT:
				serial_account_log.log_other_modification(
					tx.metadata.sourceAccount, 
					op_seq_number,
					tx.operations[i].body.createAccountOp().newAccountId);
				break;
			case CREATE_SELL_OFFER:
			case CANCEL_SELL_OFFER:
				break; //nothing to do here, we only modify self with these, and we've already logged those.
			case PAYMENT:
				serial_account_log.log_other_modification(
					tx.metadata.sourceAccount,
					op_seq_number,
					tx.operations[i].body.paymentOp().receiver);
				break;
			case MONEY_PRINTER:
				break; // nothing to do here, we only modify self with this, and new txs are already logged.
			default:
				std::printf("got op type %d\n", op_type);
				throw std::runtime_error("invalid op type");
		}
	}
}

template<typename ManagerViewType>
template<typename... Args>
bool SerialTransactionValidator<ManagerViewType>::validate_transaction(
	const SignedTransaction& signed_tx,
	BlockStateUpdateStatsWrapper& stats,
	SerialAccountModificationLog& serial_account_log,
	Args... lmdb_args) {

	auto& tx = signed_tx.transaction;
	int tx_op_count = tx.operations.size();
	//int result_count = result.results.size();
	//int result_idx = 0;

	if (!check_tx_format_parameters(tx)) {
		TX_INFO("transaction format parameters failed");
		return false;
	}

	account_db_idx source_account_idx;

	if (!account_database.lookup_user_id(
		tx.metadata.sourceAccount, &source_account_idx)) {
		TX_INFO("invalid userid lookup %lu", tx.metadata.sourceAccount);
		return false;
	}

	OperationMetadata<Database, UnbufferedViewT> op_metadata(
		tx.metadata, source_account_idx, account_database, lmdb_args...);


	auto id_status = op_metadata.db_view.reserve_sequence_number(
		source_account_idx, tx.metadata.sequenceNumber);

	if (id_status != TransactionProcessingStatus::SUCCESS) {
		TX_INFO("bad seq num on account %lu seqnum %lu", tx.metadata.sourceAccount, tx.metadata.sequenceNumber);
		return false;
	}

	//commit early because no reason not to, maybe marginally better cache perf
	op_metadata.db_view.commit_sequence_number(
		source_account_idx, tx.metadata.sequenceNumber);


	for (int i = 0; i < tx_op_count; i++) {

		op_metadata.operation_id = op_metadata.tx_metadata.sequenceNumber + i;
		auto op_type = tx.operations[i].body.type();
		/*if (has_result(op_type)) {
			if (result_idx >= result_count) {
				return false;
			}
			if (op_type != result.results[result_idx].body.type()) {
				return false;
			}
		}*/

		switch(op_type) {
			case CREATE_ACCOUNT:
				if (!validate_operation(
						op_metadata,
						tx.operations[i].body.createAccountOp())) {
					TX_INFO("create account failed");
					return false;
				}
				break;
			case CREATE_SELL_OFFER:
				if (!validate_operation(
						op_metadata,
						tx.operations[i].body.createSellOfferOp(),
						serial_account_log)) {
					return false;
				}
				break;
			case CANCEL_SELL_OFFER:
				if (!validate_operation(
						op_metadata,
						tx.operations[i].body.cancelSellOfferOp())) {
					return false;
				}
				break;
			case PAYMENT:
				if (!validate_operation(
						op_metadata,
						tx.operations[i].body.paymentOp())) {
					return false;
				}
				break;
			case MONEY_PRINTER:
				if (!validate_operation(
						op_metadata,
						tx.operations[i].body.moneyPrinterOp())) {
					TX_INFO("money printer failed");
					return false;
				}
				break;
			default:
				TX_INFO("garbage operation type");
				return false;
		}
	}
	log_modified_accounts(signed_tx, serial_account_log);
	op_metadata.commit(stats);
	return true;
}

std::string op_type_to_string(OperationType type) {
	switch(type) {
		case CREATE_ACCOUNT:
			return std::string("CREATE_ACCOUNT");
		case CREATE_SELL_OFFER:
			return std::string("CREATE_SELL_OFFER");
		case CANCEL_SELL_OFFER:
			return std::string("CANCEL_SELL_OFFER");
		case PAYMENT:
			return std::string("PAYMENT");
		case MONEY_PRINTER:
			return std::string("MONEY_PRINTER");
		default:
			throw std::runtime_error("invalid operation type");
	}
}

template<typename _BufferedMemoryDatabaseView>
TransactionProcessingStatus 
SerialTransactionProcessor<_BufferedMemoryDatabaseView>::process_transaction(
	const SignedTransaction& signed_tx,
	BlockStateUpdateStatsWrapper& stats,
	SerialAccountModificationLog& serial_account_log) {

	auto& tx = signed_tx.transaction;
	
	uint64_t tx_op_count = tx.operations.size();

	TX_INFO("starting process_transaction");

	if (!check_tx_format_parameters(tx)) {

		TX("invalid tx format");
		return TransactionProcessingStatus::INVALID_TX_FORMAT;
	}

	account_db_idx source_account_idx;

	if (!account_database.lookup_user_id(
			tx.metadata.sourceAccount, &source_account_idx)) {
		TX_INFO("invalid userid lookup %lu", tx.metadata.sourceAccount);
		return TransactionProcessingStatus::SOURCE_ACCOUNT_NEXIST;
	}

	auto seq_num_status = account_database.reserve_sequence_number(
		source_account_idx, tx.metadata.sequenceNumber);

	if (seq_num_status != TransactionProcessingStatus::SUCCESS) {
		TX_INFO("bad seq num on account %lu seqnum %lu", tx.metadata.sourceAccount, tx.metadata.sequenceNumber);

		return seq_num_status;
	}

	TX_INFO("successfully reserved seq num %lu", tx.metadata.sequenceNumber);

	OperationMetadata<Database, BufferedViewT> op_metadata(
		tx.metadata, source_account_idx, account_database);

	for (uint64_t i = 0; i < tx_op_count; i++) {
		TX_INFO("processing operation %lu, type %s", i, op_type_to_string(tx.operations[i].body.type()).c_str());

		op_metadata.operation_id = op_metadata.tx_metadata.sequenceNumber + i;

		//OperationResult wrapped_op_result;
		//wrapped_op_result.body.type(tx.operations[i].body.type());

		auto status = TransactionProcessingStatus::INVALID_OPERATION_TYPE;

		switch(tx.operations[i].body.type()) {
			case CREATE_ACCOUNT:
				status = process_operation(
					op_metadata,
					tx.operations[i].body.createAccountOp());
				break;
			case CREATE_SELL_OFFER:
				status = process_operation(
					op_metadata,
					tx.operations[i].body.createSellOfferOp(),
					serial_account_log);
				break;
			case CANCEL_SELL_OFFER:
				status = process_operation(
					op_metadata,
					tx.operations[i].body.cancelSellOfferOp());
				break;
			case PAYMENT:
				status = process_operation(
					op_metadata,
					tx.operations[i].body.paymentOp());
				break;
			case MONEY_PRINTER:
				status = process_operation(
					op_metadata,
					tx.operations[i].body.moneyPrinterOp());
				break;
		}
		if (status != TransactionProcessingStatus::SUCCESS) {

			TX_INFO("got bad status from an op");
			unwind_transaction(tx, i-1);
			op_metadata.unwind();
			account_database.release_sequence_number(source_account_idx, tx.metadata.sequenceNumber);
			return status;
		}
		//if (has_result(tx.operations[i].body.type())) {
		//	result.results.push_back(wrapped_op_result);
		//}
	}

	op_metadata.commit(stats);
	account_database.commit_sequence_number(source_account_idx, tx.metadata.sequenceNumber);
	log_modified_accounts(signed_tx, serial_account_log);
	return TransactionProcessingStatus::SUCCESS;
}

template<typename _BufferedMemoryDatabaseView>
void 
SerialTransactionProcessor<_BufferedMemoryDatabaseView>::unwind_transaction(
	const Transaction& tx,
//	TransactionResult& result,
	int last_valid_op) {

	account_db_idx source_account_idx;

	if (!account_database.lookup_user_id(
		tx.metadata.sourceAccount, &source_account_idx)) {
		throw std::runtime_error("cannot unwind tx from nonexistent acct");
	}

	OperationMetadata<Database, UnbufferedViewT> op_metadata(
		tx.metadata, source_account_idx, account_database);

	int op_idx = last_valid_op; //tx.operations.size() - 1;
	
	//unused
	//auto results_idx = result.results.size() -1;

	while (op_idx >= 0) {

		op_metadata.operation_id = op_metadata.tx_metadata.sequenceNumber + op_idx;


		const Operation& op = tx.operations[op_idx];
		//if (has_result(op.body.type())) {
		//	OperationResult& op_result = result.results[results_idx];
		//
		//	if (op.body.type() != op_result.body.type()) {
		//		throw std::runtime_error("mismatched op/result types");
		//	}
		//}
		TX("unwinding op of type %d at index %d of %u", op.body.type(), op_idx, tx.operations.size());
		switch(op.body.type()) {
			case CREATE_ACCOUNT:
				//unwind_operation(
			//		op_metadata,
			//		op.body.createAccountOp());
				break;

			case CREATE_SELL_OFFER:
				unwind_operation(
					op_metadata,
					op.body.createSellOfferOp());
				break;
			case CANCEL_SELL_OFFER:
				unwind_operation(
					op_metadata,
					op.body.cancelSellOfferOp());
				break;
			case PAYMENT:
			//	unwind_operation(
			//		op_metadata,
			//		op.body.paymentOp());
				break;
			default:
				throw std::runtime_error("cannot unwind unknown op type");

		}
		op_idx--;
	}
}

//process_operation is a no-op if it returns false.
//validate_operation can do whatever it wants in the case of returning false,
// as this will result in the whole block being reverted later anyways.
//unwind_operation should only be called on ops where process_operation
// returned true.


//CREATE_ACCOUNT

template<typename SerialManager>
template<typename DatabaseViewType>
TransactionProcessingStatus
SerialTransactionHandler<SerialManager>::process_operation(
	OperationMetadata<Database, DatabaseViewType>& metadata,
	const CreateAccountOp& op) {

	if (op.startingBalance < CREATE_ACCOUNT_MIN_STARTING_BALANCE) {
		return TransactionProcessingStatus::STARTING_BALANCE_TOO_LOW;
	}

	account_db_idx new_account_idx;
	auto status = metadata.db_view.create_new_account(op.newAccountId, op.newAccountPublicKey, &new_account_idx);
	if (status != TransactionProcessingStatus::SUCCESS) {
		return status;
	}
	status = metadata.db_view.transfer_available(
		metadata.source_account_idx,
		Database::NATIVE_ASSET,
		-op.startingBalance);
	if (status != TransactionProcessingStatus::SUCCESS) {
		return status;
	}
	status = metadata.db_view.transfer_available(
		new_account_idx,
		Database::NATIVE_ASSET,
		op.startingBalance);

	if (status != TransactionProcessingStatus::SUCCESS) {
		return status;
	}

	metadata.local_stats.new_account_count ++;
	return TransactionProcessingStatus::SUCCESS;
}

//template<typename Database>
//void SerialTransactionProcessor<Database>::unwind_operation(
//	const OperationMetadata& metadata,
//	const CreateAccountOp& op) {

	/*account_db_idx new_account_idx = -1;
	if (!account_database.lookup_user_id(op.newAccountId, &new_account_idx)) {
		throw std::runtime_error("cannot unwind nonexistent account creation");
	}
	account_database.lock_transfer_available(
		metadata.source_account_idx, 
		Database::NATIVE_ASSET,
		op.startingBalance);
	account_database.lock_transfer_available(
		new_account_idx,
		Database::NATIVE_ASSET,
		-op.startingBalance);*/

//}

//CREATE_SELL_OFFER

template<typename SerialManager>
template<typename DatabaseViewType>
TransactionProcessingStatus 
SerialTransactionHandler<SerialManager>::process_operation(
	OperationMetadata<MemoryDatabase, DatabaseViewType>& metadata,
	const CreateSellOfferOp& op,
	SerialAccountModificationLog& serial_account_log) {

	if (!WorkUnitManagerUtils::validate_category(op.category, serial_manager.get_num_assets())) {
		TX("invalid category");
		return TransactionProcessingStatus::INVALID_OFFER_CATEGORY;
	}

	if (!PriceUtils::is_valid_price(op.minPrice)) {
		TX("price out of bounds!");
		return TransactionProcessingStatus::INVALID_PRICE;
	}
	if (op.amount == 0) {
		TX("amount is 0!");
		return TransactionProcessingStatus::INVALID_AMOUNT;
	}

	int market_idx = serial_manager.look_up_idx(op.category);

	Offer to_add = make_offer(op, metadata);

	serial_manager.add_offer(market_idx, to_add, metadata, serial_account_log); // only modifies log in validation case

	auto status = metadata.db_view.escrow(metadata.source_account_idx, op.category.sellAsset, op.amount);
	if (status != TransactionProcessingStatus::SUCCESS) {
		TX("escrow failed, unwinding create sell offer: account %lu, account_db_idx %lu, asset %lu, op.amount %lu", 
			metadata.tx_metadata.sourceAccount, metadata.source_account_idx, op.category.sellAsset, op.amount);
		serial_manager.unwind_add_offer(market_idx, to_add);
		return status;
	}

	metadata.local_stats.new_offer_count ++;

	return TransactionProcessingStatus::SUCCESS;
}

template<typename _BufferedMemoryDatabaseView>
void 
SerialTransactionProcessor<_BufferedMemoryDatabaseView>::unwind_operation(
	const OperationMetadata<Database, UnbufferedViewT>& metadata,
	const CreateSellOfferOp& op) {

	int market_idx = serial_manager.look_up_idx(op.category);
	Offer to_remove = BaseT::make_offer(op, metadata);
	serial_manager.unwind_add_offer(
		market_idx, to_remove);
}

//CANCEL_SELL_OFFER

template<typename SerialManager>
template<typename DatabaseViewType>
TransactionProcessingStatus 
SerialTransactionHandler<SerialManager>::process_operation(
	OperationMetadata<Database, DatabaseViewType>& metadata,
	const CancelSellOfferOp& op) {
	int market_idx = serial_manager.look_up_idx(op.category);
	auto found_offer = serial_manager.delete_offer(
		market_idx, op.minPrice, metadata.tx_metadata.sourceAccount, op.offerId);

	if (found_offer) {
		auto status = metadata.db_view.escrow(metadata.source_account_idx, op.category.sellAsset, -found_offer -> amount);
		if (status != TransactionProcessingStatus::SUCCESS) {
			serial_manager.undelete_offer(market_idx, op.minPrice, metadata.tx_metadata.sourceAccount, op.offerId);
		} else {
			metadata.local_stats.cancel_offer_count++;
		}
		return status;
	}
	//TODO this doesn't distinguish between "the offer isn't present" and "somebody else already cancelled it"
	return TransactionProcessingStatus::CANCEL_OFFER_TARGET_NEXIST;
}

template<typename _BufferedMemoryDatabaseView>
void 
SerialTransactionProcessor<_BufferedMemoryDatabaseView>::unwind_operation(
	const OperationMetadata<Database, UnbufferedViewT>& metadata,
	const CancelSellOfferOp& op) {
	int market_idx = serial_manager.look_up_idx(op.category);
	serial_manager.undelete_offer(
		market_idx, op.minPrice, metadata.tx_metadata.sourceAccount, op.offerId);
}

//PAYMENT

template<typename SerialManager>
template<typename DatabaseViewType>
TransactionProcessingStatus 
SerialTransactionHandler<SerialManager>::process_operation(
	OperationMetadata<Database, DatabaseViewType>& metadata,
	const PaymentOp& op) {
	
	account_db_idx target_account_idx = -1;
	if (!metadata.db_view.lookup_user_id(
		op.receiver, &target_account_idx)) {
		TX("failed to find target account idx");
		return TransactionProcessingStatus::RECIPIENT_ACCOUNT_NEXIST;
	}

	auto status = metadata.db_view.transfer_available(
		metadata.source_account_idx, op.asset, -op.amount);
	if (status != TransactionProcessingStatus::SUCCESS) {
		return status;
	}
	status = metadata.db_view.transfer_available(
		target_account_idx, op.asset, op.amount);
	if (status != TransactionProcessingStatus::SUCCESS) {
		return status;
	}
	metadata.local_stats.payment_count ++;
	return TransactionProcessingStatus::SUCCESS;
}

//MONEY_PRINTER

template<typename SerialManager>
template<typename DatabaseViewType>
TransactionProcessingStatus
SerialTransactionHandler<SerialManager>::process_operation(
	OperationMetadata<Database, DatabaseViewType>& metadata,
	const MoneyPrinterOp& op) {

	if (op.amount < 0) {
		return TransactionProcessingStatus::INVALID_PRINT_MONEY_AMOUNT;
	}

	return metadata.db_view.transfer_available(
		metadata.source_account_idx, op.asset, op.amount);
}

} // namespace edce
