#pragma once

#include <cstdint>
#include <atomic>

#include <mutex>

#include <memory>

#include "xdr/types.h"
#include "xdr/database_commitments.h"

#include "simple_debug.h"

namespace edce {

/*
LMAO we don't need an escrow count, we just need to track available amounts.
escrow is implicitly logged by sell offers.
*/
class RevertableAsset {
	
public:
	using amount_t = int64_t;

private:

	using atomic_amount_t = std::atomic<amount_t>;

	using pointer_t = std::unique_ptr<std::atomic<amount_t>>;

	atomic_amount_t available;
	//atomic_amount_t escrowed;
	
	amount_t committed_available;
	//amount_t committed_escrow;

	bool commit_modified = true;

	bool checkpoint_modified = true;

	constexpr static auto read_order = std::memory_order_relaxed; // was acquire
	constexpr static auto write_order = std::memory_order_relaxed; // was release


public:

	RevertableAsset() 
		: available(0),
		//escrowed(0),
		committed_available(0) {}//,
		//committed_escrow(0) {}

	RevertableAsset(amount_t amount)
		: available(amount),
		committed_available(amount) {}

	/*
	RevertableAsset(std::pair<amount_t, amount_t> contents)
		: available(contents.first),
		escrowed(contents.second),
		committed_available(contents.first) {}//,
		committed_escrow(contents.second) {}
	*/
	//CANNOT BE CALLED CONCURRENTLY WITH ANYTHING ELSE

	RevertableAsset(RevertableAsset&& other)
		: available(other.available.load(read_order)),
		//escrowed(other.escrowed.load(std::memory_order_acquire)),
		committed_available(other.committed_available) {}//,
		//committed_escrow(other.committed_escrow) {}


	void stringify() const {
		std::printf("%lu (c: %lu) ", available.load(read_order), committed_available);
	}

	/*std::pair<amount_t, amount_t> get_committed_contents() {
		commit();
		return std::make_pair(committed_available, committed_escrow);
	}*/

	void escrow(const amount_t& amount) {
	//	escrowed.fetch_add(amount, std::memory_order_release);
		available.fetch_add(-amount, write_order);
	}

	void transfer_available(const amount_t& amount) {
		available.fetch_add(amount, write_order);
	}

	void transfer_escrow(const amount_t& amount) {
	//	escrowed.fetch_add(amount, std::memory_order_release);
	}

	bool conditional_escrow(const amount_t& amount) {
		if (amount > 0) {
			auto result = conditional_transfer_available(-amount);
			return result;
		} else {
			transfer_available(-amount);
			return true;
		}
	}

	/*bool conditional_escrow(const amount_t& amount) {
		if (amount > 0) {
			auto result = conditional_transfer_available(-amount);
			if (result) {
				escrowed.fetch_add(amount, std::memory_order_release);
			}
			return result;
		} else {
			auto result = conditional_transfer_escrow(amount);
			if (result) {
				available.fetch_add(-amount, std::memory_order_release);
			}
			return result;
		}
	}*/

/*
	bool conditional_escrow(amount_t amount) {
		if (amount > 0) {
			while (true) {
				amount_t current_available = available->load(
					std::memory_order_relaxed);
				amount_t tentative_available = current_available - amount;
				if (tentative_available < 0) {
					return false;
				}
				bool result = available->compare_exchange_weak(
					current_available, 
					tentative_available, 
					std::memory_order_release);
				if (result) {
					escrowed->fetch_add(amount, std::memory_order_release);
					return true;
				}
				__builtin_ia32_pause();
			}
		} else {
			while (true) {
				amount_t current_escrow = escrowed->load(
					std::memory_order_relaxed);
				amount_t tentative_escrow = current_escrow + amount;
				if (tentative_escrow < 0) {
					return false;
				}
				bool result = escrowed->compare_exchange_weak(
					current_escrow, 
					tentative_escrow, 
					std::memory_order_release);
				if (result) {
					available->fetch_add(-amount, std::memory_order_release);
					//available_uncommitted->fetch_add(-amount, std::memory_order_relaxed);
					//escrowed_uncommitted->fetch_add(amount, std::memory_order_relaxed);
					return true;
				}
				__builtin_ia32_pause();
			}
		}
	}*/

	//Another approach would be to subtract, see if the original value you subtracted
	// from is actually high enough, and apologize if not (undo).
	//THis creates the option to make txs that shouldn't fail fail, though.
	//Unclear which causes less contention.

	bool conditional_transfer_available(const amount_t& amount) {
		while (true) {
			amount_t current_available
				= available.load(std::memory_order_relaxed);
			amount_t tentative_available = current_available + amount;
			if (tentative_available < 0) {
				return false;
			}
			bool result = available.compare_exchange_weak(
				current_available, 
				tentative_available, 
				write_order);
			if (result) {
				return true;
			}
			__builtin_ia32_pause();
		}	
	}



	/*

	bool conditional_transfer_escrow(const amount_t& amount) {
		while (true) {
			amount_t current_escrow
				= escrowed.load(std::memory_order_relaxed);
			amount_t tentative_escrow = current_escrow + amount;
			if (tentative_escrow < 0) {
				return false;
			}
			bool result = escrowed.compare_exchange_weak(
				current_escrow, 
				tentative_escrow, 
				std::memory_order_release);
			if (result) {
				//escrowed_uncommitted->fetch_add(amount);
				return true;
			}
			__builtin_ia32_pause();
		}
	}*/

	/*bool conditional_escrow(amount_t amount) {
		std::lock_guard<std::mutex> lock(mtx);

		amount_t current_available
			= available.load(std::memory_order_relaxed);
		amount_t current_owned 
			= owned.load(std::memory_order_relaxed);
		amount_t tentative_available = current_available - amount;

		if (tentative_available < 0 || tentative_available > current_owned) {
			return false;
		}

		escrow(amount);
		return true;
	}

	bool conditional_transfer_available(amount_t amount) {
		std::lock_guard<std::mutex> lock(mtx);

		amount_t current_available
			= available.load(std::memory_order_relaxed);
		amount_t tentative_remaining = current_available - amount;
		if (tentative_remaining < 0) {
			return false;
		}
		transfer_available(amount);
		return true;
	}

	bool conditional_transfer_escrow(amount_t amount) {
		std::lock_guard<std::mutex> lock(mtx);

		amount_t current_available
			= available.load(std::memory_order_relaxed);
		amount_t current_owned 
			= owned.load(std::memory_order_relaxed);
		if (current_available + amount > current_owned) {
			return false;
		}
		transfer_escrow(amount);
		return true;

	}*/

	int64_t lookup_available_balance() {
		return available.load(read_order);
	}

	AssetCommitment produce_commitment(AssetID asset) const {
		return AssetCommitment(asset, committed_available);//, committed_escrow);
	}
	AssetCommitment tentative_commitment(AssetID asset) const {
		return AssetCommitment{asset, available.load(read_order)};
	}

	//NOT THREADSAFE, don't call commit and rollback and in_valid_state at
	// the same time as each other or as escrow/transfer
	amount_t commit() {
		//amount_t new_escrow = escrowed.load(std::memory_order_acquire);
		amount_t new_avail = available.load(read_order);
		//if (new_escrow != committed_escrow) {
		//	committed_escrow = new_escrow;
		//	commit_modified = true;
		//}
		if (new_avail != committed_available) {
			committed_available = new_avail;
			commit_modified = true;
			checkpoint_modified = true;
		}
		return committed_available;	
	}
	void rollback() {
		//escrowed.store(committed_escrow, std::memory_order_release);
		available.store(committed_available, write_order);
	}

	bool in_valid_state() {
	//	uint64_t escrowed_load = escrowed.load(std::memory_order_acquire);
		int64_t available_load = available.load(read_order);
		return /*(escrowed_load >= 0) &&*/ (available_load >= 0);
	}

	constexpr static uint8_t SERIALIZATION_BYTES = 8;

	/*
	bool empty() {
		return committed_escrow == 0 && committed_available == 0;
	}*/

	bool get_commit_modified() const {
		return commit_modified;
	}

	void clear_commit_modified_flag() {
		commit_modified = false;
	}

	bool get_checkpoint_modified() const {
		return checkpoint_modified;
	}

	void clear_checkpoint_modified_flag() {
		checkpoint_modified = false;
	}
};

}