#pragma once
#include <cstdint>
#include <memory>

#include "merkle_work_unit_manager.h"
#include "operation_metadata.h"
#include "memory_database_view.h"
#include "database.h"
#include "account_modification_log.h"
#include "block_update_stats.h"

#include "edce_management_structures.h"

#include "xdr/transaction.h"
#include "xdr/ledger.h"

namespace edce {

template<typename SerialManager>
class SerialTransactionHandler {

protected:
	using Database = MemoryDatabase;

	SerialManager serial_manager;
	Database& account_database;

	//SerialAccountModificationLog& serial_account_log;

	template<typename DatabaseView>
	TransactionProcessingStatus process_operation(
		OperationMetadata<Database, DatabaseView>& metadata,
		const CreateAccountOp& op);
	template<typename DatabaseView>
	TransactionProcessingStatus process_operation(
		OperationMetadata<Database, DatabaseView>& metadata,
		const CreateSellOfferOp& op, 
		SerialAccountModificationLog& serial_account_log);
	template<typename DatabaseView>
	TransactionProcessingStatus process_operation(
		OperationMetadata<Database, DatabaseView>& metadata,
		const CancelSellOfferOp& op);
	template<typename DatabaseView>
	TransactionProcessingStatus process_operation(
		OperationMetadata<Database, DatabaseView>& metadata,
		const PaymentOp& op);
	template<typename DatabaseView>
	TransactionProcessingStatus process_operation(
		OperationMetadata<Database, DatabaseView>& metadata,
		const MoneyPrinterOp& op);

	//bool check_tx_format_parameters(Transaction tx);
	template<typename ViewType>
	Offer make_offer(
		const CreateSellOfferOp& op, 
		const OperationMetadata<Database, ViewType>& metadata) {
		Offer offer;
		offer.category = op.category;
		offer.offerId = metadata.operation_id;
		offer.owner = metadata.tx_metadata.sourceAccount;
		offer.amount = op.amount;
		offer.minPrice = op.minPrice;
		return offer;
	}

	void log_modified_accounts(const SignedTransaction& tx, SerialAccountModificationLog& serial_account_log);

public:

	SerialTransactionHandler(
		EdceManagementStructures& management_structures, SerialManager&& serial_manager)
		: serial_manager(std::move(serial_manager))
		, account_database(management_structures.db)
		//, serial_account_log(management_structures.account_modification_log) 
		{}

	void finish() {
		//std::printf("starting finish\n");
		serial_manager.finish_merge();
		//if (SerialManager::maintain_account_log) {
		//	std::printf("starting account log finish\n");
		//	serial_account_log.sanity_check();
		//	serial_account_log.finish();
		//}
		//std::printf("done finish\n");
	}

	void clear() {
		serial_manager.clear();
		//serial_account_log.clear_and_reset();
	}

	//SerialAccountModificationLog& extract_account_log() {
	//	return serial_account_log;
	//}

	SerialManager& extract_manager_view() {
		return serial_manager;
	}
};

//arg used for injecting alternate db views for running experiments
template<typename _BufferedMemoryDatabaseView = BufferedMemoryDatabaseView>
class SerialTransactionProcessor : public SerialTransactionHandler<ProcessingSerialManager> {

	using BaseT = SerialTransactionHandler<ProcessingSerialManager>;
	using BaseT::account_database;
	using BaseT::process_operation;
	using Database = typename BaseT::Database;
	using BufferedViewT = _BufferedMemoryDatabaseView;
	using UnbufferedViewT = UnbufferedMemoryDatabaseView;
	using BaseT::log_modified_accounts;
	using BaseT::serial_manager;


public:
	SerialTransactionProcessor(EdceManagementStructures& management_structures) 
		: BaseT(management_structures, ProcessingSerialManager(management_structures.work_unit_manager)) {}

	TransactionProcessingStatus process_transaction(
		const SignedTransaction& tx,
		BlockStateUpdateStatsWrapper& stats,
		SerialAccountModificationLog& serial_account_log);

	void unwind_operation(
		const OperationMetadata<Database, UnbufferedViewT>& metadata,
		const CreateSellOfferOp& op);

	void unwind_operation(
		const OperationMetadata<Database, UnbufferedViewT>& metadata,
		const CancelSellOfferOp& op);


	void unwind_transaction(
		const Transaction& tx,
		//TransactionResult& result,
		int last_valid_op);

	//using BaseT::extract_account_log;
	using BaseT::extract_manager_view;
};

template<typename ManagerViewType = MerkleWorkUnitManager>
class SerialTransactionValidator : public SerialTransactionHandler<ValidatingSerialManager<ManagerViewType>> {
	using SerialManagerT = ValidatingSerialManager<ManagerViewType>;
	using BaseT = SerialTransactionHandler<SerialManagerT>;
	using BaseT::account_database;
	using BaseT::process_operation;
	using Database = typename BaseT::Database;
	using BaseT::log_modified_accounts;
	using BaseT::serial_manager;
	//using BaseT::serial_account_log;
	using UnbufferedViewT = typename std::conditional<
								std::is_same<ManagerViewType, MerkleWorkUnitManager>::value,
									UnbufferedMemoryDatabaseView,
									LoadLMDBMemoryDatabaseView>
							::type;



	template<typename OpType, typename... Args>
	bool validate_operation(
		OperationMetadata<Database, UnbufferedViewT>& metadata,
		OpType op,
		Args&... args) {
		return process_operation(metadata, op, args...) 
			== TransactionProcessingStatus::SUCCESS;
	}




public:
	template<typename ...Args>
	SerialTransactionValidator(
		EdceManagementStructures& management_structures, 
		const WorkUnitStateCommitmentChecker& work_unit_state_commitment, 
		ThreadsafeValidationStatistics& main_stats,
		Args... lmdb_args) 
		: BaseT(
			management_structures, 
			std::move(
				ValidatingSerialManager<ManagerViewType>(
					management_structures.work_unit_manager, 
					work_unit_state_commitment, 
					main_stats,
					lmdb_args...))) {}

	template<typename ...Args>
	bool validate_transaction(
		const SignedTransaction& tx,
		BlockStateUpdateStatsWrapper& stats,
		SerialAccountModificationLog& serial_account_log,
		Args... lmdb_args);

	//void merge_in_other_serial_log(SerialTransactionValidator& other) {
	//	if (SerialManagerT::maintain_account_log) {
	//		serial_account_log.merge_in_other_serial_log(other.serial_account_log);
	//	}
	//	serial_manager.merge_in_other_serial_log(other.serial_manager);
	//}

	//using BaseT::extract_account_log;
	using BaseT::extract_manager_view;
};

}
