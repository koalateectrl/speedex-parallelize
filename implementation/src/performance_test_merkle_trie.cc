#include "merkle_trie.h"
#include "price_utils.h"
#include "utils.h"
#include "account_modification_log.h"
#include "account_merkle_trie.h"


#include <cstdint>

#include "xdr/types.h"

using namespace edce;


void account_log_insert_time(uint64_t num_accounts) {


	SignedTransaction tx;
	tx.transaction.operations.resize(1);
	Operation op;
	while(true) {
		auto timestamp = init_time_measurement();

		AccountModificationLog log;

		SerialAccountModificationLog serial_log(log, 0);

		for (uint64_t i = 0; i < num_accounts; i++) {
//			if (i % 10'000'000 == 0) {
//				std::printf("%lu\n", i);
//			}
			serial_log.log_self_modification(i, i);
			serial_log.log_other_modification(i, i, i);
		//	SignedTransaction tx;
		//	Operation op;
			op.body.type(PAYMENT);
			op.body.paymentOp().receiver = i;
			op.body.paymentOp().amount = 100;
			op.body.paymentOp().asset = 5;

			tx.transaction.metadata.sourceAccount = i;


			tx.transaction.operations[0] = op;
			serial_log.log_new_self_transaction(tx);
		}

		log.merge_in_log_batch();

		//serial_log.finish();
		

		std::printf("log time: %lf\n", measure_time(timestamp));

		Hash hash;

		log.freeze_and_hash(hash);

		std::printf("hash time: %lf\n", measure_time(timestamp));
	}

}


using InsertValueT = XdrTypeWrapper<Offer>;

void insert_time(uint64_t num_insertions) {
	using MT = MerkleTrie<8, InsertValueT, CombinedMetadata<SizeMixin>>;
	using prefix_t = typename MT::prefix_t;

	prefix_t key;


	while(true) {
		MT trie;
	//	trie.print_offsets();
		auto timestamp = init_time_measurement();
		for (uint64_t i = 0; i < num_insertions; i++) {
			PriceUtils::write_unsigned_big_endian(key, i);
			InsertValueT offer;
			trie.insert(key, offer);
		}

		std::printf("time: %lf\n", measure_time(timestamp));

		if (trie.size() != num_insertions) {
			throw std::runtime_error("size error!");
		}
	}
}

void insert_time_account(uint64_t num_insertions) {
	using MT = AccountTrie<InsertValueT>;
	while(true) {
		MT trie;
		auto serial_trie = trie.open_serial_subsidiary();
		auto timestamp = init_time_measurement();
		for (uint64_t i = 0; i < num_insertions; i++) {
			InsertValueT offer;
			serial_trie.insert(i, offer);
		}

		std::printf("time: %lf\n", measure_time(timestamp));

		if (serial_trie.size() != num_insertions) {
			throw std::runtime_error("size error!");
		}
	}
}

struct ApplyLambda {

	void operator()(Offer& offer) {
		offer.amount ++;
	}
};

struct CheckLambda {
	uint64_t amount_comparison = 1;
	void operator() (const Offer& offer) {
		if (offer.amount != amount_comparison) {
			std::printf("got offer amount %lu\n", offer.amount);
			throw std::runtime_error("invalid offer amount!");
		}
	}
};

void accumulate_values_time(uint64_t num_values) {
	AccountModificationLog log;

	SerialAccountModificationLog serial_log(log, 1);

	for (uint64_t i = 0; i < num_values; i++) {
		if (i % 10'000'000 == 0) {
			std::printf("%lu\n", i);
		}
		serial_log.log_self_modification(i, i);
		serial_log.log_other_modification(i, i, i);
		SignedTransaction tx;
		Operation op;
		op.body.type(PAYMENT);
		op.body.paymentOp().receiver = i;
		op.body.paymentOp().amount = 100;
		op.body.paymentOp().asset = 5;

		tx.transaction.metadata.sourceAccount = i;


		tx.transaction.operations.push_back(op);
		serial_log.log_new_self_transaction(tx);
	}

	log.merge_in_log_batch();
	//serial_log.finish();

	std::printf("made test setup\n");

	while (true) {

		auto timestamp = init_time_measurement();
	
		
/*		AccountModificationBlock block = log.template coroutine_parallel_accumulate_values<AccountModificationBlock>();

		float res = measure_time(timestamp);
		std::printf("coroutine acc time: %lf\n", res);

		if (block.size() != log.size()) {
			throw std::runtime_error("invalid");
		}
*/		AccountModificationBlock block2 = log.template parallel_accumulate_values<AccountModificationBlock>();
		float res2 = measure_time(timestamp);
		std::printf("regular acc time: %lf\n", res2);

		if (log.size() != block2.size()) {
			throw std::runtime_error("size mismatch!");
		}
		block2.clear();
		float res3 = measure_time(timestamp);
		std::printf("delete time %lf\n", res3);
	
	
	}

}

void apply_time(uint64_t num_insertions) {
	using ValueType = XdrTypeWrapper<Offer>;
	using MT = MerkleTrie<8, ValueType, CombinedMetadata<SizeMixin>>;
	using prefix_t = typename MT::prefix_t;

	prefix_t key;

	ValueType value;

	MT trie;

	for (uint64_t i = 0; i < num_insertions; i++) {
		PriceUtils::write_unsigned_big_endian(key, i);
		trie.insert(key, value);
	}

	if (trie.size() != num_insertions) {
		throw std::runtime_error("size error!");
	}

	std::printf("made test trie\n");

	CheckLambda checker;

	while(true) {
		ApplyLambda lambda;

		auto timestamp = init_time_measurement();



		trie.coroutine_apply(lambda);

		float result = measure_time(timestamp);

		std::printf("coroutine apply time: %lf\n", result);

		/*trie.apply(checker);
		checker.amount_comparison++;

		measure_time(timestamp);

		trie.apply(lambda);

		result = measure_time(timestamp);

		std::printf("regular apply time: %lf\n", result);

		trie.apply(checker);
		checker.amount_comparison++;*/
	}
}


int main(int argc, char const *argv[])
{
//	accumulate_values_time(20'000'000);
	//auto timestamp = init_time_measurement();
	insert_time(1'000'000);
	
	//account_log_insert_time(1'000'000);
	//float result = measure_time(timestamp);
	//std::printf("insert time: %lf\n", result);
	return 0;
}
