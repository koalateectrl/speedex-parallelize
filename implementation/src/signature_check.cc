#include "signature_check.h"

#include <chrono>

namespace edce {

bool SignatureCheck::check(const SignedTransaction& arg) {
	auto buf = xdr::xdr_to_msg(arg.transaction);

	const unsigned char* msg = (const unsigned char*) buf->data();
	auto msg_len = buf->size();

	auto pk = db.get_pk(arg.transaction.metadata.sourceAccount);
	if (!pk) {
		return false;
	}

	int sig_check = crypto_sign_verify_detached(arg.signature.data(), msg, msg_len, pk->data());
	return sig_check == 0;
}

void BufferedSignatureCheck::run() {
	TransactionBufferManager::buffer_ptr checked_sigs_buffer = checked_sigs.get_empty_buffer();
	TransactionBufferManager::buffer_ptr unchecked_sigs_buffer = unchecked_sigs.get_full_buffer();

	auto start_time = std::chrono::high_resolution_clock::now();
	while (true) {
		bool unchecked_sigs_buffer_empty = false;
		bool checked_sigs_buffer_full = false;

		auto& tx = unchecked_sigs_buffer->remove(unchecked_sigs_buffer_empty);

		if (check(tx)) {
			checked_sigs_buffer_full = checked_sigs_buffer->insert(tx);
		}

		if (checked_sigs_buffer_full) {
			checked_sigs.return_full_buffer(std::move(checked_sigs_buffer));
			checked_sigs_buffer = checked_sigs.get_empty_buffer();
		}

		if (unchecked_sigs_buffer_empty) {
			auto end_time = std::chrono::high_resolution_clock::now();

			double elapsed_time = ((double) std::chrono::duration_cast<std::chrono::microseconds>(end_time-start_time).count()) / 1000000;

			std::printf("sig check:cleared a tx buffer in %f (might have had to wait on checked_sigs_buffer)\n", elapsed_time);

			unchecked_sigs.return_empty_buffer(std::move(unchecked_sigs_buffer));
			unchecked_sigs_buffer = unchecked_sigs.get_full_buffer();
			start_time = std::chrono::high_resolution_clock::now();
		}

	}
}

} /* edce */