#pragma once
#include <cstdint>

#include "merkle_trie_utils.h"
#include "account_merkle_trie_utils.h"
#include "threadlocal_cache.h"

#include "xdr/types.h"
#include "xdr/trie_proof.h"

#include <sodium.h>

#include <vector>
#include <array>
#include <atomic>
#include <optional>
#include <cstddef>

namespace edce {

template<typename ValueType, typename NodeType>
struct TrieNodeContents {
	using prefix_t = AccountIDPrefix;
	using children_map_t = AccountChildrenMap<ValueType, NodeType>;
	using ptr_t = children_map_t::ptr_t;

	children_map_t children;
	prefix_t prefix;
	PrefixLenBits prefix_len;
	int32_t size;
};

struct PtrWrapper {
	uint32_t ptr;
	constexpr static uint32_t NULLPTR = UINT32_MAX;
	bool non_null() const {
		return ptr != NULLPTR;
	}
	uint32_t get() const {
		return ptr;
	}

	constexpr static PtrWrapper make_nullptr() {
		return PtrWrapper{NULLPTR};
	}
};

template<typename ValueType>
struct ApplyableSubnodeRef {
	uint32_t ptr;
	const AccountTrieNodeAllocator<ValueType>& allocator;

	template<typename ApplyFn>
	void apply(ApplyFn& fn) const {
		allocator.get_object(ptr).apply(fn, allocator);
	}

	std::array<uint8_t, 8> get_prefix_bytes() const {
		return allocator.get_object(ptr).get_prefix().get_bytes_array();
	}

	PrefixLenBits get_prefix_len() const {
		return allocator.get_object(ptr).get_prefix_len();
	}
};

template<typename ValueType>
class AccountTrieNode {

public:
	using prefix_t = AccountIDPrefix;
	using children_map_t = AccountChildrenMap<ValueType, AccountTrieNode<ValueType>>;
	using ptr_t = children_map_t::ptr_t;
	using hash_t = Hash;
	using node_t = AccountTrieNode<ValueType>;
	using allocation_context_t = AllocationContext<node_t>;
	using contents_t = TrieNodeContents<ValueType, node_t>;
	using allocator_t = AccountTrieNodeAllocator<node_t>;
	using value_t = ValueType;
	constexpr static uint8_t BRANCH_BITS = 4;
private:

	friend children_map_t;

	constexpr static uint8_t KEY_LEN_BYTES = 8;
	static_assert(sizeof(AccountID) == 8, "invalid accountid len (bunch of stuff to fix if change)");

	constexpr static uint8_t MAX_BRANCH_VALUE = 0xF;


	constexpr static PrefixLenBits MAX_KEY_LEN_BITS = PrefixLenBits{KEY_LEN_BYTES * 8};

	// should all fit in 2 cache lines

	children_map_t children; // 9 bytes  old:should be 64 + 3 bytes

	std::atomic<bool> hash_valid = false; // 1
	SpinMutex mtx; // 1
	
	PrefixLenBits prefix_len; // 2, could be made 1

	std::atomic<int32_t> size_ = 0; // 4

	//ptr_t this_ptr = UINT32_MAX; // 4
	


	prefix_t prefix; // 8

	hash_t hash; // 32

	void set_to(AccountTrieNode& other, ptr_t this_ptr) {
		auto lock = other.lock();
		children = std::move(other.children);
		prefix = other.prefix;
		prefix_len = other.prefix_len;
		hash_valid.store(other.hash_valid.load(std::memory_order_relaxed), std::memory_order_relaxed);
		size_.store(other.size_.load(std::memory_order_relaxed), std::memory_order_relaxed);

		if (other.get_hash_valid()) {
			hash = other.hash;
			validate_hash();
		} else {
			invalidate_hash();
		}
		other.children.set_stolen(this_ptr);
	}

	std::optional<ptr_t> check_stolen() {
		return children.check_stolen();
	}



	contents_t extract_contents() {
		return contents_t {
			.children = std::move(children),
			.prefix = prefix,
			.prefix_len = prefix_len,
			.size = size()
		};
	}

	void set_from_contents(contents_t& contents) {
		children = std::move(contents.children);
		prefix = contents.prefix;
		prefix_len = contents.prefix_len;
		set_size(contents.size);
		invalidate_hash();
	}



public:

	void print_offsets() {
		using node_t = AccountTrieNode<ValueType>;
		std::printf("children: start %lu end %lu\n", offsetof(node_t, children), offsetof(node_t, children) + sizeof(children));
		std::printf("hash_valid: start %lu end %lu\n", offsetof(node_t, hash_valid), offsetof(node_t, hash_valid) + sizeof(hash_valid));
		std::printf("prefix_len: start %lu end %lu\n", offsetof(node_t, prefix_len), offsetof(node_t, prefix_len) + sizeof(prefix_len));
		std::printf("size_: start %lu end %lu\n", offsetof(node_t, size_), offsetof(node_t, size_) + sizeof(size_));
		//std::printf("this_ptr: start %lu end %lu\n", offsetof(node_t, this_ptr), offsetof(node_t, this_ptr) + sizeof(this_ptr));
		std::printf("mtx: start %lu end %lu\n", offsetof(node_t, mtx), offsetof(node_t, mtx) + sizeof(mtx));
		std::printf("prefix: start %lu end %lu\n", offsetof(node_t, prefix), offsetof(node_t, prefix) + sizeof(prefix));
		std::printf("hash: start %lu end %lu\n", offsetof(node_t, hash), offsetof(node_t, hash) + sizeof(hash));
		children.print_offsets();

	}

	template<typename InsertFn, typename InsertedValueType>
	void set_as_value_leaf(
		const prefix_t key, 
		typename std::enable_if<std::is_same<ValueType, InsertedValueType>::value, const InsertedValueType&>::type value,
		allocation_context_t& allocator) {

		children.set_value(allocator, value);
		prefix = key;
		prefix_len = MAX_KEY_LEN_BITS;
		set_size(1);
		invalidate_hash();
	}

	template<typename InsertFn, typename InsertedValueType>
	void set_as_value_leaf(
		const prefix_t key, 
		typename std::enable_if<!std::is_same<ValueType, InsertedValueType>::value, const InsertedValueType&>::type value,
		allocation_context_t& allocator) {
		ValueType value_out = InsertFn::template new_value<ValueType>(key);
		InsertFn::value_insert(value_out, value);

		children.set_value(allocator, value_out);
		prefix = key;
		prefix_len = MAX_KEY_LEN_BITS;
		set_size(1);
		invalidate_hash();
	}

	//for creating empty node
	AccountTrieNode() 
		: children()
		, prefix_len(0)
		, prefix(0)
		{
			set_size(0);
		}

	void set_as_empty_node() {
		children.clear();
		prefix_len = PrefixLenBits{0};
		prefix = prefix_t{0};
		//children.set_value();
		set_size(0);
		invalidate_hash();
		//set_this_ptr(this_ptr);
	}

	void steal_other_node_contents(
		children_map_t&& old_children,
		const prefix_t old_prefix,
		const PrefixLenBits old_prefix_len,
		int32_t old_size) {

		children = std::move(old_children);
		prefix = old_prefix;
		prefix_len = old_prefix_len;
		set_size(old_size);
		invalidate_hash();
	}

	//void set_this_ptr(ptr_t this_ptr_set) {
	//	this_ptr = this_ptr_set;
	//}

	int32_t size() const;

	void set_size(int32_t sz) {
		size_.store(sz, std::memory_order_relaxed);
	}
	void alter_size(int32_t delta) {
		size_.fetch_add(delta, std::memory_order_relaxed);
	}

	const PrefixLenBits get_prefix_len() const {
		return prefix_len;
	}

	prefix_t get_prefix() const {
		return prefix;
	}

	PrefixLenBits get_prefix_match_len(const prefix_t other_key, const PrefixLenBits other_len = MAX_KEY_LEN_BITS) const {
		return prefix.get_prefix_match_len(prefix_len, other_key, other_len);
	}

	uint8_t get_branch_bits(const prefix_t other_key) const {
		return other_key.get_branch_bits(prefix_len);
	}

	void invalidate_hash() {
		hash_valid.store(false, std::memory_order_relaxed);
	}

	void validate_hash() {
		hash_valid.store(true, std::memory_order_relaxed);
	}

	bool get_hash_valid() const {
		return hash_valid.load(std::memory_order_relaxed);
	}

	//obj not threadsafe with mods to hash
	void append_hash_to_vec(std::vector<unsigned char>& buf) const {
		buf.insert(buf.end(), hash.begin(), hash.end());
	}

	//not threadsafe
	template<typename InsertFn, typename InsertedValueType>
	int32_t insert(prefix_t key, const InsertedValueType& leaf_value, allocation_context_t& allocator);

	//not threadsafe
	template<typename... ApplyToValueBeforeHashFn>
	void compute_hash(const allocator_t& allocator, std::vector<unsigned char>& digest_bytes);

	//threadsafe
	template<typename MergeFn>
	int32_t merge_in(ptr_t node, allocation_context_t& allocator);

	SpinLockGuard lock() const {
		return SpinLockGuard(mtx);
	}

	bool is_leaf() const {
		return prefix_len == MAX_KEY_LEN_BITS;
	}

	template<typename allocator_or_context_t>
	void log(std::string padding, allocator_or_context_t& allocator) {
		LOG("%sprefix %s (len %u bits)", 
			padding.c_str(),
			prefix.to_string(prefix_len).c_str(),
			prefix_len.len);
		LOG("%ssz: %ld", padding.c_str(), size());
		children.log(padding);
		//LOG("%sthis_ptr: %x", padding.c_str(), this_ptr);
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			LOG("%schild: child_bb %x, ptr 0x%x", padding.c_str(), (*iter).first, (*iter).second);
			allocator.get_object((*iter).second).log(padding + " |   ", allocator);
		}
	}

	void copy_hash_to_buf(uint8_t* buf) {
		memcpy(buf, hash.data(), hash.size());
	}

	//threadsafe
	std::vector<ptr_t> children_list() const {
		auto lock_ = lock();
		std::vector<ptr_t> output;
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			output.push_back((*iter).second);
		}
		return output;
	}

	std::vector<ptr_t> children_list_nolock() const {
		std::vector<ptr_t> output;
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			output.push_back((*iter).second);
		}
		return output;
	}

	//not threadsafe
	std::vector<std::pair<uint8_t, ptr_t>> children_list_with_branch_bits_nolock() {
		
		std::vector<std::pair<uint8_t, ptr_t>> output;
		//std::printf("listing children of node %p with prefix_len %d, num_children %lu\n", this, prefix_len, children.size());
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			output.emplace_back((*iter).first, (*iter).second);
		}
		return output;
	}


	template<typename VectorType>
	void accumulate_values_parallel_worker(VectorType& output, size_t vector_offset, const allocator_t& allocator) const;

	std::tuple<bool, int32_t, PtrWrapper>
	destructive_steal_child(const prefix_t stealing_prefix, const PrefixLenBits stealing_prefix_len, allocation_context_t& allocator);

	void propagate_sz_delta(AccountTrieNode<ValueType>* target, int32_t delta, const allocator_t& allocator) {

		invalidate_hash();
		if (target == this) {
			return;
		}
		
		auto lk = lock();

		auto branch_bits = get_branch_bits(target -> get_prefix());

		auto iter = children.find(branch_bits);
		if (iter == children.end()) {
			throw std::runtime_error("can't propagate metadata to nonexistent node");
		}

		alter_size(delta);


		//update_metadata(branch_bits, metadata);
		allocator.get_object((*iter).second).propagate_sz_delta(target, delta, allocator);
	}

	template <typename ApplyFn>
	void apply(ApplyFn& fn, const allocator_t& allocator) const {
		if (prefix_len == MAX_KEY_LEN_BITS) {
			fn(children.value(allocator));
			return;
		}
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			auto& child = allocator.get_object((*iter).second);
			child.apply(fn, allocator);
		}
	}

	void sz_check(const allocator_t& allocator) {
		if (prefix_len == MAX_KEY_LEN_BITS) {
			if (size() != 1) {
				log("bad node", allocator);
				throw std::runtime_error("value didn't have sz 1!");
			}
			return;
		}

		int32_t children_sz = 0;
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			children_sz += allocator.get_object((*iter).second).size();
			allocator.get_object((*iter).second).sz_check(allocator);
		}
		if (size() != children_sz) {
			log("bad node", allocator);
			std::fflush(stdout);
			throw std::runtime_error("children sz mismatch");
		}
	}

};



template<typename ValueType>
class AccountTrie;


template<typename ValueType>
class SerialAccountTrie {
	using node_t = AccountTrieNode<ValueType>;
	AllocationContext<node_t> allocation_context;
	using ptr_t = node_t::ptr_t;
	ptr_t root;

	friend class AccountTrie<ValueType>;

public:

	SerialAccountTrie(AccountTrieNodeAllocator<node_t>& allocator) 
		: allocation_context(allocator.get_new_allocator())
		, root(UINT32_MAX) {
			acquire_new_root();
	}

	SerialAccountTrie(AccountTrie<ValueType>& main_trie) 
		: SerialAccountTrie(main_trie.get_allocator()) {}

	void clear() {
		root = UINT32_MAX;
		allocation_context.clear();
	}

	size_t size() const {
		return allocation_context.get_object(root).size();
	}

	void acquire_new_root() {
		root = allocation_context.init_root_node();
	}

	void set_root(ptr_t new_root) {
		root = new_root;
	}

	ptr_t extract_root() {
		auto out = root;
		acquire_new_root();
		return out;
	}

	template<typename InsertFn = OverwriteInsertFn, typename InsertedValueType = ValueType>
	void insert(AccountID account, const InsertedValueType& value) {
		auto& ref = allocation_context.get_object(root);
		ref.template insert<InsertFn, InsertedValueType>(AccountIDPrefix{account}, value, allocation_context);
	}

	void log() {
		auto& ref = allocation_context.get_object(root);
		ref.log("", allocation_context);
	}

	void print_offsets() {
		auto& ref = allocation_context.get_object(root);
		ref.print_offsets();
		std::printf("sizeof: %lu\n", sizeof(node_t));
	}

	AllocationContext<node_t>& get_allocation_context() {
		return allocation_context;
	}
};

template<typename MergeFn>
struct AccountBatchMergeReduction;
template<typename TrieT>
struct AccountBatchMergeRange;

template<typename ValueType>
class AccountTrie {


public:
	using node_t = AccountTrieNode<ValueType>;
	using ptr_t = node_t::ptr_t;
	using serial_trie_t = SerialAccountTrie<ValueType>;
private:

	AccountTrieNodeAllocator<node_t> allocator;

	friend class SerialAccountTrie<ValueType>;

	AccountTrieNodeAllocator<node_t>& get_allocator() {
		return allocator;
	}

	ptr_t root = UINT32_MAX; // null

	mutable std::mutex mtx;

	Hash root_hash;
	std::atomic<bool> hash_valid = false;

	void get_root_hash(Hash& out);
	void validate_hash() {
		hash_valid.store(true, std::memory_order_relaxed);
	}
	void invalidate_hash() {
		hash_valid.store(false, std::memory_order_relaxed);
	}

	bool check_hash_valid() {
		return hash_valid.load(std::memory_order_relaxed);
	}

	template<typename MergeFn = OverwriteMergeFn>
	void merge_in_nolock(serial_trie_t& trie) {

		invalidate_hash();

		if (root == UINT32_MAX) {
			root = trie.root;
			trie.acquire_new_root();
			return;
		}

		auto& obj = allocator.get_object(root);
		obj.template merge_in<MergeFn>(trie.root, trie.allocation_context);
		trie.acquire_new_root();
	}

public:
	AccountTrie() {
		if (sodium_init() == -1) {
			throw std::runtime_error("Sodium init failed!!!");
		}
	}


	serial_trie_t open_serial_subsidiary() {
		return serial_trie_t(allocator);
	}

	void clear() {
		std::lock_guard lock(mtx);
		allocator.reset();
		root = UINT32_MAX;
		hash_valid = false;
	}

	size_t size() const {
		if (root == UINT32_MAX) {
			return 0;
		}
		return allocator.get_object(root).size();
	}



	template<typename VectorType>
	VectorType accumulate_values_parallel() const;

	template<typename VectorType>
	void accumulate_values_parallel(VectorType& vec) const;

	template<typename MergeFn = OverwriteMergeFn>
	void merge_in(serial_trie_t& trie) {
		std::lock_guard lock(mtx);

		merge_in_nolock<MergeFn>(trie);
	}

	template<typename MergeFn>
	void batch_merge_in(ThreadlocalCache<serial_trie_t>& tl_cache) {

		std::lock_guard lock(mtx);
		invalidate_hash();

		auto& serial_tries = tl_cache.get_objects();
		std::vector<ptr_t> ptrs;
		for (auto& serial : serial_tries) {
			if (serial) {
				if (size() > 0) {
					ptrs.push_back(serial->extract_root());
					//std::printf("alloc %lu ptr=%x\n", ptrs.size(), ptrs.back());
	//				allocator.get_object(ptrs.back()).log("blah ", allocator);
				} else {
					merge_in_nolock<MergeFn>(*serial);
				}
			}
		}

		if (ptrs.size() == 0) {
			return;
		}

		AccountBatchMergeRange<node_t> range(root, ptrs, allocator, tl_cache);

		AccountBatchMergeReduction<MergeFn> reduction{};

		tbb::parallel_reduce(range, reduction);
	}

	template<typename ValueModifyFn>
	void parallel_batch_value_modify(ValueModifyFn& fn) const {

		std::lock_guard lock(mtx);
		if (root == UINT32_MAX) {
			std::printf("trie is null, nothing to parallel_batch_value_modify\n");
			return;
		}

		AccountApplyRange<node_t> range(root, allocator);
		//guaranteed that range.work_list contains no overlaps

		//Fences are present because ValueModifyFn can modify various
		//memory locations in MemoryDatabase
		//std::atomic_thread_fence(std::memory_order_release);
		tbb::parallel_for(
			range,
			[&fn, this] (const auto& range) {
				for (unsigned int i = 0; i < range.work_list.size(); i++) {
					ApplyableSubnodeRef ref{range.work_list[i], allocator};
					fn(ref);
				}
			});
	}

	template<typename... ApplyFn>
	void hash(Hash& hash);

	void log() {
		auto& ref = allocator.get_object(root);
		ref.log("", allocator);
	}

	void sz_check() {
		if (root == UINT32_MAX) {
			return;
		}
		allocator.get_object(root).sz_check(allocator);
	}

};

template<typename ValueType>
int32_t AccountTrieNode<ValueType>::size() const {
	return size_.load(std::memory_order_relaxed);
}

template<typename ValueType>
template<typename InsertFn, typename InsertedValueType>
int32_t 
AccountTrieNode<ValueType>::insert(prefix_t key, const InsertedValueType& leaf_value, allocation_context_t& allocator) {

	invalidate_hash();

	auto prefix_match_len = get_prefix_match_len(key);

	//log("start init: ", allocator);

	if (children.size() == 1) {
		log("bad node", allocator);
		std::fflush(stdout);
		throw std::runtime_error("children size should never be 1 (account insert)");
	}

	if (children.empty() && !(prefix_len == MAX_KEY_LEN_BITS || prefix_len.len == 0)) {
		std::printf("what the actual fuck, prefix_len=%d, num children = %lu, max len = %u\n", prefix_len.len, children.size(), MAX_KEY_LEN_BITS.len);
		throw std::runtime_error("invalid initialization somewhere (account insert)");
	}

	if (prefix_len.len == 0 && children.empty()) {
		// dealing with initial node case

		//std::printf("setting initial value leaf\n");
		set_as_value_leaf<InsertFn, InsertedValueType>(key, leaf_value, allocator);
		//log("post init:", allocator);

		return 1;
	}

	if (prefix_match_len == MAX_KEY_LEN_BITS) {
		//std::printf("overwrite value");
		InsertFn::value_insert(children.value(allocator), leaf_value);
		return 0;
	}

	if (prefix_match_len == prefix_len) {
		auto branch_bits = get_branch_bits(key);

		auto iter = children.find(branch_bits);

		if (iter != children.end()) {
			auto& child = allocator.get_object((*iter).second);
			auto sz_delta = child.template insert<InsertFn, InsertedValueType>(key, leaf_value, allocator);
			alter_size(sz_delta);
			return sz_delta;
		} else {

			//auto [new_child, new_child_ptr] = allocator.allocate_pair();
			//auto new_child_ptr = allocator.allocate();
			//auto& new_child = allocator.get_object(new_child_ptr);

			auto& new_child = children.init_new_child(branch_bits, allocator);
			
			new_child.template set_as_value_leaf<InsertFn, InsertedValueType>(key, leaf_value, allocator);
			alter_size(1);
			//children.emplace(branch_bits, new_child_ptr);
			return 1;
		}
	}

	children_map_t original_children = std::move(children);
	children.reset_map(allocator);

	auto original_prefix_len = prefix_len;

	prefix_len = prefix_match_len;
	auto new_child_branch = get_branch_bits(key);
	auto old_child_branch = get_branch_bits(prefix);

	//auto original_child_ptr = allocator.allocate();
	//auto& original_child = allocator.get_object(original_child_ptr);

	auto& original_child = children.init_new_child(old_child_branch, allocator);

	original_child.steal_other_node_contents(std::move(original_children), prefix, original_prefix_len, size());

	//auto new_child_ptr = allocator.allocate();
	auto& new_child = children.init_new_child(new_child_branch, allocator);//d allocator.get_object(new_child_ptr);

	new_child.template set_as_value_leaf<InsertFn, InsertedValueType>(key, leaf_value, allocator);



//	children.emplace(new_child_branch, new_child_ptr);
//	children.emplace(old_child_branch, original_child_ptr);

	prefix.truncate(prefix_len);

	alter_size(1);
	return 1;
}

constexpr static bool USE_BLAKE = true;
constexpr static bool USE_DIGEST_BUFFER = true;

template <typename ValueType, typename prefix_t>
static void 
compute_hash_value_node_v2(
	Hash& hash_buf, 
	const prefix_t prefix,
	const PrefixLenBits prefix_len, 
	ValueType& value, 
	std::vector<unsigned char>& digest_bytes) {

	//std::vector<unsigned char> digest_bytes;
	if (!USE_DIGEST_BUFFER) {
		std::vector<unsigned char>().swap(digest_bytes); // clear mem
	} else {
		digest_bytes.clear();
	}

	write_node_header(digest_bytes, prefix, prefix_len);
	value.copy_data(digest_bytes);

	if (USE_BLAKE) {
		if (crypto_generichash(hash_buf.data(), hash_buf.size(), digest_bytes.data(), digest_bytes.size(), NULL, 0) != 0) {
			throw std::runtime_error("error from crypto_generichash");
		}
	} else {
		SHA256(digest_bytes.data(), digest_bytes.size(), hash_buf.data());
	}

}

template<typename Map, typename prefix_t, uint8_t BRANCH_BITS, typename allocator_t, typename... ApplyToValueBeforeHashFn>
static void
compute_hash_branch_node_v2(
	Hash& hash_buf, 
	const prefix_t prefix, 
	const PrefixLenBits prefix_len, 
	const Map& children, 
	const allocator_t& allocator,
	std::vector<unsigned char>& digest_bytes) {
	
	//int num_header_bytes = get_header_bytes(prefix_len);

	SimpleBitVector<BRANCH_BITS> bv;
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		bv.add((*iter).first);
		if (!(*iter).second) {
			throw std::runtime_error("can't recurse hash down null ptr");
		}
		allocator.get_object((*iter).second).template compute_hash<ApplyToValueBeforeHashFn...>(allocator, digest_bytes);

	}
	//uint8_t num_children = children.size();

	if (!USE_DIGEST_BUFFER) {
		std::vector<unsigned char>().swap(digest_bytes); // clear mem
	} else {
		digest_bytes.clear();
	}

	//std::vector<unsigned char> digest_bytes_local;

	write_node_header(digest_bytes, prefix, prefix_len);

	bv.write(digest_bytes);

	//iter goes in increasing order
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		allocator.get_object((*iter).second).append_hash_to_vec(digest_bytes);
	}

	if (USE_BLAKE) {
		if (crypto_generichash(hash_buf.data(), hash_buf.size(), digest_bytes.data(), digest_bytes.size(), NULL, 0) != 0) {
			throw std::runtime_error("error from crypto_generichash");
		}
	} else {
		SHA256(digest_bytes.data(), digest_bytes.size(), hash_buf.data());
	}
}

template<typename ValueType>
template<typename... ApplyToValueBeforeHashFn>
void 
AccountTrieNode<ValueType>::compute_hash(const allocator_t& allocator, std::vector<unsigned char>& digest_bytes) {
	auto hash_is_valid = get_hash_valid();

	if (hash_is_valid) return;

	if (children.empty()) {
		auto& value = children.value(allocator);
		(ApplyToValueBeforeHashFn::apply_to_value(value),...);
		compute_hash_value_node_v2(hash, prefix, prefix_len, value, digest_bytes);
	} else {
		compute_hash_branch_node_v2<children_map_t, prefix_t, BRANCH_BITS, allocator_t, ApplyToValueBeforeHashFn...>(
			hash, prefix, prefix_len, children, allocator, digest_bytes); 
	}

	validate_hash();
}

template<typename ValueType>
template<typename MergeFn>
int32_t 
AccountTrieNode<ValueType>::merge_in(ptr_t node, allocation_context_t& allocator) {

	auto lock_guard = lock();
	invalidate_hash();

	if (allocator.get_object(node).check_stolen()) {
		std::printf("bad node: %x\n", node);
		std::fflush(stdout);
		throw std::runtime_error("thing being merged in can't have been stolen!");
	}

	auto stolen_check = children.check_stolen();
	if (stolen_check) {
		throw std::runtime_error("shoudln't have stolen check");
		auto& real_addr = allocator.get_object(*stolen_check);
		return real_addr.template merge_in<MergeFn>(node, allocator);
	}
	
	auto& other = allocator.get_object(node);

	auto prefix_match_len = get_prefix_match_len(other.prefix, other.prefix_len);

/*	std::printf("merge current %s (%lu) input %s (%lu) (match: %lu)\n", 
		prefix.to_string(prefix_len).c_str(),
		prefix_len.len,
		other.prefix.to_string(other.prefix_len).c_str(),
		other.prefix_len.len,
		prefix_match_len.len);
*/
	// case 0
	if (prefix_match_len == MAX_KEY_LEN_BITS) {
//		std::printf("case 0\n");
		//log("preval", allocator);
		MergeFn::value_merge(children.value(allocator), other.children.value(allocator));
		return 0;
	}

	//case 1
	if (prefix_len == other.prefix_len && prefix_len == prefix_match_len) {
		int32_t sz_delta = 0;
//		std::printf("case 1\n");
		for (auto other_iter = other.children.begin(); other_iter != other.children.end(); other_iter++) {
			auto main_iter = children.find((*other_iter).first);

			auto other_ptr = other.children.extract((*other_iter).first);
			auto& other_ref = allocator.get_object(other_ptr);

			if (main_iter == children.end()) {

				sz_delta += other_ref.size();
				children.emplace((*other_iter).first, other_ptr, allocator);
			} else {

				auto& main_child = allocator.get_object((*main_iter).second);
				sz_delta += main_child.template merge_in<MergeFn>(other_ptr, allocator);

			}
		}
		alter_size(sz_delta);
		return sz_delta;
	}

	// case 2

	if (prefix_len == prefix_match_len /* thus other.prefix_len > prefix_match_len */ ) {
		auto bb = get_branch_bits(other.prefix);
//		std::printf("case 2\n");
		auto iter = children.find(bb);
		if (iter == children.end()) {
			auto sz_delta = other.size();
			children.emplace(bb, node, allocator);
			alter_size(sz_delta);
			return sz_delta;
		}

		auto child_ptr = (*iter).second;
		auto& child = allocator.get_object(child_ptr);

		auto sz_delta = child.template merge_in<MergeFn>(node, allocator);
		alter_size(sz_delta);
		return sz_delta;
	}

	// case 3

	if (other.prefix_len == prefix_match_len /* thus prefix_len > prefix_match_len */ ) {
//		std::printf("case 3\n");
		auto original_sz = size();
		auto other_sz = other.size();

		auto original_contents = extract_contents();
		//auto original_children = std::move(children);
		//auto original_prefix_len = prefix_len;
		//auto original_prefix = prefix;



		children = std::move(other.children);
		other.children.clear();

		prefix_len = other.prefix_len;
		auto original_child_branch_bits = get_branch_bits(prefix);
		prefix = other.prefix;
		set_size(other_sz);


		auto iter = children.find(original_child_branch_bits);

		if (iter == children.end()) {
			// case 3 no recursion
			auto& new_child = children.init_new_child(original_child_branch_bits, allocator);

			new_child.set_from_contents(original_contents);// steal_other_node_contents(std::move(original_children), original_prefix, original_prefix_len, original_sz);
			set_size(other_sz + original_sz);
			//alter_size(original_sz);
			return other_sz;
		} else {
			// case 3 recursion

			auto matching_subtree_of_other_ptr = children[original_child_branch_bits];
			auto& matching_subtree_of_other = allocator.get_object(matching_subtree_of_other_ptr);

			auto matching_subtree_sz = matching_subtree_of_other.size();

			auto matching_subtree_contents = matching_subtree_of_other.extract_contents();
			matching_subtree_of_other.set_from_contents(original_contents);

			auto temp_ptr = allocator.init_root_node();
			auto& temp_obj = allocator.get_object(temp_ptr);
			temp_obj.set_from_contents(matching_subtree_contents);

			matching_subtree_of_other.template merge_in<MergeFn>(temp_ptr, allocator);

			auto new_matching_subtree_sz = matching_subtree_of_other.size();
			auto sz_delta = new_matching_subtree_sz - matching_subtree_sz;
			alter_size(sz_delta);
			return (size() - original_sz);
		}
	} /* end case 3 */

	// case 4
//	std::printf("case 4\n");
	/* other.prefix_len > prefix_match_len and prefix_len > prefix_match_len */

	auto original_contents = extract_contents();
	children.reset_map(allocator);

	prefix_len = prefix_match_len;

	auto new_child_branch = get_branch_bits(other.prefix);
	auto old_child_branch = get_branch_bits(prefix);

	//auto original_child_ptr = allocator.allocate();
	//auto& original_child = allocator.get_object(original_child_ptr);

	auto& original_child = children.init_new_child(old_child_branch, allocator);

	original_child.set_from_contents(original_contents);



	children.emplace(new_child_branch, node, allocator);

	auto& new_child = allocator.get_object(children[new_child_branch]);
	auto sz_delta = new_child.size();
	alter_size(sz_delta);
	prefix.truncate(prefix_len);
	return sz_delta;
}







//AccountTrie

template<typename ValueType>
template<typename... ApplyFn>
void 
AccountTrie<ValueType>::hash(Hash& output_hash) {
	std::lock_guard lock(mtx);

	constexpr static size_t default_digest_buffer_sz = 16 * 32 + 32; // actually its 16 * 32 + 14, but this rounds up just a bit in case its necessary


	if (check_hash_valid()) {
		output_hash = root_hash;
		return;
	}
	//Somehow this performs really badly
	ThreadlocalCache<std::vector<unsigned char>> digest_bytes_cache;

	tbb::parallel_for(
		AccountHashRange<node_t>(root, allocator),
		[this , &digest_bytes_cache  ] (const auto& r) {
			std::vector<unsigned char> digest_buffer;
			//auto& digest_buffer = digest_bytes_cache.get();
			digest_buffer.reserve(default_digest_buffer_sz);
			for (size_t i = 0; i < r.num_nodes(); i++) {
				r[i].template compute_hash<ApplyFn...>(allocator, digest_buffer);
			}
		});
	
	std::vector<unsigned char> digest_buffer;
	digest_buffer.reserve(default_digest_buffer_sz);

	allocator.get_object(root).template compute_hash<ApplyFn...>(allocator, digest_buffer);

	get_root_hash(root_hash);

	output_hash = root_hash;

	validate_hash();

}

template<typename ValueType>
void 
AccountTrie<ValueType>::get_root_hash(Hash& out) {

	if (root == UINT32_MAX) {
		throw std::runtime_error("can't hash null root!");
	}

	auto& root_obj = allocator.get_object(root);
	uint32_t sz = root_obj.size();

	constexpr size_t buf_sz = 4 + 32;
	std::array<unsigned char, buf_sz> buf;
	buf.fill(0);

	PriceUtils::write_unsigned_big_endian(buf, sz);

	if (sz > 0) {
		root_obj.copy_hash_to_buf(buf.data() + 4);
	}

	SHA256(buf.data(), buf.size(), root_hash.data());
	validate_hash();

	out = root_hash;
}
template<typename ValueType>
template<typename VectorType>
VectorType
AccountTrie<ValueType>::accumulate_values_parallel() const {
	VectorType output;
	accumulate_values_parallel(output);
	return output;
}

template<typename ValueType>
template<typename VectorType>
void  
AccountTrie<ValueType>::accumulate_values_parallel(VectorType& output) const {

	std::lock_guard lock(mtx);

	if (size() == 0) return;

	AccountAccumulateValuesRange<node_t> range(root, allocator);

	//VectorType output;
	output.resize(size());

	tbb::parallel_for(
		range,
		[&output, this] (const auto& range) {
			auto vector_offset = range.vector_offset;
			for (size_t i = 0; i < range.work_list.size(); i++) {
				auto& work_node = allocator.get_object(range.work_list[i]);
				work_node.accumulate_values_parallel_worker(output, vector_offset, allocator);
				vector_offset += work_node.size();
			}
		});
}

template<typename ValueType>
template<typename VectorType>
void 
AccountTrieNode<ValueType>::accumulate_values_parallel_worker(VectorType& output, size_t vector_offset, const allocator_t& allocator) const {
	if (prefix_len == MAX_KEY_LEN_BITS) {
		output[vector_offset] = children.value(allocator);
		return;
	}

	for (auto iter = children.begin(); iter != children.end(); iter++) {
		auto& ref = allocator.get_object((*iter).second);
		ref.accumulate_values_parallel_worker(output, vector_offset, allocator);
		vector_offset += ref.size();
	}
}

template<typename ValueType>
std::tuple<bool, int32_t, PtrWrapper>
AccountTrieNode<ValueType>::destructive_steal_child(const prefix_t stealing_prefix, const PrefixLenBits stealing_prefix_len, allocation_context_t& allocator) {
	auto lk = lock();

	auto prefix_match_len = get_prefix_match_len(stealing_prefix, stealing_prefix_len);
	if (prefix_match_len == stealing_prefix_len) {
		/*std::printf("can steal whole node: prefix %s (%lu) stealing_prefix %s (%lu)\n",
				prefix.to_string(prefix_len).c_str(),
				prefix_len.len,
				stealing_prefix.to_string(stealing_prefix_len).c_str(),
				stealing_prefix_len.len);
		*/

		return std::tuple(true, size(), PtrWrapper::make_nullptr());
	}

	if (prefix_match_len == prefix_len) {
		auto bb = get_branch_bits(stealing_prefix);

		auto iter = children.find(bb);
		if (iter == children.end()) {
			return {false, 0, PtrWrapper::make_nullptr()};
		}

		auto [do_steal_entire_subtree, sz_delta, ptr] = allocator.get_object((*iter).second).destructive_steal_child(stealing_prefix, stealing_prefix_len, allocator);

		if (do_steal_entire_subtree) {
			alter_size(-sz_delta);

			auto new_ptr = allocator.allocate(1);
			auto& new_node = allocator.get_object(new_ptr);
			new_node.set_as_empty_node();
			auto old_ptr = children.extract(bb); // == (*iter).second
			auto& old_ptr_ref = allocator.get_object(old_ptr);
			auto old_contents = old_ptr_ref.extract_contents();
			new_node.set_from_contents(old_contents);

			return {false, sz_delta, PtrWrapper{new_ptr}};
		} else {
			if (ptr.non_null()) {
				alter_size(-sz_delta);
				return {false, sz_delta, ptr};
			} else {
				return {false, 0, PtrWrapper::make_nullptr()};
			}
		}
	}

	return {false, 0, PtrWrapper::make_nullptr()};
}


template<typename TrieT>
struct AccountBatchMergeRange {
	using ptr_t = TrieT::ptr_t;
	using map_value_t = std::vector<ptr_t>;
	using allocator_t = AccountTrieNodeAllocator<TrieT>;
	using context_cache_t = ThreadlocalCache<SerialAccountTrie<typename TrieT::value_t>>;
	using prefix_t = TrieT::prefix_t;
	
	allocator_t& allocator;
	context_cache_t& cache;

	//maps from nodes of main trie to lists of new subtries that will get merged in at that node.
	//the new subtries are owned by this object.
	std::unordered_map<ptr_t, map_value_t> entry_points; // maps to unpropagated metadata
	//Whichever thread executes the contents of a map_value_t own the memory.
	//Tbb needs a copy constructor for unclear reasons, but seems to only execute a range once.

	//brances of the main trie that some other node has taken responsibility for.
	std::unordered_set<ptr_t> banned_branches;

	const ptr_t root;

	uint64_t num_children;


	bool empty() const {
		return entry_points.size() == 0;
	}

	bool is_divisible() const {
		return (num_children >= 100) && (entry_points.size() != 0);
	}

	std::shared_ptr<std::mutex> mtx;

	AccountBatchMergeRange (ptr_t root, std::vector<ptr_t> merge_in_list, allocator_t& allocator, context_cache_t& cache)
		: allocator(allocator)
		, cache(cache)
		, entry_points()
		, banned_branches()
		, root(root)
		, num_children(allocator.get_object(root).size())
		, mtx(std::make_shared<std::mutex>())
		{

			map_value_t value;
			for (auto& ptr : merge_in_list) {
				value.push_back(ptr);
			}

			entry_points.emplace(root, value);
		}

	AccountBatchMergeRange(AccountBatchMergeRange& other, tbb::split)
		: allocator(other.allocator)
		, cache(other.cache)
		, entry_points()
		, banned_branches(other.banned_branches)
		, root (other.root)
		, num_children(0)
		, mtx(other.mtx)
		 {
			//constexpr bool print_debugs = false;

			if (other.entry_points.size() == 0) {
				std::printf("other entry pts is nonzero!\n");
				throw std::runtime_error("what the fuck");
			}

			auto original_sz = other.num_children;
			//std::printf("doing a split, original_sz = %lu\n", original_sz);

			while (num_children < original_sz /2) {
				if (other.entry_points.size() > 1) {
					auto iter = other.entry_points.begin();
					auto& entry_pt = allocator.get_object((*iter).first);
					num_children += entry_pt.size();
					other.num_children -= entry_pt.size();

					entry_points.emplace((*iter).first, (*iter). second);
					other.entry_points.erase(iter);
				} else {

				//	std::printf("unique node has size %lu\n", other.entry_points.begin()->first ->size());
					break;
				}
			}

			//std::printf("Num children acquired: %lu entry_points.size(): %lu other.entry_points.size(): %lu\n", num_children, entry_points.size(), other.entry_points.size());

			//other.entry_points.size() == 1

			if (num_children < original_sz /2) {

				if (other.entry_points.size() == 0) {
					std::printf("invalid other.entry_points!\n");
					throw std::runtime_error("invalid other.entry_points!");
				}
				ptr_t theft_root = other.entry_points.begin()->first;

				std::vector<std::pair<uint8_t, ptr_t>> stealable_subnodes;

				/*if (print_debugs) {
					std::printf("stealing from entry point %p : size %lu  prefix %s (len %u)\n", 
						theft_root, 
						theft_root->size(), 
						theft_root->get_prefix().to_string(theft_root->get_prefix_len()).c_str(),
						//DebugUtils::__array_to_str(theft_root -> get_prefix().data(), theft_root-> get_prefix_len().num_prefix_bytes()).c_str(),
						theft_root -> get_prefix_len().len);
				}*/
				//other entry_points is a single TrieT*.
				//We will iterate through the children of this node, stealing until we size is high enough.
				//A stolen child of theft_root (theft_candidate) becomes an entry point for us, and a banned subnode for them.
				// The TrieT*s in the map corresponding to theft_candidate are the children of the merge_in tries that correspond to theft_candidate.
				//Anything that matches theft_root + candidate branch bits can get merged in to theft_candidate.
				// I read through the TBB source, at least the points where deleting the copy ctor causes problems, and this seemed to be not an issue.
				// In fact, in two of the three spots, there's "TODO: std::move?"

				map_value_t merge_in_tries = other.entry_points.begin() -> second;
				//std::printf("tries at entry point in other: %lu\n", other.entry_points.begin()->second.size());


	//			prefix_t steal_prefix{0};

				while (num_children + 10 < other.num_children) { // + 10 for rounding error
					

					if (stealable_subnodes.size() > 1) {
						auto& theft_root_ref = allocator.get_object(theft_root);
						auto lk = theft_root_ref.lock();
						//auto lock = theft_root->get_lock_ref().template lock<exclusive_lock_t>();

						auto [stolen_bb, theft_candidate] = stealable_subnodes.back();
						stealable_subnodes.pop_back();
						//do steal
						other.banned_branches.insert(theft_candidate);
						std::vector<ptr_t> stolen_merge_in_tries;
						prefix_t steal_prefix = theft_root_ref.get_prefix();
						//memcpy(steal_prefix, theft_root -> get_prefix(), TrieT::KEY_LEN_BYTES_EXPORT);

						for (auto iter = merge_in_tries.begin(); iter != merge_in_tries.end(); iter++) {

							if (theft_root_ref.is_leaf()) {
							//	std::printf("hit leaf\n");
								break;
							}
							//std::printf("starting set_next_branch_bits\n");
							//memcpy(steal_prefix, theft_root -> get_prefix(), TrieT::KEY_LEN_BYTES_EXPORT);
							//set_next_branch_bits<prefix_t, TrieT::BRANCH_BITS>(steal_prefix, theft_root_ref.get_prefix_len(), stolen_bb);
							steal_prefix.set_next_branch_bits(theft_root_ref.get_prefix_len(), stolen_bb);
							/*if (print_debugs) {
								std::printf("trial key %s (len %u) (bb = %u)\n",
									
									steal_prefix.to_string(theft_root -> get_prefix_len() + TrieT::BRANCH_BITS_EXPORT).c_str(),
									//DebugUtils::__array_to_str(steal_prefix.data(), (theft_root -> get_prefix_len() + TrieT::BRANCH_BITS_EXPORT).num_prefix_bytes()).c_str(), 
									(theft_root -> get_prefix_len() + TrieT::BRANCH_BITS_EXPORT).len,
									stolen_bb);
							} */
								
							auto [do_steal_entire_subtree, metadata_delta, theft_result] 
								= (allocator.get_object(*iter).destructive_steal_child(
										steal_prefix, 
										theft_root_ref.get_prefix_len() + TrieT::BRANCH_BITS, 
										cache.get(allocator).get_allocation_context()));
							if (theft_result.non_null()) {
								
								/*if (print_debugs) {
									std::printf("for entry pt  %p (%s, %d), stole %p (%s, %d)\n", theft_candidate,
										theft_candidate->get_prefix().to_string(theft_candidate->get_prefix_len()).c_str(),
										//DebugUtils::__array_to_str(theft_candidate -> get_prefix().data(), 
										//	theft_candidate->get_prefix_len().num_prefix_bytes()).c_str(), 
										theft_candidate -> get_prefix_len().len,
										theft_result.get(),
										theft_result->get_prefix().to_string(theft_result->get_prefix_len()).c_str(),
										//DebugUtils::__array_to_str(theft_result -> get_prefix().data(), 
										//	theft_result->get_prefix_len().num_prefix_bytes()).c_str(), 
										theft_result -> get_prefix_len().len);
								}*/
								stolen_merge_in_tries.emplace_back(theft_result.get());
							} else if (do_steal_entire_subtree) {
								//std::printf("asked to steal entire subtree!\n");
								//throw std::runtime_error("should not happen if stealing from theft_root");

							}
						}
						auto& theft_candidate_ref = allocator.get_object(theft_candidate);
						if (stolen_merge_in_tries.size() != 0) {
							//std::printf("stole %lu tries\n", stolen_merge_in_tries.size());
							entry_points.emplace(theft_candidate, std::move(stolen_merge_in_tries));
							num_children += theft_candidate_ref.size();
						} else {
							//std::printf("nothing stolen #sad so continuing, losing %lu size\n", theft_candidate -> size());
						}



						other.num_children -= theft_candidate_ref.size();
						//std::printf("my size:%lu, my entry_points size() = %lu, other remaining size: %lu\n", num_children, entry_points.size(), other.num_children);
					} else {
						if (stealable_subnodes.size() != 0) {
							theft_root = stealable_subnodes.back().second;
							stealable_subnodes.pop_back();
						}

						auto& theft_root_ref = allocator.get_object(theft_root);


						auto lk = theft_root_ref.lock();


						//std::printf("listing stealable children of node %p (len %lu, size %lu)\n", theft_root, theft_root -> get_prefix_len(), theft_root -> size());
						stealable_subnodes = theft_root_ref.children_list_with_branch_bits_nolock();
						//std::printf("stealable size pre filter: %lu\n", stealable_subnodes.size());
						if (theft_root_ref.is_leaf()) {
						//	std::printf("CAN'T STEAL FROM LEAF\n");
							return;
							//throw std::runtime_error("can't steal from leaf!!!");
						}
						//filter stealable
						for (std::size_t i = 0; i < stealable_subnodes.size();) {
							auto iter = other.banned_branches.find(stealable_subnodes[i].second);
							if (iter != other.banned_branches.end()) {
								stealable_subnodes.erase(stealable_subnodes.begin() + i);
							} else {
								i++;
							}
						}
						//std::printf("stealable size post filter: %lu\n", stealable_subnodes.size());

						if (stealable_subnodes.size() == 0) {
							std::printf("NO STEALABLE NODES\n");
							throw std::runtime_error("we could just return, but throwing error for now to be safe");
						}

						//theft_root = stealable_subnodes.back().second;
						//stealable_subnodes.pop_back();
					}
				}
			}
			/*std::printf("stolen entry points: %lu\n", entry_points.size());
			for (auto iter = entry_points.begin(); iter != entry_points.end(); iter++) {
				std::printf("stole %p with %lu waiting tries\n", iter -> first, iter -> second.size());
			}

			std::printf("other entry points: %lu\n", other.entry_points.size());
			for (auto iter = other.entry_points.begin(); iter != other.entry_points.end(); iter++) {
				std::printf("remaining %p with %lu waiting tries\n", iter -> first, iter -> second.size());
			}*/

			//std::printf("done split, stolen size = %lu\n", num_children);
		}

		template<typename MergeFn>
		void execute() {
			//std::lock_guard lock(*mtx);
			//std::lock_guard lock(range_mtx);

			//std::lock_guard<std::mutex> lock_local(*lock);
		//	if (already_executed) {
			//	return;
			//}
			//already_executed = true;
			//std::printf("starting execution\n");
			for (auto iter = entry_points.begin(); iter != entry_points.end(); iter++) {
				int32_t sz_delta = 0;
				//std::printf("executing merge in to entry point %p (prefix: %s len %d)\n",
				//	 iter->first, DebugUtils::__array_to_str(iter->first-> get_prefix(), __num_prefix_bytes(iter->first -> get_prefix_len())).c_str(), iter->first -> get_prefix_len());
				ptr_t entry_pt = iter -> first;
				for (ptr_t node : iter -> second) {

					//auto& node_ref = allocator.get_object(node);
					//auto& entry_pt_ref = allocator.get_object(entry_pt);
			/*		std::printf("input prefix: %s %lu node_ref %s %lu\n",
						entry_pt_ref.get_prefix().to_string(entry_pt_ref.get_prefix_len()).c_str(),
						entry_pt_ref.get_prefix_len().len,
						node_ref.get_prefix().to_string(node_ref.get_prefix_len()).c_str(),
						node_ref.get_prefix_len().len);
			*/		
					
					//entry_pt_ref.log("entry_pt", allocator);
					//node_ref.log("node_ref", allocator);

					//This is not a race condition because:
					// if this range is being executed, it's not getting split.
					// And a range can't get executed while being split.
					// The only concern would be if tbb copies a range, then splits the copy.  This seems to not happen?  And wouldn't really make sense?
					// But it's not technically disallowed by tbb's documentation?  Unclear.  TBB's documentation is not clear.
					//TODO see if we can figure out TBB's actual contract.
					//auto lock = node -> get_lock_ref().template lock<TrieNode::exclusive_lock_t>();
					//std::unique_ptr<TrieT> ptr = std::unique_ptr<TrieT>(node);
					if (allocator.get_object(node).size() > 0) {
						
						sz_delta += allocator.get_object(entry_pt).template merge_in<MergeFn>(node, cache.get(allocator).get_allocation_context());
					}
				//	std::printf("merge in node is %p (prefix: %s len %d)\n",
				//		node, DebugUtils::__array_to_str(node-> get_prefix(), __num_prefix_bytes(node -> get_prefix_len())).c_str(), node -> get_prefix_len());

				}

				auto* entry_pt_realptr = &(allocator.get_object(entry_pt));
				
				//std::printf("search for %x\n", entry_pt);
				//allocator.get_object(root).log("sdfsd", allocator);
				try {

					allocator.get_object(root).propagate_sz_delta(entry_pt_realptr, sz_delta, allocator);
				}
				catch(...) {
					allocator.get_object(root).log("failed to find: ", allocator);
					
					auto prefix_len = entry_pt_realptr -> get_prefix_len();
					std::printf("target_prefix: %s (len %d)\n", entry_pt_realptr -> get_prefix().to_string(prefix_len).c_str(), prefix_len.len);
					std::fflush(stdout);		
					throw;
				}
				/*try {
					root -> propagate_metadata(entry_pt, metadata);
				} catch (...) {
					std::printf("METADATA PROPAGATION FAILED\n");
					std::printf("root: %p target: %p\n", root, entry_pt);
					root -> _log("root ");
					entry_pt -> _log("entry pt ");
					std::fflush(stdout);
					throw;
				} */
			}
			//std::printf("ending execution\n");
		}
};

template<typename MergeFn>
struct AccountBatchMergeReduction {

	template<typename TrieT>
	void operator()(AccountBatchMergeRange<TrieT>& range) {
		range.template execute<MergeFn>();
	}

	AccountBatchMergeReduction() {}

	AccountBatchMergeReduction(AccountBatchMergeReduction& other, tbb::split) {}

	void join(AccountBatchMergeReduction& other) {}
};

} /* edce */
