#pragma once

//POTENTIAL OPTIMIZATION:
/*
This kind of scheme produces shorter proofs if entropy is concentrated
in particular bits.  Put some extractor (deterministic, doesn't need
to be crypto secure, or just use account_name) as first few bits.

This should be done outside of this merkle trie impl.
*/
//#include <sstream>
#include <iomanip>

#include "price_utils.h"
#include <cstdint>
#include <mutex>
#include <xdrpp/marshal.h>
#include <compare>

#include <atomic>

#include <tbb/blocked_range.h> // to get tbb::split

#include <xdrpp/endian.h>


namespace edce {

inline static size_t __num_prefix_bytes(const unsigned int x){ return ((x/8) + (x % 8 == 0?0:1));}

struct PrefixLenBits {

	uint16_t len;
	size_t num_prefix_bytes() const{
		return __num_prefix_bytes(len);
	}

	size_t num_fully_covered_bytes() const {
		return len / 8;
	}

	std::strong_ordering operator<=>(const PrefixLenBits& other) const {
		return len <=> other.len;
	}

	bool operator==(const PrefixLenBits& other) const = default;

	PrefixLenBits operator+(const uint16_t other_bits) const {
		PrefixLenBits out;
		out.len = len + other_bits;
		return out;
	}
};

template<uint16_t MAX_LEN_BYTES, uint8_t BRANCH_BITS>
struct prefix_type {
	static_assert(!xdr::is_big_endian, "big endian is unimplemented");
	static_assert(BRANCH_BITS == 4, "unimplemented otherwise");

	constexpr static uint16_t WORDS = (MAX_LEN_BYTES / 8) + (MAX_LEN_BYTES % 8 == 0?0:1); //round up
	std::array<uint64_t, WORDS> data;

	constexpr static uint16_t MAX_LEN_BITS = 8 * MAX_LEN_BYTES;

	constexpr static uint64_t BRANCH_MASK = (((uint64_t)1) << (BRANCH_BITS)) - 1;

	prefix_type()
		: data() {
			data.fill(0);
		}

	prefix_type(const std::array<unsigned char, MAX_LEN_BYTES>& input)
		: data() {
			data.fill(0);
			auto* ptr = reinterpret_cast<unsigned char*>(data.data());
			memcpy(ptr, input.data(), MAX_LEN_BYTES);
	}

	PrefixLenBits get_prefix_match_len(
		const PrefixLenBits& self_len, 
		const prefix_type& other, 
		const PrefixLenBits& other_len) const {

		size_t res = MAX_LEN_BYTES * 8;

		for (size_t i = 0; i < WORDS; i++) {
			uint64_t local = data[i] ^ other.data[i];
			if (local) {

				size_t word_offset = __builtin_ctzll(local);
				word_offset -= word_offset % 8;

				if ((((local >> word_offset) & 0xF0) == 0) && (((local >> word_offset) & 0x0F) != 0)){
					word_offset += 4;
				}
				res = std::min((i * 64) + word_offset, res);// __builtin_ctzll(local), res);
				break;
			}
		}
		uint16_t res_final = res - res % BRANCH_BITS;
		return std::min({PrefixLenBits{res_final}, self_len, other_len});
	}

	unsigned char get_branch_bits(const PrefixLenBits branch_point) const {
		if (branch_point.len >= MAX_LEN_BITS) {
			std::printf("Bad branch bits was %u\n", branch_point.len);
			throw std::runtime_error("can't branch beyond end");
		}

		uint16_t word_idx = branch_point.len / 64;
		//uint16_t word_offset = (branch_point.len % 64);

		uint16_t byte_offset = (branch_point.len % 64) / 8;

		uint8_t byte = (data[word_idx] >> (8 * byte_offset)) & 0xFF;


		//uint64_t selected_bits = data[word_idx] & (BRANCH_MASK << word_offset);

		return (branch_point.len % 8 == 0 ? byte >> 4 : byte) & 0x0F;
	}

	void truncate(const PrefixLenBits truncate_point) {
		if (truncate_point.len >= MAX_LEN_BITS) {
			throw std::runtime_error("can't branch beyond end");
		}

		uint16_t word_idx = truncate_point.len / 64;
		uint16_t byte_offset = (truncate_point.len % 64) / 8;
		uint16_t word_offset = 8 * byte_offset;

		uint64_t truncate_mask = (((uint64_t)1) << word_offset) - 1;
		if (truncate_point.len % 8 != 0) {
			truncate_mask |= ((uint64_t)0xF0) <<word_offset;
		}
		data[word_idx] &= truncate_mask;
		for (size_t i = word_idx + 1; i < WORDS; i++) {
			data[i] = 0;
		}
	}


	unsigned char& operator[](size_t i) {
		unsigned char* data_ptr = reinterpret_cast<unsigned char*>(data.data());
		return data_ptr[i];
	}

	unsigned char operator[](size_t i) const {
		const unsigned char* data_ptr = reinterpret_cast<const unsigned char*>(data.data());
		return data_ptr[i];
	}

	void set_byte(size_t i, unsigned char byte) {
			if (i >= MAX_LEN_BYTES) {
			throw std::runtime_error("invalid prefix array access!");
		}

		unsigned char* data_ptr = reinterpret_cast<unsigned char*>(data.data());
		data_ptr[i] = byte;
//		return data_ptr[i];
	}

	unsigned char& at(size_t i) {
		if (i >= MAX_LEN_BYTES) {
			throw std::runtime_error("invalid prefix array access!");
		}

		unsigned char* data_ptr = reinterpret_cast<unsigned char*>(data.data());
		return data_ptr[i];
	}

	void set_max() {
		data.fill(UINT64_MAX);
	}

	void clear() {
		data.fill(0);
	}

	xdr::opaque_array<MAX_LEN_BYTES> get_bytes_array() const {
		xdr::opaque_array<MAX_LEN_BYTES> out;
		const unsigned char* ptr = reinterpret_cast<const unsigned char*>(data.data());

		memcpy(out.data(), ptr, MAX_LEN_BYTES);
		return out;
	}

	std::vector<unsigned char> get_bytes(const PrefixLenBits prefix_len) const {
		std::vector<unsigned char> bytes_out;

		const unsigned char* ptr = reinterpret_cast<const unsigned char*>(data.data());

		bytes_out.insert(bytes_out.end(), ptr, ptr + prefix_len.num_prefix_bytes());
 
		//for (size_t i = 0; i < prefix_len.num_prefix_bytes(); i++) {
		//	bytes_out.push_back(get_byte(i));

			//bytes_out.insert((*this)[i]);
		//}
		//const unsigned char* bytes_ptr = reinterpret_cast<const unsigned char*>(data.data());
		//bytes_out.insert(bytes_out.end(), bytes_ptr, bytes_ptr + prefix_len.num_prefix_bytes());
		return bytes_out;
	}

	constexpr static size_t size() {
		return MAX_LEN_BYTES;
	}

	void set_from_raw(const unsigned char* src, size_t len) {
		auto* dst = reinterpret_cast<unsigned char*>(data.data());
		if (len > MAX_LEN_BYTES) {
			throw std::runtime_error("len is too long!");
		}
		memcpy(dst, src, len); 
	}


	constexpr static PrefixLenBits len() {
		return PrefixLenBits{MAX_LEN_BITS};
	}

	std::strong_ordering operator<=>(const prefix_type& other) const {

		if (&other == this) return std::strong_ordering::equal;


		//TODO try the other candidate(compare word by word, in loop);

		auto res = memcmp(reinterpret_cast<const unsigned char*>(data.data()), reinterpret_cast<const unsigned char*>(other.data.data()), MAX_LEN_BYTES);
		if (res < 0) {
			return std::strong_ordering::less;
		}
		if (res > 0) {
			return std::strong_ordering::greater;
		}
		return std::strong_ordering::equal;
	}

	bool operator==(const prefix_type& other) const = default;

	std::string to_string(const PrefixLenBits len) const {
		//auto* ptr = reinterpret_cast<const unsigned char*>(data.data());
		
		auto bytes = get_bytes(len);
		return DebugUtils::__array_to_str(bytes.data(), len.num_prefix_bytes());
	}
};



class SpinMutex {
	mutable std::atomic<bool> flag;
public:
	void lock() const {
		while(true) {
			bool res = flag.load(std::memory_order_relaxed);

			if (!res) {
				bool expect = false;
				if (flag.compare_exchange_weak(expect, true, std::memory_order_acquire)) {
					return;
				}
			}
			__builtin_ia32_pause();
		}
	}

	void unlock() const {
		flag.store(false, std::memory_order_release);
	}
};

class SpinLockGuard {
	const SpinMutex& mtx;
public:
	SpinLockGuard(const SpinMutex& mtx) 
		: mtx(mtx) {
			mtx.lock();
		}
	~SpinLockGuard() {
		mtx.unlock();
	}
};

template<typename prefix_t>
static void write_node_header(std::vector<unsigned char>& buf, const prefix_t prefix, const PrefixLenBits prefix_len_bits) {

	PriceUtils::append_unsigned_big_endian(buf, prefix_len_bits.len);
	//size_t num_prefix_bytes = prefix_len_bits.num_prefix_bytes();//__num_prefix_bytes(prefix_len_bits.len);
	//if (num_prefix_bytes > prefix.size()) {
//		throw std::runtime_error("invalid num prefix bytes");
//	}

	auto prefix_bytes = prefix.get_bytes(prefix_len_bits);
	buf.insert(buf.end(), prefix_bytes.begin(), prefix_bytes.end());
}

[[maybe_unused]]
static void write_node_header(unsigned char* buf, const unsigned char* prefix, const PrefixLenBits prefix_len, const uint8_t last_byte_mask = 255) {
	PriceUtils::write_unsigned_big_endian(buf, prefix_len.len);
	int num_prefix_bytes = __num_prefix_bytes(prefix_len.len);
	memcpy(buf+2, prefix, num_prefix_bytes);
	buf[num_prefix_bytes + 1] &= last_byte_mask; //TODO not sure what this is for +1 from -1 +2
}


[[maybe_unused]]
static int get_header_bytes(const PrefixLenBits prefix_len) {
	return prefix_len.num_prefix_bytes() + 2;
}

template<typename prefix_t>
static void truncate_prefix(prefix_t& prefix, const PrefixLenBits truncate_len_bits, const PrefixLenBits prefix_len_bits) {

	prefix.truncate(truncate_len_bits);
/*
	if (prefix.size() * 8 < prefix_len_bits.len) {
		throw std::runtime_error("invalid prefix_len_bits");
	}
	if (truncate_len_bits > prefix_len_bits) {
		throw std::runtime_error("truncate can't lengthen");
	}
	auto start_zero_bytes = truncate_len_bits.num_prefix_bytes();// __num_prefix_bytes(truncate_len_bits);
	//auto zero_end_bytes = prefix_len_bits.num_prefix_bytes();// __num_prefix_bytes(prefix_len_bits);
	for (auto i = start_zero_bytes; i < prefix.size(); i++) {
		prefix.at(i) = 0;
	}
	if (truncate_len_bits.len/8 < prefix.size()) {
		prefix.at(truncate_len_bits.len/8) &= (0xFF <<((truncate_len_bits.len % 8)));
	}*/
}

template<typename prefix_t, unsigned int BRANCH_BITS>
static void set_next_branch_bits(prefix_t& prefix, const PrefixLenBits fixed_len_bits, const unsigned char branch_bits) {

	unsigned int byte_index = fixed_len_bits.len / 8;
	uint8_t remaining_bits = fixed_len_bits.len % 8;

	if (byte_index >= prefix.size()) {
		std::printf("invalid set_next_branch_bits access\n");
		throw std::runtime_error("invalid access");
	}

	uint8_t next_byte = prefix.at(byte_index);

	next_byte &= (0xFF << (8-remaining_bits));

	uint8_t branch_bits_offset = 8-remaining_bits-BRANCH_BITS;
	next_byte |= (branch_bits << (branch_bits_offset));

	//std::printf("fixed_len_bits = %d branch_bits = %u\n", fixed_len_bits, branch_bits);

	prefix.at(byte_index) = next_byte;
}
/*
template<typename T>
struct TypeWrapper {
	using type = T;
};

class FrozenValue {
	std::vector<unsigned char> serialization;
public:

	FrozenValue() : serialization() {}

	FrozenValue(const unsigned char * frozen_bytes, unsigned int frozen_len)
	: serialization(frozen_bytes, frozen_bytes + frozen_len) {}
	
	void copy_data(std::vector<uint8_t>& buf) const {
		buf.insert(buf.end(), serialization.begin(), serialization.end());
	}

	void copy_data(unsigned char* buf) const {
		memcpy(buf, serialization.data(), serialization.size());
	}

	uint16_t data_len() const {
		return serialization.size();
	}

	bool modified_since_last_hash() const {
		return false;
	}

	void serialize() const {}

	//void append_to_proof(std::vector<unsigned char>& buf) const {
	//	buf.insert(buf.end(), bytes.begin(), bytes.end());
	//}

};*/

struct EmptyValue {

	constexpr static uint16_t data_len() {
		return 0;
	}

	constexpr static void serialize() {}

	constexpr static void copy_data(std::vector<uint8_t>& buf) {}
	//void copy_data(unsigned char* buf) const {}

	constexpr static bool modified_since_last_hash() {
		return false;
	}
	//void append_to_proof(std::vector<unsigned char>& buf) const {}
};

template<unsigned int LEN>
class SimpleBitVector {

};

/*
template<>
class SimpleBitVector<2> {
	uint8_t bv = 0;
public:
	void add(unsigned char branch_bits) {
		bv |= ((uint8_t)1)<<branch_bits;
	}
	unsigned char pop() {
		unsigned char loc = __builtin_ffs(bv) - 1;
		bv -= ((uint8_t)1)<<loc;
		return loc;
	}
	unsigned int size() const {
		return __builtin_popcount(bv);
	}
	constexpr static size_t needed_bytes() {
		return 1;
	}
	//void write_to(unsigned char* ptr) const {
	//	*ptr = bv;
	//}
	void write(std::vector<unsigned char> vec) {
		vec.push_back(bv);
	}
};*/

template<>
class SimpleBitVector<4> {
	uint16_t bv = 0;
public:

	SimpleBitVector(uint16_t bv) : bv(bv) {}
	SimpleBitVector() : bv(0) {}

	void add(unsigned char branch_bits) {
		bv |= ((uint16_t)1)<<branch_bits;
	}
	unsigned char pop() {
		unsigned char loc = __builtin_ffs(bv) - 1;
		bv -= ((uint16_t)1)<<loc;
		return loc;
	}

	void erase(uint8_t loc) {
		bv &= (~(((uint16_t)1)<<loc));
	}

	unsigned char lowest() const {
		unsigned char loc = __builtin_ffs(bv) - 1;
		return loc;
	}
	unsigned int size() const {
		return __builtin_popcount(bv);
	}
	unsigned int needed_bytes() const {
		return 2;
	}
	void write_to(unsigned char* ptr) {
		PriceUtils::write_unsigned_big_endian(ptr, bv);
	}
	void write(std::vector<unsigned char>& vec) {
		PriceUtils::append_unsigned_big_endian(vec, bv);
		//vec.insert(vec.end(), (unsigned char*) &bv, (unsigned char*)((&bv) + 2));
	}

	bool contains(uint8_t loc) const {
		return ((((uint16_t)1)<<loc) & bv) != 0;
	}

	SimpleBitVector drop_lt(uint8_t bb) const {
		uint16_t out = (UINT16_MAX << bb) & bv;
		return SimpleBitVector{out};
	}

	bool empty() const {
		return bv == 0;
	}

	bool operator==(const SimpleBitVector& other) const {
		return bv == other.bv;
	}

	void clear() {
		bv = 0;
	}

	uint16_t get() const {
		return bv;
	}
};
template<>
class SimpleBitVector<8> {
	//unimplemented lazy
};

template<typename ptr_t>
struct kv_pair_t {
	uint8_t first;
	ptr_t& second;
};

template<typename trie_ptr_t, unsigned int BRANCH_BITS, typename ValueType>
class FixedChildrenMap {
	constexpr static unsigned int NUM_CHILDREN = 1<<BRANCH_BITS;

	//using ptr_map_t = std::array<trie_ptr_t, NUM_CHILDREN>;
	using underlying_ptr_t = trie_ptr_t::pointer;
	union {
		underlying_ptr_t map[NUM_CHILDREN];
		ValueType value_;
	};

	using bv_t = SimpleBitVector<BRANCH_BITS>;

	bv_t bv;
	bool call_value_dtor;

	void clear_open_links() {
		if (!bv.empty()) {
			TRIE_INFO("clearing links from bv %x", bv.get());
			for (auto iter = begin(); iter != end(); iter++) {
				TRIE_INFO("resetting ptr %p", (*iter).second);
				//(*iter).second.reset();
				delete (*iter).second;
			}
		}
	}

	void init_ptr_map() {

		//for (size_t i = 0; i < NUM_CHILDREN; i++) {
		//	map[i].release();
			//map[i] = nullptr;
		//}
	}

	void steal_ptr_map(FixedChildrenMap& other) {
		for (size_t i = 0; i < NUM_CHILDREN; i++) {
			map[i] = other.map[i];//std::move(other.map[i]);
			other.map[i] = nullptr;
		}
	}

public:

	void set_value(ValueType new_value) {
		clear_open_links();
		if (call_value_dtor) {
			value_.~ValueType();
		}
		//value_ = ValueType{};
		value_ = new_value;
		call_value_dtor = true;
		bv = bv_t{0};
	}

	uint16_t get_bv() const {
		return bv.get();
	}

	const ValueType& value() const {
		if (bv.empty()) {
			return value_;
		}
		throw std::runtime_error("can't get value from non leaf");
	}

	ValueType& value() {
		return value_;
	}

	//leaves map as active union member
	void clear() {
		if (bv.empty()) {
			if (call_value_dtor) {
				value_.~ValueType();
				call_value_dtor = false;
			}
//			value_ = ValueType{};
		} else {
			clear_open_links();
			//map = ptr_map_t();//.clear();
		}
		bv.clear();
		//map = ptr_map_t();
		init_ptr_map();
	}
		//map.clear();
		//for (unsigned char i = 0; i < NUM_CHILDREN; i++) {
		//	map[i].reset();
		//}

	FixedChildrenMap(ValueType value) 
		: value_(value), bv{0}, call_value_dtor(true) {}

	FixedChildrenMap() : value_(), bv{0}, call_value_dtor(true) {}	

	FixedChildrenMap(FixedChildrenMap&& other) : value_(), bv{other.bv} {
		TRIE_INFO("children move ctor, bv %x other.bv %x", bv.get(), other.bv.get());
		if (bv.empty()) {
			value_ = other.value_;
			call_value_dtor = true;
		} else {
			TRIE_INFO("other mv ctor for stealing children, bv=%x", bv.get());
			value_.~ValueType();
			init_ptr_map();
			steal_ptr_map(other);
		//	for (size_t i = 0; i < NUM_CHILDREN; i++) {
		//		map[i] = std::move(other.map[i]);
		//	}
	//		map = std::move(other.map);
			other.bv.clear();
			call_value_dtor = false;
		}
//		for (uint8_t i = 0; i < NUM_CHILDREN; i++) {
//			map[i] = std::move(other.map[i]);
//		}
//		value_ = other.value_;
	}

	FixedChildrenMap& operator=(FixedChildrenMap&& other) {
		
		TRIE_INFO("operator= other.bv %x", other.bv.get());

		if (other.bv.empty()) {
			value_ = other.value_;
			call_value_dtor = true;
		} else {
			
			if (call_value_dtor) {
				value_.~ValueType();
			}
			if (bv.empty()) {
				init_ptr_map();
			}
			call_value_dtor = false;
			clear_open_links();
			steal_ptr_map(other);
//			map = std::move(other.map);
		}

		bv = other.bv;
		other.bv.clear();
//		for (uint8_t i = 0; i < NUM_CHILDREN; i++) {
//			map[i] = std::move(other.map[i]);
//		}
		//value_ = other.value_;
		return *this;
	}

	underlying_ptr_t at(uint8_t idx) {
		if (idx >= NUM_CHILDREN) {
			throw std::runtime_error("out of bounds!");
		}
		if (!map[idx]) {
			throw std::runtime_error("attempt to dereference null ptr");
		}
		return map[idx];
	}

	underlying_ptr_t operator[](uint8_t idx) {
		return map[idx];
	}

	~FixedChildrenMap() {
		TRIE_INFO("calling destructor, bv=%x", bv.get());
		if (call_value_dtor) {
			value_.~ValueType();
			call_value_dtor = false;
		}

		clear_open_links();
	}

	template<bool is_const>
	struct iterator_ {
		SimpleBitVector<BRANCH_BITS> bv;

		using ptr_t = typename std::conditional<is_const, const underlying_ptr_t, underlying_ptr_t>::type;

		ptr_t* map;

		kv_pair_t<ptr_t> operator*() {
			uint8_t branch = bv.lowest();
			return {branch, map[branch]};
		}

		template<bool other_const>
		bool operator==(const iterator_<other_const>& other) {
			return bv == other.bv;
		}

		iterator_& operator++(int) {
			bv.pop();
			return *this;
		}
	};

	using iterator = iterator_<false>;
	using const_iterator = iterator_<true>;

	void emplace(uint8_t branch_bits, trie_ptr_t&& ptr) {
		if (bv.contains(branch_bits)) {
			delete map[branch_bits];
//			map[branch_bits].reset();
		}// else {
		//	map[branch_bits].release();
		//}
		map[branch_bits] = ptr.release();//std::move(ptr);
		bv.add(branch_bits);
	}

	trie_ptr_t extract(uint8_t branch_bits) {
		auto* out = map[branch_bits];
		if (!bv.contains(branch_bits)) {
			std::printf("bad extraction of bb %u! bv was %x\n", branch_bits, bv.get());
			throw std::runtime_error("can't extract invalid node!");
		}
		bv.erase(branch_bits);

		std::unique_ptr<typename trie_ptr_t::element_type> out_ptr(out);
		//out_ptr.reset(out);
		return out_ptr;//std::make_unique<typename trie_ptr_t::element_type>(out);
	}

	iterator erase(iterator loc) {

		auto bb = (*loc).first;
		//map[bb].reset();
		if (bv.contains(bb)) {
			delete map[bb];
			bv.erase(bb);
			loc++;
		} else {
			throw std::runtime_error("cannot erase nonexistent iter!");
		}

		return loc;
	}

	void erase(uint8_t loc) {
		if (bv.contains(loc)) {
			delete map[loc];
		} else {
			throw std::runtime_error("cannot erase nonexistent loc!");
		}
		bv.erase(loc);
		//map[loc].reset();
	}

	iterator begin() {
		if (!bv.empty()) {
			return iterator{bv, map};
		}
		return end();
	}

	const_iterator begin() const {
		if (!bv.empty()) {
			return const_iterator{bv, map};
		}
		return cend();
	}

	constexpr static iterator end() {
		return iterator{0, nullptr};
	}

	constexpr static const_iterator cend() {
		return const_iterator{0, nullptr};
	}

	iterator find(uint8_t bb) {
		if (bv.contains(bb)) {
			return iterator{bv.drop_lt(bb), map};
		}
		return end();
	}

	const_iterator find(uint8_t bb) const {
		if (bv.contains(bb)) {
			return const_iterator{bv.drop_lt(bb), map};
		}
		return cend();
	}

	bool empty() const {
		return bv.empty();
	}

	size_t size() const {
		return bv.size();
	}

};


/*
Membership Proof:
[KEY] 

(prefix_len (in units of BRANCH_BITS multiples)) children_bv hash of other children's nodes (in sorted order)

(if VALUE [VALUE LEN] [VALUE])

NONMEMBERSHIP Proof:
KEY (if VALUE [VALUE LEN] [VALUE])

for each node up to where things branch off:
prefix_len (i units of BRANCH_BITS multiples) children_bv hash of other children's nodes (in sorted order)
*/

//metadata type needs to impl these methods
//Must be a commutative group
// Clone is obviously not threadsafe with modification, by itself
struct EmptyMetadata {

	using BaseT = EmptyMetadata;
	using AtomicT = EmptyMetadata;

	template<typename ValueType>
	EmptyMetadata(ValueType ptr) {}

	EmptyMetadata () {} // 0

	EmptyMetadata& operator+=(const EmptyMetadata& other) { return *this; }

	EmptyMetadata& operator-=(const EmptyMetadata& other) { return *this; }

	EmptyMetadata operator-() { return EmptyMetadata(); }

	bool operator==(const EmptyMetadata& other) { return true; };

	//EmptyMetadata duplicate() const {return EmptyMetadata(); }
	EmptyMetadata unsafe_substitute(EmptyMetadata other) { return EmptyMetadata(); }
	void clear() {}

	EmptyMetadata unsafe_load() const { return EmptyMetadata{}; }
	void unsafe_store(const EmptyMetadata& other) {}

	std::string to_string() const {
		return std::string();
	}
};

constexpr static auto load_order = std::memory_order_relaxed;//acquire;
constexpr static auto store_order = std::memory_order_relaxed;//release;
constexpr static auto load_store_order = std::memory_order_relaxed;//acq_rel;

struct AtomicDeletableMixin;

struct DeletableMixin {

	using AtomicT = AtomicDeletableMixin;
	using value_t = int32_t;
	value_t num_deleted_subnodes;

	template<typename ValueType>
	DeletableMixin(const ValueType& ptr) 
		: num_deleted_subnodes(0) {}
	DeletableMixin() : num_deleted_subnodes(0) {}

	DeletableMixin& operator+=(const DeletableMixin& other) {
		num_deleted_subnodes += other.num_deleted_subnodes;
		return *this;
	}

	DeletableMixin& operator-=(const DeletableMixin& other) {
		num_deleted_subnodes -= other.num_deleted_subnodes;
		return *this;
	}

	bool operator==(const DeletableMixin& other) {
		return num_deleted_subnodes == other.num_deleted_subnodes;
	}

	std::string to_string() const {
		std::stringstream s;
		s << "num_deleted_subnodes:"<<num_deleted_subnodes<<" ";
		return s.str();
	}

	template<typename AtomicType>
	void unsafe_load_from(const AtomicType& s) {
		num_deleted_subnodes = s.num_deleted_subnodes.load(load_order);
	}
};

struct AtomicDeletableMixin {

	using BaseT = DeletableMixin;

	std::atomic<BaseT::value_t> num_deleted_subnodes;

	template<typename ValueType>
	AtomicDeletableMixin(const ValueType& val) : num_deleted_subnodes(0) {}

	AtomicDeletableMixin() : num_deleted_subnodes(0) {}

	void operator+= (const DeletableMixin& other) {
		num_deleted_subnodes.fetch_add(other.num_deleted_subnodes, store_order);
	}

	void operator-= (const DeletableMixin& other) {
		num_deleted_subnodes.fetch_sub(other.num_deleted_subnodes, store_order);
	}

	bool operator== (const DeletableMixin& other) {
		return num_deleted_subnodes.load(load_order) == other.num_deleted_subnodes;
	}

	bool compare_exchange(BaseT::value_t expect, BaseT::value_t desired) {
		return num_deleted_subnodes.compare_exchange_strong(expect, desired, load_store_order);
	}

	static bool compare_exchange(
		AtomicDeletableMixin& object, BaseT::value_t& expect, BaseT::value_t& desired) {
		return object.compare_exchange(expect, desired);
	}

	void clear() {
		num_deleted_subnodes = 0;
	}

	void unsafe_store(const DeletableMixin& other) {
		num_deleted_subnodes.store(other.num_deleted_subnodes, store_order);
	}

	std::string to_string() const {
		std::stringstream s;
		s << "num_deleted_subnodes:"<<num_deleted_subnodes<<" ";
		return s.str();
	}
};

struct AtomicRollbackMixin;

struct RollbackMixin {
	using value_t = int32_t;

	using AtomicT = AtomicRollbackMixin;
	value_t num_rollback_subnodes;

	template<typename ValueType>
	RollbackMixin(const ValueType& ptr) 
		: num_rollback_subnodes(0) {}
	RollbackMixin() : num_rollback_subnodes(0) {}

	RollbackMixin& operator+=(const RollbackMixin& other) {
		num_rollback_subnodes += other.num_rollback_subnodes;
		return *this;
	}

	RollbackMixin& operator-=(const RollbackMixin& other) {
		num_rollback_subnodes -= other.num_rollback_subnodes;
		return *this;
	}

	bool operator==(const RollbackMixin& other) {
		return num_rollback_subnodes == other.num_rollback_subnodes;
	}

	std::string to_string() const {
		std::stringstream s;
		s << "num_rollback_subnodes:"<<num_rollback_subnodes<<" ";
		return s.str();
	}

	template<typename AtomicType>
	void unsafe_load_from(const AtomicType& s) {
		num_rollback_subnodes = s.num_rollback_subnodes.load(load_order);
	}
};

/* Caveat:  "Rollback" is not really the right word (except in our particular use case, which is slightly more specific than this metadata's functionality)

If you insert an object as a "rollback" when it's already present in the trie, then rollback, the object will be deleted.

This is ok when all keys are distinct - i.e. when we insert a key to a trie in rollback mode, then merge in to committed_offers, it's ok to just delete.
This would be a problem for memory database commitment tries, if those rely on doing overwrites.

*/

struct AtomicRollbackMixin {

	using BaseT = RollbackMixin;

	std::atomic<BaseT::value_t> num_rollback_subnodes;

	template<typename ValueType>
	AtomicRollbackMixin(const ValueType& val) : num_rollback_subnodes(0) {}

	AtomicRollbackMixin() : num_rollback_subnodes(0) {}

	void operator+= (const BaseT& other) {
		num_rollback_subnodes.fetch_add(other.num_rollback_subnodes, store_order);
	}

	void operator-= (const BaseT& other) {
		num_rollback_subnodes.fetch_sub(other.num_rollback_subnodes, store_order);
	}

	bool operator== (const BaseT& other) {
		return num_rollback_subnodes.load(load_order) == other.num_rollback_subnodes;
	}

	bool compare_exchange(BaseT::value_t expect, BaseT::value_t desired) {
		return num_rollback_subnodes.compare_exchange_strong(expect, desired, load_store_order);
	}

	static bool compare_exchange(
		AtomicRollbackMixin& object, BaseT::value_t& expect, BaseT::value_t& desired) {
		return object.compare_exchange(expect, desired);
	}

	void clear() {
		num_rollback_subnodes = 0;
	}

	void unsafe_store(const RollbackMixin& other) {
		num_rollback_subnodes.store(other.num_rollback_subnodes, store_order);
	}

	std::string to_string() const {
		std::stringstream s;
		s << "num_rollback_subnodes:"<<num_rollback_subnodes<<" ";
		return s.str();
	}
};

struct AtomicSizeMixin;

struct SizeMixin {

	using AtomicT = AtomicSizeMixin;
	int64_t size;

	template<typename ValueType>
	SizeMixin(ValueType v) : size(1) {}

	SizeMixin() : size(0) {}

	SizeMixin& operator+=(const SizeMixin& other) {
		size += other.size;
		return *this;
	}
	SizeMixin& operator-=(const SizeMixin& other) {
		size -= other.size;
		return *this;
	}

	bool operator==(const SizeMixin& other) {
		return size == other.size;
	}

	std::string to_string() const {
		std::stringstream s;
		s << "size:"<<size<<" ";
		return s.str();
	}

	template<typename OtherSizeType>
	void unsafe_load_from(const OtherSizeType& s) {
		size = s.size.load(load_order);
	}
	/*SizeMixin operator-() {
		SizeMixin out;
		out.size = -size;
		return out;
	}*/
};



struct AtomicSizeMixin {
	std::atomic_int64_t size;

	template<typename ValueType>
	AtomicSizeMixin(const ValueType& v) : size(1) {}

	AtomicSizeMixin() : size(0) {}

	void operator+= (const SizeMixin& other) {
		size.fetch_add(other.size, store_order);
	}

	void operator-= (const SizeMixin& other) {
		size.fetch_sub(other.size, store_order);
	}

	bool operator== (const SizeMixin& other) {
		return size.load(load_order) == other.size;
	}
	void clear() {
		size = 0;
	}

	void unsafe_store(const SizeMixin& other) {
		size.store(other.size, store_order);
	}

	std::string to_string() const {
		std::stringstream s;
		s << "size:"<<size<<" ";
		return s.str();
	}

};

template<typename ...MetadataComponents>
struct AtomicCombinedMetadata;

template <typename ...MetadataComponents>
struct CombinedMetadata : public MetadataComponents... {
	using AtomicT = AtomicCombinedMetadata<MetadataComponents...>;

	template<typename ValueType>
	CombinedMetadata(ValueType v) : MetadataComponents(v)... {}

	CombinedMetadata() : MetadataComponents()... {}

	using MetadataComponents::operator+=...;
	using MetadataComponents::operator-=...;
	using MetadataComponents::operator==...;
	using MetadataComponents::unsafe_load_from...;
	//using MetadataComponents::operator-...;

	CombinedMetadata& operator+=(const CombinedMetadata& other) {
		(MetadataComponents::operator+=(other), ...);
		return *this;
	}
	CombinedMetadata& operator-=(const CombinedMetadata& other) {
		(MetadataComponents::operator-=(other),...);
		return *this;
	}
	CombinedMetadata operator-() {
		CombinedMetadata out = CombinedMetadata();
		out -=(*this);
		return out;
	}

	bool operator==(const CombinedMetadata& other) {
		bool result = true;
		((result = result && MetadataComponents::operator==(other)),...);
		return result;
	}

	bool operator!=(const CombinedMetadata& other) {
		return !(*this == other);
	}

	void clear() {
		*this = CombinedMetadata();
		//(MetadataComponents::clear(),...);
	}
	CombinedMetadata clone() const {
		CombinedMetadata output = *this;
		return output;
	}

	CombinedMetadata substitute(const CombinedMetadata& other) {
		CombinedMetadata output = *this;
		*this = other;
		return output;
	}

	std::string to_string() const {
		std::string output("");
		((output += MetadataComponents::to_string()),...);
		return output;
	}

	template<typename AtomicType>
	void unsafe_load_from(const AtomicType& other) {
		(MetadataComponents::unsafe_load_from(other),...);
	}

	void unsafe_store(const CombinedMetadata& other) {
		(MetadataComponents::unsafe_store(other),...);
	}
};

template<typename ...MetadataComponents>
struct AtomicCombinedMetadata : public MetadataComponents::AtomicT... {

	using BaseT = CombinedMetadata<MetadataComponents...>;

	using MetadataComponents::AtomicT::operator+=...;
	using MetadataComponents::AtomicT::operator-=...;
	using MetadataComponents::AtomicT::operator==...;
	using MetadataComponents::AtomicT::clear...;
	using MetadataComponents::AtomicT::unsafe_store...;

	template<typename ValueType>
	AtomicCombinedMetadata(ValueType value)
		: MetadataComponents::AtomicT(value)... {}

	AtomicCombinedMetadata() : MetadataComponents::AtomicT()...{}

	void operator+=(const BaseT& other) {
		(MetadataComponents::AtomicT::operator+=(other),...);
	}
	void operator-=(const BaseT& other) {
		(MetadataComponents::AtomicT::operator-=(other),...);
	}
	bool operator==(const BaseT& other) {
		bool comparison = true;
		((comparison &= MetadataComponents::AtomicT::operator+=(other)),...);
		return comparison;
	}
	void clear() {
		(MetadataComponents::AtomicT::clear(),...);
	}

	//only safe to load when you have an exclusive lock on node.
	//Otherwise, might get shorn reads
	BaseT unsafe_load() const{
		BaseT output;
		output.unsafe_load_from(*this);
		return output;
	}

	void unsafe_store(const BaseT& other) {
		(MetadataComponents::AtomicT::unsafe_store(other),...);
	}

	BaseT unsafe_substitute(const BaseT& new_metadata) {
		auto output = unsafe_load();
		unsafe_store(new_metadata);
		return output;
	}

	std::string to_string() const {
		std::string output("");
		((output += MetadataComponents::AtomicT::to_string()),...);
		return output;
	}

};

/*

template<typename BaseMetadata>
struct AtomicWrapper : public BaseMetadata {

	std::mutex mtx;

	template<typename ValueType>
	AtomicWrapper(ValueType value)
		: BaseMetadata(value),
		mtx() {}

	AtomicWrapper() : BaseMetadata(),
		mtx() {}


	AtomicWrapper& operator+=(const AtomicWrapper& other) {
		std::lock_guard<std::mutex> lock(*mtx);
		this -> BaseMetadata::operator+=(other);
		return *this;
	}

	AtomicWrapper& operator-=(const AtomicWrapper& other) {
		std::lock_guard<std::mutex> lock(*mtx);
		this -> BaseMetadata::operator-=(other);
		return *this;
	}

	AtomicWrapper operator-() {
		std::lock_guard lock(*mtx);
		return this->BaseMetadata::operator-();
	}

	bool operator==(const AtomicWrapper& other) {
		std::lock_guard lock(*mtx);
		std::lock_guard lock2(*other.mtx);
		return this->BaseMetadata::operator==(other);
	}

	bool operator!=(const AtomicWrapper& other) {
		return !(*this == other);
	}

	AtomicWrapper(const BaseMetadata& other) 
	: BaseMetadata(other),
	mtx(std::make_unique<std::mutex>()) {}

	AtomicWrapper& operator=(const BaseMetadata& other) {
		std::lock_guard<std::mutex> lock(*mtx);
		this -> BaseMetadata::operator=(other);
		return *this;
	}

	BaseMetadata substitute(const BaseMetadata& other) {
		std::lock_guard lock(*mtx);
		BaseMetadata output = this -> BaseMetadata::clone();
		this -> BaseMetadata::operator=(other);
		return output;
	}

	BaseMetadata duplicate() const {
		std::lock_guard lock(*mtx);
		return BaseMetadata::clone();
	}
};
*/

//F is function to map prefix to KeyInterpretationType
template<typename MetadataOutputType, typename KeyInterpretationType, typename KeyMakerF>
struct IndexedMetadata {
	KeyInterpretationType key;
	MetadataOutputType metadata;

	IndexedMetadata(KeyInterpretationType key, MetadataOutputType metadata) :
			key(key),
			metadata (metadata) {}
};

struct GenericInsertFn {
	template<typename MetadataType, typename ValueType>
	static MetadataType new_metadata(const ValueType& value) {
		return MetadataType(value);
	}

	template<typename ValueType, typename prefix_t>
	static ValueType new_value(const prefix_t& prefix) {
		return ValueType{};
	}
};

//can call unsafe methods bc exclusive locks on metadata inputs in caller
struct OverwriteMergeFn {
	template<typename ValueType>
	static void value_merge(ValueType& main_value, const ValueType& other_value) {
		main_value = other_value;
	}

	template<typename AtomicMetadataType>
	static typename AtomicMetadataType::BaseT 
	metadata_merge(AtomicMetadataType& main_metadata, const AtomicMetadataType& other_metadata) {

		//return other - main, set main <- delta
		auto other_metadata_loaded = other_metadata.unsafe_load();
		auto original_main_metadata = main_metadata.unsafe_load();
		main_metadata.unsafe_store(other_metadata_loaded);

		other_metadata_loaded -= original_main_metadata;
		return other_metadata_loaded;

		//auto original_main_metadata = main_metadata.substitute(other_metadata);
		//auto metadata_delta = main_metadata.duplicate();
		//metadata_delta -= original_main_metadata;
		//return metadata_delta;
	}
};

//can call unsafe methods bc excl locks on metadata inputs in caller
struct OverwriteInsertFn : public GenericInsertFn {

	template<typename ValueType>
	static void value_insert(ValueType& main_value, const ValueType& other_value) {
		main_value = other_value;
	}

	template<typename AtomicMetadataType, typename ValueType>
	static typename AtomicMetadataType::BaseT 
	metadata_insert(AtomicMetadataType& original_metadata, const ValueType& new_value) {

		//return other - main, set main <- delta
		auto new_metadata = typename AtomicMetadataType::BaseT(new_value);
		auto metadata_delta = new_metadata;
		metadata_delta -= original_metadata.unsafe_substitute(new_metadata);
		return metadata_delta;
	}
};

struct RollbackInsertFn : public OverwriteInsertFn {
	template<typename MetadataType, typename ValueType>
	static MetadataType new_metadata(const ValueType& value) {
		auto out = MetadataType(value);
		out.num_rollback_subnodes = 1;
		return out;
	}


	template<typename AtomicMetadataType, typename ValueType>
	static typename AtomicMetadataType::BaseT 
	metadata_insert(AtomicMetadataType& original_metadata, const ValueType& new_value) {

		//return other - main, set main <- delta
		auto new_metadata = typename AtomicMetadataType::BaseT(new_value);
		new_metadata.num_rollback_subnodes = 1;
		
		auto metadata_delta = new_metadata;
		metadata_delta -= original_metadata.unsafe_substitute(new_metadata);
		return metadata_delta;
	}
};

struct NullOpDelSideEffectFn {
	template<typename ...Args>
	void operator() (const Args&... args) {}
};


template<typename xdr_type>
struct XdrTypeWrapper : public xdr_type {
	
	//mutable std::vector<uint8_t> serialization;
	//mutable std::mutex mtx;

	XdrTypeWrapper() 
		: xdr_type()
		//, serialization() 
		{}
	XdrTypeWrapper(const xdr_type& x) 
		: xdr_type(x)
		//, serialization()
       		{}

	XdrTypeWrapper& operator=(const XdrTypeWrapper& other) {
		//std::lock_guard l1(mtx);
		//std::lock_guard l2(other.mtx);

		xdr_type::operator=(other);
	//	serialization = other.serialization;
		return *this;
	}

	XdrTypeWrapper(const XdrTypeWrapper& other)
	: xdr_type()
	//, serialization()
	//, mtx() 
	{
	//	std::lock_guard l2(other.mtx);
		//don't need to lock self since this is still ctor
		xdr_type::operator=(other);
	//	serialization = other.serialization;
	}

	/*void serialize() const {
	//	std::lock_guard lock(mtx);
		serialization.clear();
		xdr_type to_serialize = *this;
		auto buf = xdr::xdr_to_opaque(to_serialize);

		//const unsigned char* msg = (const unsigned char*) buf->data();
		//auto msg_len = buf.size();

		serialization.insert(serialization.end(), buf.begin(), buf.end());
	}*/

	size_t data_len() const 
	{

		return xdr::xdr_size(static_cast<xdr_type>(*this));
	//	std::lock_guard lock(mtx);
		//return serialization.size();
	}

	void copy_data(std::vector<uint8_t>& buf) const {
	//	std::lock_guard lock(mtx);
		auto serialization = xdr::xdr_to_opaque(static_cast<xdr_type>(*this));
		buf.insert(buf.end(), serialization.begin(), serialization.end());
	}

/*	void copy_data(unsigned char* buf, size_t len) const {
	//	std::lock_guard lock(mtx);
		auto serialization = xdr::xdr_to_opaque(*this);

		if (len != serialization.size()) {
			throw std::runtime_error("mismatch between len and size()");
		}
		memcpy(buf, serialization.data(), len);
	}*/
};

template<typename TrieT>
class HashRange {
	
	uint64_t num_children;
public:
	std::vector<TrieT*> nodes;

	bool empty() const {
		return num_children == 0;
	}

	bool is_divisible() const {
		return num_children > 1000;
	}

	std::size_t num_nodes() const {
		return nodes.size();
	}

	TrieT* operator[](std::size_t idx) const {
		return nodes.at(idx);
	}

	HashRange(std::unique_ptr<TrieT>& node) 
		: num_children(0)
		, nodes() {
			nodes.push_back(node.get());
			num_children = (node->size()) - (node -> num_deleted_subnodes());
		};

	HashRange(HashRange& other, tbb::split) 
		: num_children(0)
		, nodes() {
			try {
				auto original_sz = other.num_children;
				while (num_children < original_sz/2) {
					if (other.nodes.size() == 1) {
						other.nodes = other.nodes.at(0)->children_list();
					}
					if (other.nodes.size() == 0) {
						std::printf("other.nodes.size() = 0!");
						return;
					}

					nodes.push_back(other.nodes.at(0));
					other.nodes.erase(other.nodes.begin());
					auto sz = nodes.back()->size() - nodes.back()->num_deleted_subnodes();
					num_children += sz;
					other.num_children -= sz;
				}
			} catch (...) {
				std::printf("error in HashRange split: other.nodes.size() = %lu, self.nodes.size() = %lu\n", other.nodes.size(), nodes.size());
				throw;
			}
	}
};


//Main difference with hash range is accounting for subnodes marked deleted.
//No nodes in work_list overlap, even after splitting
template<typename TrieT, unsigned int GRAIN_SIZE = 1000>
struct ApplyRange {
	std::vector<TrieT*> work_list;

	uint64_t work_size;

	bool empty() const {
		return work_size == 0;
	}

	bool is_divisible() const {
		return work_size > GRAIN_SIZE;
	}

	ApplyRange(const std::unique_ptr<TrieT>& work_root)
		: work_list()
		, work_size(work_root -> size()) {
			work_list.push_back(work_root.get());
		}

	ApplyRange(ApplyRange& other, tbb::split)
		: work_list()
		, work_size(0) {

			auto original_sz = other.work_size;
			if (original_sz == 0) {
				return;
			}
			//std::printf("starting split of range of size %lu\n", original_sz);
			while (work_size < original_sz/2) {
				if (other.work_list.size() == 0) {
					std::printf("other work list shouldn't be zero!\n");
					throw std::runtime_error("get fucked this won't print");
				}
				if (other.work_list.size() == 1) {

					if (other.work_list.at(0) == nullptr) {
						throw std::runtime_error("found nullptr in ApplyRange!");
					}
					other.work_list = other.work_list.at(0)->children_list();
				} else {

				//if (other.work_list.size() <= 1) {
				//	std::printf("other work list is empty!\n");
				//	throw std::runtime_error("other work_list sz shouldn't be <= 1 here");
				//}

					work_list.push_back(other.work_list.at(0));
					other.work_list.erase(other.work_list.begin());
				
					auto sz = work_list.back()->size();
					work_size += sz;
					other.work_size -= sz;
				}
			}
			//std::printf("done split\n");
	}
};

template<typename TrieT, unsigned int GRAIN_SIZE = 1000>
struct ClearRollbackRange {
	std::vector<TrieT*> work_list;

	uint64_t work_size;

	uint64_t vector_offset;

	bool empty() const {
		return work_size == 0;
	}

	bool is_divisible() const {
		return work_size > GRAIN_SIZE;
	}
	ClearRollbackRange(const std::unique_ptr<TrieT>& work_root)
		: work_list()
		, work_size(work_root -> get_metadata_unsafe().num_rollback_subnodes)
		, vector_offset(0) {
			work_list.push_back(work_root.get());
		}

	ClearRollbackRange(ClearRollbackRange& other, tbb::split)
		: work_list()
		, work_size(0)
		, vector_offset(other.vector_offset) {


			auto original_sz = other.work_size;
			while (work_size < original_sz/2) {
				if (other.work_list.size() == 1) {
					other.work_list.at(0)->clear_local_rollback_metadata();
					other.work_list = other.work_list.at(0)->children_list_ordered();
				}
				if (other.work_list.size() == 1) {
					std::printf("other.work_size = 1?!\n");
					throw std::runtime_error("shouldn't still have other.work_list.size() == 1");
				}

				if (other.work_list.size() == 0) {
					std::printf("other.work_size = 0?!\n");
					throw std::runtime_error("shouldn't get to other.work_list.size() == 0");
				}

				auto sz = other.work_list.at(0)->get_metadata_unsafe().num_rollback_subnodes;

				if (sz > 0) {
					work_list.push_back(other.work_list[0]);

					other.work_list.erase(other.work_list.begin());
					
					work_size += sz;
					other.work_size -= sz;

					other.vector_offset += sz;
				} else {
					other.work_list.erase(other.work_list.begin());
				}
			}
	}
};

template<typename TrieT, unsigned int GRAIN_SIZE = 1000>
struct AccumulateValuesRange {
	std::vector<TrieT*> work_list;

	uint64_t work_size;

	uint64_t vector_offset;

	bool empty() const {
		return work_size == 0;
	}

	bool is_divisible() const {
		return work_size > GRAIN_SIZE;
	}

	AccumulateValuesRange(const std::unique_ptr<TrieT>& work_root)
		: work_list()
		, work_size(work_root -> size())
		, vector_offset(0) {
			work_list.push_back(work_root.get());
		}

	AccumulateValuesRange(AccumulateValuesRange& other, tbb::split)
		: work_list()
		, work_size(0)
		, vector_offset(other.vector_offset) {

			//std::printf("starting split\n");

			auto original_sz = other.work_size;
			while (work_size < original_sz/2) {
				//std::printf("starting loop:%lu of %lu\n", work_size, original_sz);
				if (other.work_list.size() == 1) {
					//std::printf("doing children list\n");
					other.work_list = other.work_list.at(0)->children_list_ordered();
					//std::printf("done split children list\n");
				}
				if (other.work_list.size() == 1) {
					std::printf("other.work_size = 1?!\n");
					throw std::runtime_error("shouldn't still have other.work_list.size() == 1");
				}

				if (other.work_list.size() == 0) {
					std::printf("other.work_size = 0?!\n");
					throw std::runtime_error("shouldn't get to other.work_list.size() == 0");
				}

				work_list.push_back(other.work_list[0]);

				//work_list.push_back(other.work_list[0]);
				other.work_list.erase(other.work_list.begin());
				
				auto sz = work_list.back()->size();
				//std::printf("stole %lu of %lu\n", sz, original_sz);
				work_size += sz;
				other.work_size -= sz;

				other.vector_offset += sz;
			}

			//std::printf("done split\n");

	}
};


} /* edce */
