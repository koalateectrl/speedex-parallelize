#pragma once

#include "merkle_trie_utils.h"

#include "threadlocal_cache.h"


#include <cstdint>
#include <cstddef>
#include <atomic>



namespace edce {

class AccountIDPrefix {
	AccountID prefix;

	static_assert(sizeof(AccountID) == 8, "this won't work with diff size accountid");

	constexpr static uint8_t BRANCH_BITS = 4;
	constexpr static uint8_t BRANCH_MASK = 0x0F;

	constexpr static uint8_t MAX_LEN_BITS = 64;
	constexpr static uint8_t MAX_LEN_BYTES = 8;

public:

	AccountIDPrefix(AccountID id) : prefix(id) {}

	std::strong_ordering operator<=>(const AccountIDPrefix& other) const = default;
	bool operator==(const AccountIDPrefix& other) const = default;

	unsigned char get_branch_bits(const PrefixLenBits branch_point) const {
		if (branch_point.len >= MAX_LEN_BITS) {
			std::printf("Bad branch bits was %u\n", branch_point.len);
			throw std::runtime_error("can't branch beyond end");
		}
		return (prefix >> (60 - branch_point.len)) & BRANCH_MASK;
	}

	PrefixLenBits get_prefix_match_len(
		const PrefixLenBits& self_len, 
		const AccountIDPrefix& other, 
		const PrefixLenBits& other_len) const {

		uint64_t diff = prefix ^ other.prefix;
		PrefixLenBits computed = PrefixLenBits{MAX_LEN_BITS};

		if (diff != 0) {
			size_t matching_bits = __builtin_clzll(diff);
			uint16_t match_rounded = matching_bits - (matching_bits % BRANCH_BITS);
			computed = PrefixLenBits{match_rounded};
		}
		return std::min({computed, self_len, other_len});
	}

	void truncate(const PrefixLenBits truncate_point) {
		prefix &= (UINT64_MAX << (64 - truncate_point.len));
	}

	xdr::opaque_array<MAX_LEN_BYTES> get_bytes_array() const {
		xdr::opaque_array<MAX_LEN_BYTES> out;
		//const unsigned char* ptr = reinterpret_cast<const unsigned char*>(data.data());

		//memcpy(out.data(), ptr, MAX_LEN_BYTES);
		PriceUtils::write_unsigned_big_endian(out, prefix);
		return out;
	}

	std::vector<uint8_t> get_bytes(PrefixLenBits prefix_len_bits) const {
		std::vector<uint8_t> out;
		auto full = get_bytes_array();
		auto num_bytes = prefix_len_bits.num_prefix_bytes();
		out.insert(out.end(), full.begin(), full.begin() + num_bytes);
		return out;
	}

	AccountID get_account() const {
		return prefix;
	}

	std::string to_string(const PrefixLenBits len) const {
		auto bytes = get_bytes_array();
		return DebugUtils::__array_to_str(bytes.data(), len.num_prefix_bytes());
	}

	void set_next_branch_bits(PrefixLenBits fixed_len, const uint8_t bb) {
		uint8_t offset = (60-fixed_len.len);	
		uint64_t mask = ((uint64_t) BRANCH_MASK) << offset;
		uint64_t adjust = ((uint64_t) bb) << offset;
		prefix = (prefix & (~mask)) | adjust;

		//unsigned int byte_index = fixed_len.len / 8;
		//uint8_t byte_offset = fixed_len.len % 8;


	}
};


struct AccountTrieAllocatorConstants {
	constexpr static size_t BUF_SIZE = 500'000;

	constexpr static uint8_t BUFFER_ID_BITS = 8;
	constexpr static uint8_t OFFSET_BITS = 24;

	constexpr static uint32_t OFFSET_MASK = (((uint32_t)1) << (OFFSET_BITS)) - 1;

	static_assert(BUFFER_ID_BITS + OFFSET_BITS == 32, "ptrs are size 32 bits");
};

template<typename ObjType>
struct AccountTrieNodeAllocator;

template<typename ObjType>
class AllocationContext {
	uint32_t cur_buffer_offset_and_index = UINT32_MAX;
	uint32_t value_buffer_offset_and_index = UINT32_MAX;

	AccountTrieNodeAllocator<ObjType>& allocator;

	constexpr static size_t BUF_SIZE = AccountTrieAllocatorConstants::BUF_SIZE;
	constexpr static uint8_t OFFSET_BITS = AccountTrieAllocatorConstants::OFFSET_BITS;
	constexpr static uint32_t OFFSET_MASK = AccountTrieAllocatorConstants::OFFSET_MASK;
	using value_t = typename ObjType::value_t;

public:

	AllocationContext(uint32_t cur_buffer_offset_and_index, uint32_t value_buffer_offset_and_index, AccountTrieNodeAllocator<ObjType>& allocator) 
		: cur_buffer_offset_and_index(cur_buffer_offset_and_index)
		, value_buffer_offset_and_index(value_buffer_offset_and_index)
		, allocator(allocator) {}

	uint32_t allocate(uint8_t num_nodes) {
		if (((cur_buffer_offset_and_index + num_nodes) & OFFSET_MASK) > BUF_SIZE) {
			allocator.assign_new_buffer(*this);
		}

		uint32_t out = cur_buffer_offset_and_index;
		cur_buffer_offset_and_index += num_nodes;
		return out;
	}

	uint32_t allocate_value() {
		if (((value_buffer_offset_and_index + 1) & OFFSET_MASK) > BUF_SIZE) {
			allocator.assign_new_value_buffer(*this);
		}
		uint32_t out = value_buffer_offset_and_index;
		value_buffer_offset_and_index += 1;

	//	auto& out_ref = get_value(out);
	//	out_ref = value_t{};
		return out;
	}

	void set_cur_buffer_offset_and_index(uint32_t value) {
		cur_buffer_offset_and_index = value;
	}

	void set_cur_value_buffer_offset_and_index(uint32_t value) {
		value_buffer_offset_and_index = value;
	}

	uint32_t init_root_node() {
		auto ptr = allocate(1);
		auto& node = get_object(ptr);
		node.set_as_empty_node();
		return ptr;
	}

	ObjType& get_object(uint32_t ptr) const {
		return allocator.get_object(ptr);
	}

	value_t& get_value(uint32_t ptr) const {
		return allocator.get_value(ptr);
	}

	/*void clear() {
		cur_buffer_offset_and_index = UINT32_MAX;
		value_buffer_offset_and_index = UINT32_MAX;
		allocator = nullptr;
	}*/
};

template<typename ObjType>
struct AccountTrieNodeAllocator {
	constexpr static size_t BUF_SIZE = AccountTrieAllocatorConstants::BUF_SIZE;
	constexpr static uint8_t OFFSET_BITS = AccountTrieAllocatorConstants::OFFSET_BITS;
	constexpr static uint32_t OFFSET_MASK = AccountTrieAllocatorConstants::OFFSET_MASK;

	using buffer_t = std::array<ObjType, BUF_SIZE>;

	using value_t = typename ObjType::value_t;

	using value_buffer_t = std::array<value_t, BUF_SIZE>;

private:
	std::atomic<uint16_t> next_available_buffer = 0;
	std::atomic<uint16_t> next_available_value_buffer = 0;

	using buffer_ptr_t = std::unique_ptr<buffer_t>;

	std::array<buffer_ptr_t, 256> buffers;

	using value_buffer_ptr_t = std::unique_ptr<value_buffer_t>;

	std::array<value_buffer_ptr_t, 256> value_buffers;

	using context_t = AllocationContext<ObjType>;

public:

	context_t get_new_allocator() {
		uint16_t idx = next_available_buffer.fetch_add(1, std::memory_order_relaxed);
		if (idx >= buffers.size()) {
			throw std::runtime_error("used up all allocation buffers!!!");
		}

		if (!buffers[idx]) {
			buffers[idx] = std::make_unique<buffer_t>();
		}

		uint16_t value_buffer_idx = next_available_value_buffer.fetch_add(1, std::memory_order_relaxed);
		if (value_buffer_idx >= value_buffers.size()) {
			throw std::runtime_error("used up all value buffers");
		}

		if (!value_buffers[value_buffer_idx]) {
			value_buffers[value_buffer_idx] = std::make_unique<value_buffer_t>();
		}

		return context_t(((uint32_t) idx) << OFFSET_BITS, ((uint32_t) value_buffer_idx) << OFFSET_BITS, *this);
	}

	void assign_new_buffer(context_t& context) {
		uint16_t idx = next_available_buffer.fetch_add(1, std::memory_order_relaxed);
		if (idx >= buffers.size()) {
			throw std::runtime_error("used up all allocation buffers!!!");
		}

		if (!buffers[idx]) {
			buffers[idx] = std::make_unique<buffer_t>();
		}

		context.set_cur_buffer_offset_and_index(((uint32_t) idx) << OFFSET_BITS);
	}

	void assign_new_value_buffer(context_t& context) {
		uint16_t value_buffer_idx = next_available_value_buffer.fetch_add(1, std::memory_order_relaxed);
		if (value_buffer_idx >= value_buffers.size()) {
			throw std::runtime_error("used up all value buffers");
		}

		if (!value_buffers[value_buffer_idx]) {
			value_buffers[value_buffer_idx] = std::make_unique<value_buffer_t>();
		}

		context.set_cur_value_buffer_offset_and_index(((uint32_t) value_buffer_idx) << OFFSET_BITS);
	}


	ObjType& get_object(uint32_t ptr) const {
		uint8_t idx = ptr >> OFFSET_BITS;
		uint32_t offset = ptr & OFFSET_MASK;
	//	std::printf("attempting dereference of %u %u\n", idx, offset);
		return (*buffers[idx])[offset];
	}

	value_t& get_value(uint32_t value_ptr) const {
		uint8_t idx = value_ptr >> OFFSET_BITS;
		uint32_t offset = value_ptr & OFFSET_MASK;
		return (*value_buffers[idx])[offset];
	}

	void reset() {
		next_available_buffer = 0;
		next_available_value_buffer = 0;
	}
};

template<typename ptr_t>
struct bb_ptr_pair_t {
	uint8_t first;
	ptr_t second;
};

//doesn't actually do memory management for children.
template<typename ValueType, typename NodeT>
class AccountChildrenMap {

public:

	constexpr static uint8_t NUM_CHILDREN = 16;
	constexpr static uint8_t BRANCH_BITS = 4;


	using ptr_t = uint32_t;
	using value_ptr_t = uint32_t;
	using bv_t = SimpleBitVector<BRANCH_BITS>;

private:

	using allocator_t = AllocationContext<NodeT>;

	struct ChildrenPtrs {
		ptr_t base_ptr_offset;

		//ptr_t map[NUM_CHILDREN];
		bv_t bv;

		ChildrenPtrs() 
			: base_ptr_offset(UINT32_MAX)
			, bv(0) {}

		void allocate(allocator_t& allocator) {
			base_ptr_offset = allocator.allocate(NUM_CHILDREN);
			bv.clear();
		}

		ChildrenPtrs& operator=(ChildrenPtrs&& other) {
			
			//for (size_t i = 0; i < NUM_CHILDREN; i++) {
			//	map[i] = other.map[i];
			//}
			base_ptr_offset = other.base_ptr_offset;
			other.base_ptr_offset = UINT32_MAX;
			bv = other.bv;
			other.bv.clear();
			return *this;
		}

		void set_child(uint8_t branch_bits, ptr_t ptr, allocator_t& allocator) {
			auto child_ptr = base_ptr_offset + branch_bits;
			auto& child = allocator.get_object(child_ptr);
			child.set_as_empty_node();
			//child.set_this_ptr(child_ptr);
			child.set_to(allocator.get_object(ptr), child_ptr);
			bv.add(branch_bits);
		}

		NodeT& init_new_child(uint8_t branch_bits, allocator_t& allocator) {
			auto child_ptr = base_ptr_offset + branch_bits;
			bv.add(branch_bits);
			auto& ref = allocator.get_object(child_ptr);
			ref.set_as_empty_node();
			//ref.set_this_ptr(child_ptr);
			return ref;
		}

		ptr_t extract(uint8_t branch_bits) {
			if (!bv.contains(branch_bits)) {
				std::printf("bad extraction of bb %u! bv was %x\n", branch_bits, bv.get());
				throw std::runtime_error("can't extract invalid node!");
			}
			bv.erase(branch_bits);
			return base_ptr_offset + branch_bits;
		}

		ptr_t at(uint8_t branch_bits) {
			if (!bv.contains(branch_bits)) {
				std::printf("bad access of bb %u! bv was %4x\n", branch_bits, bv.get());
			}
			return base_ptr_offset + branch_bits;
		}

		void log(std::string padding) {
			LOG("%schildren map: bv 0x%x base_ptr_offset 0x%x", padding.c_str(), bv.get(), base_ptr_offset);
		}
	};

	union {
		value_ptr_t value_;
		ChildrenPtrs children;
		ptr_t moved_to_location;
	};

	enum /*:uint8_t*/ {VALUE, MAP, STOLEN, CLEARED} tag;
//	bool value_active;



	void steal_ptr_map(AccountChildrenMap&& other) {
		children = std::move(other.children);
	}

	void stolen_guard(const char* caller) const {
		if (tag == STOLEN) {
			std::printf("%s\n", caller);
			std::fflush(stdout);
			throw std::runtime_error("can't do ops on stolen nodes!");
		}
	}

	void steal_value(value_ptr_t steal_ptr) {
		stolen_guard("steal_value");
		if (tag != VALUE) {
			tag = VALUE;
		}
		value_ = steal_ptr;
	}

	value_ptr_t get_value_ptr() {
		if (tag != VALUE) {
			return UINT32_MAX;
		}
		return value_;
	}

public:
	void print_offsets() {
		using this_t = AccountChildrenMap<ValueType, NodeT>;
		std::printf("children: start %lu end %lu\n", offsetof(this_t, children), offsetof(this_t, children) + sizeof(children));
		std::printf("tag: start %lu end %lu\n", offsetof(this_t, tag), offsetof(this_t, tag) + sizeof(tag));
	}

	void set_value(allocator_t& allocator, const ValueType& value_input) {
		stolen_guard("set_value");
		if (tag != VALUE) {
			value_ = allocator.allocate_value();
			auto& ref = allocator.get_value(value_);
			ref = value_input; // TODO std::move?
			tag = VALUE;
		}
	}

	/*
	void set_value(ValueType new_value = ValueType{}) {
		stolen_guard("set_value");
		if (tag != VALUE) {
			value_ = new ValueType();
			//new (&value_) ValueType;
			tag = VALUE;
		}
		*value_ = new_value;
	}*/

	void set_map_noalloc() {
		stolen_guard("set_map_noalloc");
		//if (tag == VALUE) {
		//	delete value_;
		//}
		if (tag != MAP) {
			tag = MAP;
			new (&children) ChildrenPtrs();
		}
	}

	void log(std::string padding) {
		if (tag == VALUE) {
			//do nothing for now
		} else if (tag == MAP) {
			children.log(padding);
		} else if (tag == STOLEN) {
			LOG("%sSTOLEN to %x", padding.c_str(), moved_to_location);
		} else {
			//tag == CLEARED
			LOG("%sCLEARED NODE!!!", padding.c_str());
		}
	}

	void reset_map(allocator_t& allocator) {
		set_map(allocator);
	}

	void set_map(allocator_t& allocator) {
		set_map_noalloc();
		children.allocate(allocator);
	}

	void set_stolen(ptr_t new_address) {
		//if (tag == VALUE) {
		//	delete value_;
		//}
		tag = STOLEN;
		moved_to_location = new_address;
	}

	std::optional<ptr_t> check_stolen() {
		if (tag == STOLEN) {
			return moved_to_location;
		}
		return std::nullopt;
	}


	//AccountChildrenMap(ValueType value)
	//	: value_(new ValueType(value)), tag{VALUE} {}

	AccountChildrenMap() : value_(UINT32_MAX), tag{CLEARED} {}

	AccountChildrenMap(AccountChildrenMap&& other) : value_(UINT32_MAX), tag{CLEARED} {
		other.stolen_guard("move_ctor");
		if (other.tag == VALUE) {
			steal_value(other.get_value_ptr());
			//set_value(other.value());
			tag = VALUE;
		} else if (other.tag == MAP) {
			set_map_noalloc();
			steal_ptr_map(std::move(other));
			tag = MAP;
		} else {
			tag = CLEARED;
		}
		other.tag = CLEARED;
	}

	AccountChildrenMap& operator=(AccountChildrenMap&& other) {
		other.stolen_guard("operator=");
		if (other.tag == VALUE) {
			steal_value(other.get_value_ptr());
			//set_value(other.value());
		} else if (other.tag == MAP) {
			set_map_noalloc();
			steal_ptr_map(std::move(other));
		} else {
			tag = CLEARED;
		}
		other.tag = CLEARED;
		return *this;
	}

	~AccountChildrenMap() {
		//if (tag == VALUE) {
			//value_.~ValueType();
		//	delete value_;
		//}
	}

	ptr_t operator[](uint8_t idx) {
		if (tag == MAP) {
			return children.at(idx);//.base_ptr_offset + idx;
		}
		return UINT32_MAX;
	}

	uint16_t get_bv() const {
		if (tag == MAP) {
			return children.bv.get();
		}
		return 0;
	}

	template<typename allocator_or_context_t>
	const ValueType& value(const allocator_or_context_t& allocator) const {
		if (tag == VALUE) {
			return allocator.get_value(value_);
			//return *value_;
		}
		throw std::runtime_error("can't get value from non leaf (const)");
	}

	template<typename allocator_or_context_t>
	ValueType& value(const allocator_or_context_t& allocator) {
		if (tag == VALUE) {
			return allocator.get_value(value_);
		}
		throw std::runtime_error("can't get value from non leaf");
	}

	//leaves nothing set as active union member
	void clear() {
//
		if (tag == VALUE) {
			//delete value_;
		} else if (tag == MAP) {
			children.bv.clear();
		}
		tag = CLEARED;
	}

	template<bool is_const>
	struct iterator_ {
		SimpleBitVector<BRANCH_BITS> bv;

		//ptr_t* map;

		ptr_t base_map_offset;

		bb_ptr_pair_t<ptr_t> operator*() {
			uint8_t branch = bv.lowest();
			return {branch, base_map_offset + branch};
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

	void emplace(uint8_t branch_bits, ptr_t ptr, allocator_t& allocator) {
		//if (children.bv.contains(branch_bits)) {
		//	delete map[branch_bits];
		//}
		children.set_child(branch_bits, ptr, allocator);
//		children[branch_bits] = ptr;
//		children.bv.add(branch_bits);
	}

	NodeT& init_new_child(uint8_t branch_bits, allocator_t& allocator) {
		map_guard();
		return children.init_new_child(branch_bits, allocator);
	}

	void map_guard() {
		if (tag != MAP) {
			throw std::runtime_error("accessed MAP method when MAP not set!");
		}
	}

	ptr_t extract(uint8_t branch_bits) {
		map_guard();
		return children.extract(branch_bits);
		/*auto out = children.map[branch_bits];
		if (!bv.contains(branch_bits)) {
			std::printf("bad extraction of bb %u! bv was %x\n", branch_bits, bv.get());
			throw std::runtime_error("can't extract invalid node!");
		}
		bv.erase(branch_bits);

		//std::unique_ptr<typename trie_ptr_t::element_type> out_ptr(out);
		return out;*/
	}

	iterator erase(iterator loc) {
		map_guard();
		auto bb = (*loc).first;
		children.extract(bb); // throws error on nonexistence
		loc++;
		return loc;

		/*if (children.bv.contains(bb)) {
			// delete map[bb];
			children.bv.erase(bb);
			loc++;
		} else {
			throw std::runtime_error("cannot erase nonexistent iter!");
		}

		return loc;*/
	}

	void erase(uint8_t loc) {
		map_guard();
		children.extract(loc);
/*		if (bv.contains(loc)) {
			//delete map[loc];
		} else {
			throw std::runtime_error("cannot erase nonexistent loc!");
		}
		children.bv.erase(loc);
		//map[loc].reset();*/
	}

	iterator begin() {
		if (tag != MAP) {
			return end();
		}
		if (!children.bv.empty()) {
			return iterator{children.bv, children.base_ptr_offset};
		}
		return end();
	}

	const_iterator begin() const {
		if (tag != MAP) {
			return cend();
		}
		if (!children.bv.empty()) {
			return const_iterator{children.bv, children.base_ptr_offset};
		}
		return cend();
	}

	constexpr static iterator end() {
		return iterator{0, UINT32_MAX};
	}

	constexpr static const_iterator cend() {
		return const_iterator{0, UINT32_MAX};
	}

	iterator find(uint8_t bb) {
		if (tag != MAP) {
			return end();
		}
		if (children.bv.contains(bb)) {
			return iterator{children.bv.drop_lt(bb), children.base_ptr_offset};
		}
		return end();
	}

	const_iterator find(uint8_t bb) const {
		if (tag != MAP) {
			return cend();
		}
		if (children.bv.contains(bb)) {
			return const_iterator{children.bv.drop_lt(bb), children.base_ptr_offset};
		}
		return cend();
	}

	bool empty() const {
		stolen_guard("empty");
		if (tag != MAP) {
			return true;
		}
		return children.bv.empty();
	}

	size_t size() const {
		stolen_guard("size");
		if (tag != MAP) {
			return 0;
		}
		return children.bv.size();
	}
};


template<typename TrieT>
class AccountHashRange {
	
	uint32_t num_children;
	using allocator_t = AccountTrieNodeAllocator<TrieT>;
	using ptr_t = TrieT::ptr_t;

	allocator_t& allocator;

public:
	std::vector<ptr_t> nodes;

	bool empty() const {
		return num_children == 0;
	}

	bool is_divisible() const {
		return num_children > 1000;
	}

	size_t num_nodes() const {
		return nodes.size();
	}

	TrieT& operator[](size_t idx) const {
		return allocator.get_object(nodes[idx]);
	}

	AccountHashRange(ptr_t node, allocator_t& allocator) 
		: num_children(0)
		, allocator(allocator)
		, nodes() {
			nodes.push_back(node);
			num_children = (allocator.get_object(node).size());
		};

	AccountHashRange(AccountHashRange& other, tbb::split) 
		: num_children(0)
		, allocator(other.allocator)
		, nodes() {
			auto original_sz = other.num_children;
			while (num_children < original_sz/2) {
				if (other.nodes.size() == 1) {
					other.nodes = allocator.get_object(other.nodes[0]).children_list();
				}
				if (other.nodes.size() == 0) {
					std::printf("other.nodes.size() = 0!");
					return;
				}

				nodes.push_back(other.nodes[0]);
				other.nodes.erase(other.nodes.begin());
				auto sz = allocator.get_object(nodes.back()).size();
				num_children += sz;
				other.num_children -= sz;
			}
	}
};


template<typename TrieT, unsigned int GRAIN_SIZE = 1000>
struct AccountAccumulateValuesRange {
	using ptr_t =  TrieT::ptr_t;
	using allocator_t =  TrieT::allocator_t;
	std::vector<ptr_t> work_list;

	uint64_t work_size;

	uint64_t vector_offset;

	const allocator_t& allocator;

	bool empty() const {
		return work_size == 0;
	}

	bool is_divisible() const {
		return work_size > GRAIN_SIZE;
	}

	AccountAccumulateValuesRange(ptr_t work_root, const allocator_t& allocator)
		: work_list()
		, work_size(allocator.get_object(work_root).size())
		, vector_offset(0)
		, allocator(allocator) {
			work_list.push_back(work_root);
		}

	AccountAccumulateValuesRange(AccountAccumulateValuesRange& other, tbb::split)
		: work_list()
		, work_size(0)
		, vector_offset(other.vector_offset)
		, allocator(other.allocator) {

			auto original_sz = other.work_size;
			while (work_size < original_sz/2) {
				if (other.work_list.size() == 1) {
					other.work_list = allocator.get_object(other.work_list.at(0)).children_list();
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
				
				auto sz = allocator.get_object(work_list.back()).size();
				//std::printf("stole %lu of %lu\n", sz, original_sz);
				work_size += sz;
				other.work_size -= sz;

				other.vector_offset += sz;
			}
	}
};

//Main difference with hash range is accounting for subnodes marked deleted.
//No nodes in work_list overlap, even after splitting
template<typename TrieT, unsigned int GRAIN_SIZE = 1000>
struct AccountApplyRange {
	using ptr_t = TrieT::ptr_t;
	using allocator_t =  TrieT::allocator_t;

	std::vector<ptr_t> work_list;

	uint64_t work_size;

	const allocator_t& allocator;

	bool empty() const {
		return work_size == 0;
	}

	bool is_divisible() const {
		return work_size > GRAIN_SIZE;
	}

	AccountApplyRange(ptr_t work_root, const allocator_t& allocator)
		: work_list()
		, work_size(0)//work_root -> size())
		, allocator(allocator) {
			work_size = allocator.get_object(work_root).size();
			work_list.push_back(work_root);
		}

	AccountApplyRange(AccountApplyRange& other, tbb::split)
		: work_list()
		, work_size(0) 
		, allocator(other.allocator) {

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

					if (other.work_list.at(0) == UINT32_MAX) {
						throw std::runtime_error("found nullptr in ApplyRange!");
					}
					other.work_list = allocator.get_object(other.work_list.at(0)).children_list_nolock();
				} else {
					work_list.push_back(other.work_list.at(0));
					other.work_list.erase(other.work_list.begin());
				
					auto sz = allocator.get_object(work_list.back()).size();
					work_size += sz;
					other.work_size -= sz;
				}
			}
	}
};





} /* edce */
