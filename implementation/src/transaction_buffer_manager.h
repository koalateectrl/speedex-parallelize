#pragma once

#include "xdr/transaction.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <condition_variable>

namespace edce {

template<unsigned int buf_size>
class TransactionBuffer {
	using buffer_type = std::array<SignedTransaction, buf_size>;

	buffer_type transactions;
	unsigned int idx;
public:
	TransactionBuffer() : transactions(), idx(0) {}

	//returns true if this call to insert filled buffer
	bool insert(const SignedTransaction& tx) {
		transactions[idx] = tx;
		idx++;
		return idx >= buf_size;
	}

	//empty is true if this call empties buffer
	const SignedTransaction& remove(bool& empty) {
		idx--;
		empty = idx <= 0;
		return transactions[idx];
	}

	int size() {
		return idx;
	}
};

class TransactionBufferManager {
	constexpr static int MAX_NUM_BUFFERS = 20;

public:
	constexpr static int BUF_SIZE = 10000;
	using buffer_type = TransactionBuffer<BUF_SIZE>;
	using buffer_ptr = std::unique_ptr<buffer_type>;
private:
	std::mutex modify_mtx, full_buffer_return_wait_mtx, full_buffer_get_wait_mtx;
	std::condition_variable cv_wait_full_return, cv_wait_full_get;

	std::vector<buffer_ptr> empty_buffers;
	std::vector<buffer_ptr> full_buffers;

public:

	buffer_ptr get_empty_buffer();
	void return_empty_buffer(buffer_ptr ptr);
	
	buffer_ptr get_full_buffer();
	void return_full_buffer(buffer_ptr ptr);



};


} /* edce */