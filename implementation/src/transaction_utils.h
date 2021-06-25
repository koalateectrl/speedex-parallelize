#pragma once 
#include "xdr/transaction.h"
#include "price_utils.h"
#include "merkle_trie.h"
#include "merkle_trie_utils.h"

#include <xdrpp/marshal.h>

#include <tbb/parallel_for.h>

#include <cstdint>

namespace edce {

struct TransactionUtils {

	static constexpr int TX_KEY_LEN = sizeof(AccountID) + sizeof(uint64_t);

	using TxDataMetadataT = CombinedMetadata<SizeMixin>;
	using TxDataValueT = FrozenValue;
	using TxDataTrieT = MerkleTrie<TX_KEY_LEN, TxDataValueT, TxDataMetadataT>;
	using FrozenTxDataTrieT = FrozenMerkleTrie<TX_KEY_LEN, TxDataValueT, TxDataMetadataT>;

	TransactionUtils() = delete; 	

	static void write_tx_trie_key(unsigned char* buf, const TransactionMetadata& tx_metadata) {

		size_t idx = 0;
		PriceUtils::write_unsigned_big_endian(buf, tx_metadata.sourceAccount, idx);
		idx += sizeof(AccountID);
		PriceUtils::write_unsigned_big_endian(buf, tx_metadata.sequenceNumber, idx);
	}

	static void write_tx_trie_key(unsigned char* buf, const AccountID& source_account, const uint64_t& sequence_number) {
		size_t idx = 0;
		PriceUtils::write_unsigned_big_endian(buf, source_account, idx);
		idx += sizeof(AccountID);
		PriceUtils::write_unsigned_big_endian(buf, sequence_number, idx);
	}

	static void make_tx_data_trie_nosigs(const std::vector<Transaction>& txs) {
		/*TxDataTrieT trie;
		auto num_txs = txs.size();
		//TODO could be somewhat parallelized, perhaps, if we parallelize insert in merkle_trie

		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, num_txs),
			[&txs, &trie] (auto r) {
				unsigned char key_buf[TX_KEY_LEN];
				for (auto  i = r.begin(); i < r.end(); i++) {
					TransactionUtils::write_tx_trie_key(key_buf, txs[i].metadata);

					auto buf = xdr::xdr_to_msg(txs[i]);
					const unsigned char* msg = (const unsigned char*) buf->data();
					trie.parallel_insert(key_buf, FrozenValue(msg, buf->size()));
				}
		});

		if (trie.size() != num_txs) {
			throw std::runtime_error("parallel insert error");
		}
		if (!trie.partial_metadata_integrity_check()) {
			throw std::runtime_error("metadata error in par ins");
		}*/
		/*for (int i = 0; i < num_txs; i++) {
			TransactionUtils::write_tx_trie_key(key_buf, txs[i].metadata);


			auto buf = xdr::xdr_to_msg(txs[i]);
			const unsigned char* msg = (const unsigned char*) buf->data();
			trie.insert(key_buf, FrozenValue(msg, buf->size()));
		}*/
		throw std::runtime_error("borked");
		//return trie.freeze_and_hash(nullptr);
	}

	static void make_tx_data_trie(const std::vector<SignedTransaction>& txs) {
		/*TxDataTrieT trie;
		int num_txs = txs.size();
		unsigned char key_buf[TX_KEY_LEN];
		//TODO could be somewhat parallelized, perhaps, if we parallelize insert in merkle_trie
		for (int i = 0; i < num_txs; i++) {
			TransactionUtils::write_tx_trie_key(key_buf, txs[i].transaction.metadata);


			auto buf = xdr::xdr_to_msg(txs[i]);
			const unsigned char* msg = (const unsigned char*) buf->data();
			trie.insert(key_buf, FrozenValue(msg, buf->size()));
		}*/
		throw std::runtime_error("borked");
		//return trie.freeze_and_hash(nullptr);

		//trie.parallel_get_hash(nullptr);

		//return FrozenTxDataTrieT(std::move(trie));
	}

};

} /* edce */