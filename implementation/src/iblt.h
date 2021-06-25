#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <openssl/sha.h>
#include <shared_mutex>
#include <tbb/parallel_for.h>
#include "xdr/iblt_wire.h"

#include "tbb/global_control.h"


namespace edce {

//taken from "Biff (Bloom Filter) Codes: Fast Error Correction for Large Data Sets"
template<unsigned int NUM_HASH_FNS>
struct IBLTExpansionConstant {
};

template<>
struct IBLTExpansionConstant<3> {
	constexpr static double constant = 1.222;
};
template<>
struct IBLTExpansionConstant<4> {
	constexpr static double constant = 1.295; 
};

template<>
struct IBLTExpansionConstant<5> {
	constexpr static double constant = 1.425;
};
//this keeps going

template<unsigned int KEY_BYTES, unsigned int CHECKSUM_BYTES>
class IBLTCell{
	using raw_data_t = std::atomic<uint64_t>;

public:
	constexpr static unsigned int KEY_WORDS = (KEY_BYTES / 8) + ((KEY_BYTES % 8 == 0)?0:1);
	constexpr static unsigned int CHECKSUM_WORDS = (CHECKSUM_BYTES / 8) + ((CHECKSUM_BYTES % 8 == 0)?0:1);
private:
	constexpr static unsigned int TOTAL_WORDS = KEY_WORDS + CHECKSUM_WORDS;

	static_assert(CHECKSUM_WORDS <= 4, "longer and you need a hash w/ output longer than sha256");

	std::array<raw_data_t, KEY_WORDS + CHECKSUM_WORDS> raw_data;

	std::atomic<int32_t> count;



public:

	void compute_difference(IBLTCell& output, const std::vector<uint8_t>& iblt_wire, const int& start_idx, const int& wire_count) const {
		memcpy((unsigned char*)(output.raw_data.data()), iblt_wire.data() + start_idx, KEY_BYTES);
		memcpy((unsigned char*)(output.raw_data.data() + KEY_WORDS), iblt_wire.data() + start_idx + KEY_BYTES, CHECKSUM_BYTES);
		for (unsigned int i = 0; i < TOTAL_WORDS; i++) {
			output.raw_data[i].fetch_xor(raw_data[i].load(std::memory_order_acquire), std::memory_order_release);
		}

		output.count.store(count.load(std::memory_order_acquire) - wire_count, std::memory_order_release);
	}

	void insert_key(const std::array<uint64_t, KEY_WORDS>& key, const std::array<uint64_t, 4>& checksum) {
		do_xor(key, checksum);
		count.fetch_add(1, std::memory_order_release);
	}

	void do_xor(const std::array<uint64_t, KEY_WORDS>& key, const std::array<uint64_t, 4>& checksum) {
		for (unsigned int i = 0; i < KEY_WORDS; i++) {
			raw_data[i].fetch_xor(key[i], std::memory_order_release);
		}
		for (unsigned int i = 0; i < CHECKSUM_WORDS; i++) {
			raw_data[i + KEY_WORDS].fetch_xor(checksum[i], std::memory_order_release);
		}
	}

	void remove_pure_cell(const IBLTCell& other) {
		for (unsigned int i = 0; i < TOTAL_WORDS; i++) {
			raw_data[i].fetch_xor(other.raw_data[i].load(std::memory_order_acquire), std::memory_order_release);
		}
		count -= other.count;
	}

	void clear() {
		count = 0;
		for (unsigned int i = 0; i < TOTAL_WORDS; i++) {
			raw_data[i] = 0;
		}
	}

	// +1 for pure on positive side, -1 for pure on negative side, 0 otherwise
	//not threadsafe with modifications to raw_data
	int is_pure() {
		std::array<uint64_t, 4> expected_checksum;
		auto count_load = count.load(std::memory_order_acquire);
		if (!(count_load == 1 || count_load == -1)) {
			return 0;
		}

		compute_checksum((unsigned char*)(raw_data.data()), expected_checksum);
		auto res = memcmp((unsigned char*)(expected_checksum.data()), (unsigned char*)(raw_data.data() + KEY_WORDS), CHECKSUM_BYTES);

		if (res == 0) {
			return count_load;
		}
		return 0;
	}

	static void compute_checksum(const uint8_t* key_buf, std::array<uint64_t, 4>& checksum) {
		unsigned char hash_buf[KEY_BYTES + 1];
		memcpy(hash_buf, key_buf, KEY_BYTES);
		hash_buf[KEY_BYTES] = 0x42; // arbitrary constant
		SHA256(hash_buf, KEY_BYTES + 1, (unsigned char*)(checksum.data()));
	}

	const unsigned char* get_key() const {
		return (unsigned char*)(raw_data.data());
	}

	bool is_empty() const {
		if (count != 0) return false;
		for (unsigned int i = 0; i < CHECKSUM_WORDS; i++) {
			if (raw_data[KEY_WORDS + i].load(std::memory_order_acquire) != 0) {
				return false;
			}
		}
		return true;
	}

	int get_count() const {
		return count;
	}

	void append_serialization(IBLTWireFormat& output) const {
		output.cellCounts.push_back(count);
		auto* key_ptr = (unsigned char*)(raw_data.data());
		auto* checksum_ptr = (unsigned char*)(raw_data.data() + KEY_WORDS);
		output.rawData.insert(output.rawData.end(), key_ptr, key_ptr + KEY_BYTES);
		output.rawData.insert(output.rawData.end(), checksum_ptr, checksum_ptr + CHECKSUM_BYTES);
	}
};


template<unsigned int KEY_BYTES, unsigned int CHECKSUM_BYTES, unsigned int MAX_RECOVERABLE_ELEMENTS, unsigned int NUM_HASH_FNS>
class IBLT {
	//thank u stackoverflow
	constexpr static unsigned int ceil(double number) {
		unsigned int int_cast = static_cast<unsigned int>(number);
		if (int_cast == static_cast<double>(int_cast)) {
			return int_cast;
		}
		return int_cast + ((number > 0)? 1 : 0);
	}


	constexpr static double OVERSIZE_FACTOR = 1.1;
	constexpr static unsigned int REQUIRED_NUM_CELLS = ceil(MAX_RECOVERABLE_ELEMENTS * OVERSIZE_FACTOR * IBLTExpansionConstant<NUM_HASH_FNS>::constant);
	constexpr static unsigned int CELLS_PER_REGION = (REQUIRED_NUM_CELLS % NUM_HASH_FNS == 0)?(REQUIRED_NUM_CELLS / NUM_HASH_FNS) : ((REQUIRED_NUM_CELLS / NUM_HASH_FNS) + 1);
	constexpr static unsigned int NUM_CELLS = CELLS_PER_REGION * NUM_HASH_FNS;

	using cell_t = IBLTCell<KEY_BYTES, CHECKSUM_BYTES>;

	constexpr static unsigned int KEY_WORDS = cell_t::KEY_WORDS;
	constexpr static unsigned int CHECKSUM_WORDS = cell_t::CHECKSUM_WORDS;

	mutable std::shared_mutex serialize_mutex;

	std::array<cell_t, NUM_CELLS> cells;

	static_assert(NUM_HASH_FNS < 20, "if NUM_HASH_FNS gets huge the index calculation will not be as close to uniformly random");

	static std::array<unsigned int, NUM_HASH_FNS> make_checksum_and_indices(const unsigned char* key_buf, std::array<uint64_t, 4>& checksum) {
		cell_t::compute_checksum(key_buf, checksum);

		return make_indices(key_buf);
	}

	static std::array<unsigned int, NUM_HASH_FNS> make_indices(const unsigned char* key_buf) {
		unsigned int hash_buf_len = KEY_BYTES + 1;
		unsigned char hash_buf[hash_buf_len];

		std::array<uint64_t, 4> index_hash;
		memcpy(hash_buf, key_buf, KEY_BYTES);
		hash_buf[KEY_BYTES] = 0xFF;

		SHA256(hash_buf, hash_buf_len, (unsigned char*) (index_hash.data()));

		std::array<unsigned int, NUM_HASH_FNS> output_idxs;
		for (unsigned int i = 0; i < NUM_HASH_FNS; i++) {
			output_idxs[i] = (index_hash[i] % CELLS_PER_REGION) + CELLS_PER_REGION * i;
			if (NUM_HASH_FNS - i > 4) {
				index_hash[i] /= CELLS_PER_REGION;
			}
		}
		return output_idxs;
	}
public:

	using key_list_t = std::vector<std::array<unsigned char, KEY_BYTES>>;

	IBLT()
		: serialize_mutex(),
		cells() {}

	void insert_key(unsigned char* key_buf) {
		std::shared_lock lock(serialize_mutex);

		std::array<uint64_t, KEY_WORDS> aligned_key;
		memcpy((unsigned char*)(aligned_key.data()), key_buf, KEY_BYTES);

		std::array<uint64_t, 4> checksum;

		auto idxs = make_checksum_and_indices(key_buf, checksum);

		for (unsigned int i = 0; i < NUM_HASH_FNS; i++) {
			auto idx = idxs[i];
			cells[idx].insert_key(aligned_key, checksum);
		}
	}

	bool compute_difference(const IBLTWireFormat& other_iblt, IBLT& difference_out) const {
		std::lock_guard lock(serialize_mutex);

		if (other_iblt.cellCounts.size() != NUM_CELLS) {
			return false;
		}
		if (other_iblt.rawData.size() != NUM_CELLS * (KEY_BYTES + CHECKSUM_BYTES)) {
			return false;
		}

		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, NUM_CELLS),
			[this, &other_iblt, &difference_out] (auto r) {
				for (unsigned int i = r.begin(); i < r.end(); i++) {
					cells[i].compute_difference(difference_out.cells[i], other_iblt.rawData, i * (KEY_BYTES + CHECKSUM_BYTES), other_iblt.cellCounts[i]);
				}
			});
		return true;
	}

	IBLTWireFormat serialize() const {
		std::lock_guard lock(serialize_mutex);
		IBLTWireFormat output;
		for (unsigned int i = 0; i < NUM_CELLS; i++) {
			cells[i].append_serialization(output);
		}
		return output;
	}

	bool decode(key_list_t& output_positive_keys, key_list_t& output_negative_keys) {
		std::lock_guard lock_serialize(serialize_mutex);

		std::mutex decoded_key_mutex;

		std::atomic_flag changed;

		do {
			changed.clear();
			for (unsigned int region = 0; region < NUM_HASH_FNS; region++) {
				tbb::parallel_for(
					tbb::blocked_range<std::size_t>(0, CELLS_PER_REGION),
					[this, region, &changed, &decoded_key_mutex, &output_positive_keys, &output_negative_keys] (auto r) {
						for (unsigned int i = r.begin(); i < r.end(); i++) {
							auto idx = region * CELLS_PER_REGION + i;
							auto purity = cells[idx].is_pure();
							if (purity != 0) {
								//found a pure cell
								auto indices = make_indices(cells[idx].get_key());
								changed.test_and_set();
								for (unsigned int region_iter = 0; region_iter < NUM_HASH_FNS; region_iter++) {
									if (region_iter != region) {
										cells[indices[region_iter]].remove_pure_cell(cells[idx]);
									}
								}

								if (purity == 1) {
									std::lock_guard lock(decoded_key_mutex);
									output_positive_keys.emplace_back();
									memcpy(output_positive_keys.back().data(), cells[idx].get_key(), KEY_BYTES);
								} else if (purity == -1) {
									std::lock_guard lock(decoded_key_mutex);
									output_negative_keys.emplace_back();
									memcpy(output_negative_keys.back().data(), cells[idx].get_key(), KEY_BYTES);
								} else {
									throw std::runtime_error("purity invalid response");
								}
								cells[idx].clear();
							}
						}
					});
			}
		} while (changed.test_and_set());

		std::atomic_flag nonempty_cell;
		nonempty_cell.clear();

		tbb::parallel_for(
			tbb::blocked_range<std::size_t>(0, NUM_CELLS),
			[this, &nonempty_cell] (auto r) {
				for (auto i = r.begin(); i < r.end(); i++) {
					if (!cells[i].is_empty()) {
						nonempty_cell.test_and_set();
					}
					return;
				}

			});
		return !nonempty_cell.test_and_set();
	}
};

}