#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <cstring>
#include <openssl/sha.h>
#include <execution>
#include <cstdint>
#include <type_traits>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <tuple>
#include <thread>
#include <unordered_set>
#include <variant>

#include "simple_debug.h"
#include "merkle_trie_utils.h"

#include "xdr/trie_proof.h"
#include "xdr/types.h"


#include "tbb/task_arena.h"

#include "coroutine_throttler.h"

namespace edce {


//METADATA TYPE IS NOT HASHED, used for preprocessing tree/search metadata/etc
//KEY LEN UNITS IS BYTES

/*
TODO

Right now, if we have metadata (and moving children around),
we need exclusive locks on TrieNodes on merge/insert.

We could instead swap to shared locks, but this requires some more
careful synchronization.

Specifically, we need to grab readlock once, then check if we can
recurse down.  If we can't, we release and grab writelock, then
try again.

If we implement this, we can downgrade lock_guards to shared_locks
in a bunch of _BaseTrie methods.



In general, _BaseTrie specifies the concurrency contract for TrieNode.

TrieNode just does implementations of that contract.


*/


/*
In SERIAL_MODE = USE_LOCKS= true, we can run parallel inserts, but then we need to lock each node.

Need atomicity on hash_valid if we have metadata deletions invalidating hash.

*/
template<
	bool SERIAL_MODE>
class OptionalLock {
};

template<>
class OptionalLock<true> {

	mutable std::shared_mutex mtx;


public:
	OptionalLock() 
		: mtx() {}

	template<typename lock_type>
	lock_type lock() const {
		mtx.lock();
		return {mtx, std::adopt_lock};
	}

};

template<>
class OptionalLock<false> {
//0-ary tuple?
// new type that does the same as a 0-ary tuple?
	//MonoState "unit type" in <variant>
public:
	OptionalLock() {}

	template<typename lock_type>
	std::monostate lock() const {
		return std::monostate{};
	}

};
template<typename TrieT, typename MetadataType>
struct BatchMergeRange;

//template<typename TrieT>
//class CoroutineTrieNodeWrapper;

template<
	uint16_t KEY_LEN_BYTES, //making this longer than 2 bytes will overflow hashing/proof protocols (but doable if necessary)
	typename ValueType = EmptyValue, 
	typename MetadataType = EmptyMetadata,
	bool USE_LOCKS = true,
	unsigned int BRANCH_BITS = 4>
class TrieNode {
public:
	constexpr static bool HAS_VALUE = !std::is_same<EmptyValue, ValueType>::value;
	constexpr static bool HAS_METADATA = !std::is_same<EmptyMetadata, MetadataType>::value;
	constexpr static bool METADATA_DELETABLE = std::is_base_of<DeletableMixin, MetadataType>::value;
	constexpr static bool HAS_SIZE = std::is_base_of<SizeMixin, MetadataType>::value;
	constexpr static bool METADATA_ROLLBACK = std::is_base_of<RollbackMixin, MetadataType>::value;

	constexpr static uint16_t KEY_LEN_BYTES_EXPORT = KEY_LEN_BYTES;
	constexpr static unsigned int BRANCH_BITS_EXPORT = BRANCH_BITS;

	//Metadata locking is slightly odd.
	//Adding/subtracting MetadataTypes to AtomicMetadataTypes can be interleaved, as in parallel_insert.
	//This is because metadata is constructed to be commutative.
	//Thus, we allow metadata modification if you own a shared_lock on a node.
	//However, that means that reads not exclusively locked might be sheared (shorn?)

	using AtomicMetadataType = typename std::conditional<
		HAS_METADATA, 
			typename MetadataType::AtomicT,
			EmptyMetadata>::type;

	using SerialTrieNode = TrieNode<KEY_LEN_BYTES, ValueType, MetadataType, false, BRANCH_BITS>;
	using ParallelTrieNode = TrieNode<KEY_LEN_BYTES, ValueType, MetadataType, true, BRANCH_BITS>;

	friend class TrieNode<KEY_LEN_BYTES, ValueType, MetadataType, !USE_LOCKS, BRANCH_BITS>;
	//friend class CoroutineTrieNodeWrapper<TrieNode<KEY_LEN_BYTES, ValueType, MetadataType, USE_LOCKS, BRANCH_BITS>>;

	using trie_ptr_t = std::unique_ptr<TrieNode>;

	using serial_trie_ptr_t = std::unique_ptr<SerialTrieNode>;
	using parallel_trie_ptr_t = std::unique_ptr<ParallelTrieNode>;

	using exclusive_lock_t = std::lock_guard<std::shared_mutex>;
	using shared_lock_t = std::shared_lock<std::shared_mutex>;

	using prefix_t = prefix_type<KEY_LEN_BYTES, BRANCH_BITS>;//std::array<unsigned char, KEY_LEN_BYTES>;//unsigned char[KEY_LEN_BYTES];

	//for reasons beyond me "const prefix_t" doesn't work - it seems to try to cast a const unsigned char* to unsigned char*, ignoring the const.
	//Needed only at interfaces with const array.data() ptrs.
	//using const_prefix = const prefix_t;//const unsigned char*;

	using hash_t = Hash;
	using children_map_t = FixedChildrenMap<trie_ptr_t, BRANCH_BITS, ValueType>;//std::unordered_map<unsigned char, trie_ptr_t>;
	//using children_map_t = std::unordered_map<unsigned char, trie_ptr_t>;
	static_assert(!std::is_void<ValueType>::value, "can't have void valuetype");
	static_assert(!std::is_void<MetadataType>::value, "can't have void metadata");

private:
	static_assert(BRANCH_BITS == 2 || BRANCH_BITS == 4 || BRANCH_BITS == 8, "invalid number of branch bits");

	constexpr static uint8_t _MAX_CHAR_VALUE = 0xFFu;
	constexpr static unsigned char SHIFT_LEN = 8-BRANCH_BITS;
	constexpr static unsigned char BRANCH_MASK = ((_MAX_CHAR_VALUE)<<SHIFT_LEN) & _MAX_CHAR_VALUE; // last & just kills a warning
	constexpr static unsigned char MAX_BRANCH_VALUE = ((_MAX_CHAR_VALUE)>>(8-BRANCH_BITS)); // 0 to this value, inclusive
public:
	constexpr static PrefixLenBits MAX_KEY_LEN_BITS = PrefixLenBits{8 * KEY_LEN_BYTES}; // key len in bits (KEY_LEN unit is BYTES)
private:

	using children_or_value_t = std::variant<ValueType, children_map_t>;

	//children_or_value_t children_or_value;

	children_map_t children;
	//std::unique_ptr<std::shared_mutex> mtx;
//	OptionalLock<USE_LOCKS> locks;

	// only 0 beyond the prefix
	prefix_t prefix;
	PrefixLenBits prefix_len;

	//ValueType value; //only valid at leaves

	std::atomic_bool hash_valid = false;

	AtomicMetadataType metadata;
	
	OptionalLock<USE_LOCKS> locks;

	//AtomicMetadataType metadata;

	hash_t hash;
	//std::atomic_bool hash_valid = false;


	//For branch_bits = i, partial_metadata[i-1] holds sum_{j<i} child_metadata[j]
	//AtomicMetadataType partial_metadata[MAX_BRANCH_VALUE];

	void update_metadata(
		int updated_idx, const MetadataType& metadata_delta) {
		if (HAS_METADATA) {
			//for (int i = updated_idx; i < MAX_BRANCH_VALUE; i++) {
			//	partial_metadata[i] += metadata_delta;
			//}
			metadata += metadata_delta;
		}
	}

	void compute_partial_metadata_unsafe() {
		//DOES NOT CLEAR METADATA IN ADVANCE

		if (prefix_len == MAX_KEY_LEN_BITS) {
			metadata.clear();

			//auto& value = std::get<ValueType>(children_or_value);
			metadata += MetadataType{children.value()};
			return;
		}

		//auto& children = std::get<children_map_t>(children_or_value);

		TRIE_INFO("computing metadata, currently %s", metadata.to_string().c_str());
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			TRIE_INFO("updating branch_bits %d with %s", (*iter).first, (*iter).second->metadata.to_string().c_str());
			update_metadata((*iter).first, (*iter).second->metadata.unsafe_load());
		}

	/*	if (children.size() == 0) {
			metadata.clear();
			metadata += MetadataType{value};
		//	metadata = MetadataType(value);
		}*/

	}


	unsigned char get_branch_bits(const prefix_t& data) const;
	//bool check_prefix(const prefix_t other);
	PrefixLenBits get_prefix_match_len(
		const prefix_t& other, const PrefixLenBits other_len = MAX_KEY_LEN_BITS) const;

	// insert preserves metadata
	template<typename InsertFn, typename InsertedValueType>
	const MetadataType _insert(const prefix_t& key, const InsertedValueType& leaf_value);

	template<typename InsertFn, typename InsertedValueType>
	const std::optional<MetadataType> _parallel_insert(const prefix_t& key, const InsertedValueType& leaf_value);

	Hash& get_hash_ptr() {
		return hash;
	}

	void clear_partial_metadata() {
		if (HAS_METADATA) {
			//for (int i = 0; i < MAX_BRANCH_VALUE; i++) {
			//	partial_metadata[i].clear();
			//}
		}
	}

	void invalidate_hash() {
		hash_valid.store(false, std::memory_order_release);
	}

	void validate_hash() {
		hash_valid.store(true, std::memory_order_release);
	}

	bool get_hash_valid() const {
		return hash_valid.load(std::memory_order_acquire);
	}


	template<typename MergeFn>
	const std::optional<MetadataType>
	_parallel_merge_in(trie_ptr_t&& other);

	template<typename MergeFn>
	const MetadataType _merge_in(trie_ptr_t&& other);

	friend struct BatchMergeRange<TrieNode, MetadataType>;

	OptionalLock<USE_LOCKS>& get_lock_ref() {
		return locks;
	}

public:

	void prefetch_full_write() const {
		for (size_t i = 0; i < sizeof(TrieNode); i+=64) {
			__builtin_prefetch(static_cast<const void*>(this) + i, 1, 2);
		}
	}	

	void print_offsets() {
		LOG("children   %lu %lu", offsetof(TrieNode, children), sizeof(children));
		LOG("locks      %lu %lu", offsetof(TrieNode, locks), sizeof(locks));
		LOG("prefix     %lu %lu", offsetof(TrieNode, prefix), sizeof(prefix));
		LOG("prefix_len %lu %lu", offsetof(TrieNode, prefix_len), sizeof(prefix_len));	
		LOG("metadata   %lu %lu", offsetof(TrieNode, metadata), sizeof(metadata));	
		LOG("hash       %lu %lu", offsetof(TrieNode, hash), sizeof(hash));	
		LOG("hash_valid %lu %lu", offsetof(TrieNode, hash_valid), sizeof(hash_valid));
	}
	struct iterator {

		using kv_t = std::pair<const prefix_t, std::reference_wrapper<const ValueType>>;

		const TrieNode& main_node;
		//const std::unique_ptr<TrieNode>& main_node;

		iterator(TrieNode& main_node) : main_node(main_node), local_iter(main_node.children.begin()) {
			if (main_node.prefix_len == MAX_KEY_LEN_BITS || (main_node.prefix_len.len == 0  && main_node.children.size() == 0)) {
				child_iter = nullptr;
			} else {
				child_iter = std::make_unique<iterator>(*((*local_iter).second));
			}
		}

		/*iterator(const std::unique_ptr<TrieNode>& main_node) : main_node(main_node), local_iter(main_node -> children.begin()) {
			if (main_node -> prefix_len == MAX_KEY_LEN_BITS || (main_node-> prefix_len.len == 0  && main_node -> children.size() == 0)) {
				child_iter = nullptr;
			} else {
				child_iter = std::make_unique<iterator>((*local_iter).second);
			}
		}*/

		typename children_map_t::iterator local_iter;

		std::unique_ptr<iterator> child_iter;

		const kv_t operator*() {
			if (main_node . prefix_len == MAX_KEY_LEN_BITS) {
				return std::make_pair(main_node.prefix, std::cref(main_node.children.value()));
			}

			if (local_iter == main_node.children.end()) {
				throw std::runtime_error("deref iter end");
			}
			return **child_iter;
		}

		bool operator++() {
			if (local_iter == main_node.children.end()) {
				return true;
				//throw std::runtime_error ("can't increment from end");
			} //if check passes, then child_ptr is not null

			if (!child_iter) {
				throw std::runtime_error("how on earth is child_iter null");
			}

			auto inc_local = ++(*child_iter);
			
			if (inc_local) {
				local_iter++;
				if (local_iter != main_node.children.end()) {
					child_iter = std::make_unique<iterator>(*((*local_iter).second));
				} else {
					child_iter = nullptr;
				}
			}
			
			if (local_iter == main_node.children.end()) {
				return true;
			}

			return false;

		}

		bool at_end() {
			return local_iter == main_node.children.end();
		}
	};

/*
	trie_ptr_t duplicate_node_only_unsafe() {
		return std::make_unique<TrieNode>(prefix, prefix_len, metadata);
	}

	TrieNode(
		const prefix_t key, 
		const PrefixLenBits prefix_len, 
		const AtomicMetadataType& metadata)
		//const ValueType& value)
		: children()
		, locks()
		, prefix(key)
		//mtx(std::make_unique<std::shared_mutex>()),
		, prefix_len(prefix_len)
		//, value(value)
		, metadata(metadata.unsafe_load()) 
		{
			invalidate_hash();
			//memcpy(prefix, key, KEY_LEN_BYTES);

			//for (int i = 0; i < MAX_BRANCH_VALUE; i++) {
			//	partial_metadata[i] = partial_metadata_old[i].duplicate();
			//}
		}*/

	const PrefixLenBits get_prefix_len() const {
		return prefix_len;
	}

	const prefix_t get_prefix() const {
		return prefix;
	}

	template<typename InsertFn, typename InsertedValueType>
	static trie_ptr_t make_value_leaf(const prefix_t key, typename std::enable_if<std::is_same<ValueType, InsertedValueType>::value, InsertedValueType>::type leaf_value) {
		return std::make_unique<TrieNode>(key, leaf_value, InsertFn::template new_metadata<MetadataType>(leaf_value));
	}

	template<typename InsertFn, typename InsertedValueType>
	static trie_ptr_t make_value_leaf(const prefix_t key, typename std::enable_if<!std::is_same<ValueType, InsertedValueType>::value, InsertedValueType>::type leaf_value) {
		ValueType value_out = InsertFn::template new_value<ValueType>(key);
		InsertFn::value_insert(value_out, leaf_value);
		return std::make_unique<TrieNode>(key, value_out, InsertFn::template new_metadata<MetadataType>(leaf_value));
	}

	// for creating a leaf node
	TrieNode(const prefix_t key, ValueType leaf_value, const MetadataType& base_metadata)
	: children(leaf_value)
	//, locks()
	  	//mtx(std::make_unique<std::shared_mutex>()),
	, prefix(key)
	, prefix_len(MAX_KEY_LEN_BITS)
	//, value(leaf_value)
	, metadata()
	{
		metadata.unsafe_store(base_metadata);
	  //	memcpy(prefix, key, KEY_LEN_BYTES);
	  	clear_partial_metadata();
	}

	bool get_is_frozen() {
		return false;//is_frozen;
	}

	bool is_leaf() {
		return prefix_len == MAX_KEY_LEN_BITS;
	}

	ValueType& get_value() {
		if (!is_leaf()) {
			throw std::runtime_error("can't get value from non leaf");
		}
		return children.value();
	}

	trie_ptr_t move_contents_to_new_node() {
		//if (is_frozen) {
		//	throw std::runtime_error("can't modify a frozen node");
		//}

		//auto output = std::make_shared<TrieNode>(std::move(children), prefix prefix

		return std::make_unique<TrieNode>(std::move(children), prefix, prefix_len);
	}

	trie_ptr_t duplicate_node_with_new_children(children_map_t&& new_children) {
		return std::make_unique<TrieNode>(std::move(new_children), prefix, prefix_len);
	}

	//for splitting a prefix into branches
	//we transfer all of the root nodes stuff to the child
	TrieNode(
		children_map_t&& new_children,
		const prefix_t old_prefix,
		const PrefixLenBits prefix_len)
//		ValueType leaf_value) 
	 	: children(std::move(new_children))
	 //	, locks()
	 	, prefix(old_prefix)
		, prefix_len(prefix_len)
	//	, value(leaf_value)
		, metadata() // Take care of setting metadata in caller
	{
		//memcpy(prefix, old_prefix, KEY_LEN_BYTES);
		clear_partial_metadata();
	}

	static trie_ptr_t make_empty_node() {
		return std::make_unique<TrieNode>();
	}


	//for creating empty trie
	//TODO make sure we default initialize everything
	// that we'll later depend on.
	// This is what gets called for default-initialization,
	// so we have to init everything we might later use
	// (normally compiler does this for us).
	TrieNode()
	  : children()
	  //, locks()
	  	//mtx(std::make_unique<std::shared_mutex>()),
	  , prefix()
	  , prefix_len{0}
	  //,	value()
	  ,	metadata() {
	  		//prefix.fill(0);
	  		//memset(prefix, 0, KEY_LEN_BYTES);
	  		clear_partial_metadata();
	  	}

	//overwrites previous key, if it exists
	template<bool x = HAS_VALUE, typename InsertFn, typename InsertedValueType> // template nonsense
	void insert(typename std::enable_if<x, const prefix_t&>::type key, const InsertedValueType& leaf_value);

	template<bool x = HAS_VALUE, typename InsertFn>
	void insert(typename std::enable_if<!x, const prefix_t&>::type key);

	template<bool x = HAS_VALUE, typename InsertFn, typename InsertedValueType> // template nonsense
	void parallel_insert(typename std::enable_if<x, const prefix_t&>::type key, const InsertedValueType& leaf_value);

	template<bool x = HAS_VALUE, typename InsertFn>
	void parallel_insert(typename std::enable_if<!x, const prefix_t&>::type key);

	template<typename... ApplyToValueBeforeHashFn>
	void compute_hash();

	template<bool x = HAS_VALUE>
	std::optional<ValueType> get_value(typename std::enable_if<x, const prefix_t&>::type query_key);

	void append_hash_to_vec(std::vector<unsigned char>& buf) {
		[[maybe_unused]]
		auto lock = locks.template lock<shared_lock_t>();
		buf.insert(buf.end(), hash.begin(), hash.end());
	}

	void copy_hash_to_buf(Hash& buffer) {
		[[maybe_unused]]
		auto lock = locks.template lock<shared_lock_t>();
		buffer = hash;
	}

	void copy_hash_to_buf(unsigned char* buffer) {
	
		[[maybe_unused]]
		auto lock = locks.template lock<shared_lock_t>();
		static_assert(sizeof(hash) == 32, "hash size nonsense");
		memcpy(buffer, hash.data(), 32);
	}

	template<bool x = HAS_SIZE>
	typename std::enable_if<x, size_t>::type size() const {
		static_assert(x == HAS_SIZE, "no funny business");

		int64_t sz = metadata.size.load(std::memory_order_acquire);
		TRIE_INFO("metadata.size:%d", sz);
		if (sz < 0) {
			throw std::runtime_error("invalid size!");
		}
		return sz;
	}

	template<bool x = HAS_SIZE>
	typename std::enable_if<!x, size_t>::type size() const {
		static_assert(x == HAS_SIZE, "no funny business");
		return uncached_size();
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, size_t>::type num_deleted_subnodes() const {
		static_assert(x == METADATA_DELETABLE, "no funny business");
		int64_t out = metadata.num_deleted_subnodes.load(std::memory_order_acquire);
		if (out < 0) {
			throw std::runtime_error("invalid number of deleted subnodes");
		}
		return out;
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<!x, size_t>::type num_deleted_subnodes() const {
		static_assert(x == METADATA_DELETABLE, "no funny business");
		return 0;
	}


	template<bool x = METADATA_ROLLBACK>
	typename std::enable_if<x, void>::type
	clear_local_rollback_metadata() {
		metadata.num_rollback_subnodes = 0;
	}



	size_t uncached_size() const;

	void accumulate_and_freeze_unhashed_nodes(
		std::vector<std::reference_wrapper<TrieNode>>& nodes);

	template<typename VectorType>
	void accumulate_values(VectorType& values) const;

	template<typename VectorType>
	void accumulate_keys(VectorType& values);

	ProofNode create_proof_node();
	void create_proof(Proof& proof, prefix_t data);

	const MetadataType get_metadata_unsafe() {
		return metadata.unsafe_load();
	}

	void set_metadata_unsafe(
		const AtomicMetadataType& other) {
		
		metadata.unsafe_store(other.unsafe_load());
		//for (int i = 0; i < MAX_BRANCH_VALUE; i++) {
		//	partial_metadata[i] = other_partial[i].duplicate();
		//}
	}

	std::vector<TrieNode*> children_list_ordered() const {
		
		[[maybe_unused]]
		auto lock = locks.template lock<shared_lock_t>();

		std::vector<TrieNode*> output;
		//std::printf("start children list ordered\n");
		for (unsigned int branch_bits = 0; branch_bits <= MAX_BRANCH_VALUE; branch_bits ++) {
			auto iter = children.find(branch_bits);
			if (iter != children.end()) {
				output.push_back((*iter).second);
			}
		}
		//std::printf("done loop\n");
		if (output.size() > MAX_BRANCH_VALUE + 1) {
			std::printf("invalid number of children!, %lu %d\n", output.size(), MAX_BRANCH_VALUE);
			throw std::runtime_error("invalid number of children!");
		}
		if (output.size() != children.size()) {
			std::printf("mismatch: output.size() %lu children.size() %lu\n", output.size(), children.size());
			throw std::runtime_error("mismatch between output.size() and children.size()");
		}
		return output;
	}

	std::vector<TrieNode*> children_list() const {

		[[maybe_unused]]
		auto lock = locks.template lock<shared_lock_t>();

		std::vector<TrieNode*> output;
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			output.push_back((*iter).second);
		}
		return output;
	}

	std::vector<std::pair<uint8_t, TrieNode*>> children_list_with_branch_bits() {
		
		std::vector<std::pair<uint8_t, TrieNode*>> output;
		//std::printf("listing children of node %p with prefix_len %d, num_children %lu\n", this, prefix_len, children.size());
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			output.emplace_back((*iter).first, (*iter).second);
		}
		return output;
	}




	std::tuple<bool, MetadataType, trie_ptr_t> // bool: tell parent to remove entirety of child
	destructive_steal_child(const prefix_t stealing_prefix, const PrefixLenBits stealing_prefix_len);

	void propagate_metadata(TrieNode* target, MetadataType metadata);

	void
	invalidate_hash_to_node_nolocks(TrieNode* target);

/*
	bool partial_metadata_integrity_check() {

		auto iter_zero = children.find(0);
		if (iter_zero != children.end()) {
			if (partial_metadata[0] != iter_zero->second->get_metadata()) {
				std::printf("case 1\n");
				std::printf("%s vs. %s\n", partial_metadata[0].to_string().c_str(), iter_zero->second->get_metadata().to_string().c_str());
				_log("");
				return false;
			}
		} else {
			if (partial_metadata[0] != MetadataType()) {
				std::printf("case 2\n");
				std::printf("%s vs. %s\n", partial_metadata[0].to_string().c_str(), MetadataType().to_string().c_str());
				return false;
			}
		}

		for (unsigned int i = 0; i <= MAX_BRANCH_VALUE; i++) {
			auto iter = children.find(i);
			MetadataType delta;
			if (iter != children.end()) {
				delta = iter->second->get_metadata();
				if (!iter->second->partial_metadata_integrity_check()) {
					return false;
				}
			}
			if (i == 0 || i == MAX_BRANCH_VALUE) continue;

			auto diff = partial_metadata[i].duplicate();
			diff -= partial_metadata[i-1];
			diff -= delta;

			if (diff != MetadataType()) {
				std::printf("case 3\n");
				std::printf("%s vs. %s + %s\n", partial_metadata[i].to_string().c_str(), partial_metadata[i-1].to_string().c_str(), delta.to_string().c_str());
				return false;
			}
		}
		return true;
	}*/

	//unsafe, but don't be a fool this doesn't run in prod
	bool metadata_integrity_check() {
		if (prefix_len == MAX_KEY_LEN_BITS) {
			return metadata.unsafe_load().operator==(MetadataType(children.value()));
		}

		MetadataType sum;
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			sum += (*iter).second->metadata.unsafe_load();
			if (!(*iter).second->metadata_integrity_check()) {
				return false;
			}
		}

		MetadataType local = metadata.unsafe_load();

		auto res = (sum.operator==(local));
		if (!res) {
			auto delta = metadata.unsafe_load();
			delta -= sum;
			std::printf("DISCREPANCY: %s\n", delta.to_string().c_str());
		}
		return res;
	}

	template<typename MergeFn>
	void merge_in(trie_ptr_t&& other) {
		{
			[[maybe_unused]]
			auto lock = locks.template lock<TrieNode::exclusive_lock_t>();
			if (size() == 0) {
				throw std::runtime_error("can't merge into empty trie!");
			}
		}


		TRIE_INFO_F(other -> _log("merging in:"));

		_merge_in<MergeFn>(std::move(other));

		TRIE_INFO_F(_log("result after merge:"));
	}

	template<typename MergeFn>
	void parallel_merge_in(trie_ptr_t&& other) {
		if (_parallel_merge_in<MergeFn>(std::move(other))) {
			return;
		}
		_merge_in<MergeFn>(std::move(other));
	}

	void _log(std::string padding) const;

	const MetadataType metadata_query(
		const prefix_t query_prefix,
		const PrefixLenBits query_len);

	template<typename MetadataOutputType, typename KeyInterpretationType, typename KeyMakerF>
	void metadata_traversal(
		std::vector<IndexedMetadata<MetadataOutputType, KeyInterpretationType, KeyMakerF>>& vec, 
		MetadataOutputType& acc_metadata,
		const PrefixLenBits query_len);

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, std::tuple<MetadataType, std::optional<ValueType>>>::type
	mark_for_deletion(const prefix_t key);

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, std::tuple<MetadataType, std::optional<ValueType>>>::type
	unmark_for_deletion(const prefix_t key);
	
	template<bool x = METADATA_DELETABLE, typename DelSideEffectFn>
	typename std::enable_if<x, std::pair<bool, MetadataType>>::type
	perform_marked_deletions(DelSideEffectFn& side_effect_handler);

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, void>::type
	clear_marked_deletions();

	template<bool x = METADATA_ROLLBACK>
	typename std::enable_if<x, void>::type
	clear_rollback();

	template<bool x = METADATA_ROLLBACK>
	typename std::enable_if<x, std::pair<bool, MetadataType>>::type
	do_rollback();

	// msg to parent: delete child, is anything deleted?, metadata change
	std::tuple<bool, std::optional<ValueType>, MetadataType> 
	perform_deletion(const prefix_t key);


	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, MetadataType>::type
	mark_subtree_lt_key_for_deletion(const prefix_t max_key);

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, MetadataType>::type
	mark_subtree_for_deletion();

	bool contains_key(const prefix_t key);

	bool single_child() {
		return children.size() == 1;
	}

	//TODO use this throughout deletions
	trie_ptr_t get_single_child() {
		if (children.size() != 1) {
			throw std::runtime_error("can't get single child from nonsingle children map");
		}
		auto out = children.extract((*children.begin()).first);
//		auto out = std::move((*children.begin()).second);
		children.clear();
		return out;
		//if (is_frozen) {
		//	throw std::runtime_error("modification of frozen trie"); // TODO remove this check after debugging
		//}
		//return std::move(children.begin()->second);
	}

	void 
	clean_singlechild_nodes(const prefix_t explore_path);
	
	/*canonical use-case
	param_t is uint64_t for amount, rounded down.
	split off up to and including amount units of endowment
	*/

	trie_ptr_t 
	endow_split(int64_t endow_threshold);

	int64_t 
	endow_lt_key(const prefix_t max_key) const;

	//template<typename MetadataPredicate>
	//trie_ptr_t
	//metadata_split(const typename MetadataPredicate::param_t split_parameter, TypeWrapper<MetadataPredicate> overload);

	template<typename ApplyFn>
	void apply(ApplyFn& func);

	template<typename ApplyFn>
	void apply(ApplyFn& func) const;

	template<typename ApplyFn>
	void apply_geq_key(ApplyFn& func, const prefix_t min_apply_key);

	template<typename ApplyFn>
	void apply_lt_key(ApplyFn& func, const prefix_t min_apply_key);

	std::optional<prefix_t> get_lowest_key();

	//trie_ptr_t deep_copy();

	//void deep_freeze();

	//const trie_ptr_t& get_node(const unsigned char* query, const int query_len);

	template<typename ModifyFn>
	void
	modify_value_nolocks(const prefix_t query_prefix, ModifyFn& fn);

	//concurrent modification will cause problems here
	TrieNode*
	get_subnode_ref_nolocks(const prefix_t query_prefix, const PrefixLenBits query_len);

	template<typename VectorType>
	void accumulate_values_parallel_worker(VectorType& vec, size_t offset) const;

	template<typename VectorType>
	NoOpTask
	coroutine_accumulate_values_parallel_worker(VectorType& vec, CoroutineThrottler& throttler, size_t offset);

	template<typename ApplyFn>
	NoOpTask
	coroutine_apply(ApplyFn& func, CoroutineThrottler& throttler);

};


/*
Parallelism Contract:
Caller must ensure that there are:
	No modifications (inserts or values) during hashing.
	No modifications (^) during freezing.
	No modifications (^) during metadata traversal
	No modificationd (^) during 

Write modifications:
Merge, insert, delete are threadsafe with each other

*/
template
<
	uint16_t KEY_LEN_BYTES, 
	typename ValueType = EmptyValue, 
	typename MetadataType = EmptyMetadata,
	bool USE_LOCKS = true,
	unsigned int BRANCH_BITS = 4
>
class _BaseTrie {
public:
	using TrieT = TrieNode<KEY_LEN_BYTES, ValueType, MetadataType, USE_LOCKS, BRANCH_BITS>;
	using prefix_t = typename TrieT::prefix_t;
	constexpr static PrefixLenBits MAX_KEY_LEN_BITS = TrieT::MAX_KEY_LEN_BITS;
protected:

	using hash_t = typename TrieT::hash_t;//unsigned char[32];


	typename TrieT::trie_ptr_t root;
	std::unique_ptr<std::shared_mutex> hash_modify_mtx;

	friend class _BaseTrie<KEY_LEN_BYTES, ValueType, MetadataType, !USE_LOCKS, BRANCH_BITS>;

	std::atomic<bool> hash_valid = false;
	hash_t root_hash;

	constexpr static bool HAS_VALUE = TrieT::HAS_VALUE;
	constexpr static bool METADATA_DELETABLE = TrieT::METADATA_DELETABLE;
	constexpr static bool HAS_METADATA = TrieT::HAS_METADATA;
	constexpr static int HAS_SIZE = TrieT::HAS_SIZE;
	constexpr static bool IS_FROZEN = TrieT::IS_FROZEN;
	constexpr static bool METADATA_ROLLBACK = TrieT::METADATA_ROLLBACK;

	_BaseTrie(typename TrieT::trie_ptr_t&& root) 
		: root(std::move(root)),
		hash_modify_mtx(std::make_unique<std::shared_mutex>()) {}

	_BaseTrie() :
		root(TrieT::make_empty_node()),
		hash_modify_mtx(std::make_unique<std::shared_mutex>()) {}

	_BaseTrie(_BaseTrie&& other) 
		: root(std::move(other.root))
		, hash_modify_mtx(std::move(other.hash_modify_mtx)) {}
		//intentionally just resets hash_valid to false, rather than wrap the atomic_bool in yet another ptr.


	//ALL _hash methods UNSAFE if not using atomic bool
	void invalidate_hash() {
		//hash_valid = false;
		hash_valid.store(false, std::memory_order_release);
	}

	void validate_hash() {
		//hash_valid = true;
		hash_valid.store(true, std::memory_order_release);
	}

	bool get_hash_valid() {
		//return hash_valid
		return hash_valid.load(std::memory_order_acquire);
	}

	template<bool use_locks_template = USE_LOCKS, typename... ApplyFn>
	void _freeze_and_hash(typename std::enable_if<use_locks_template, Hash&>::type buf);
	template<bool use_locks_template = USE_LOCKS, typename... ApplyFn>
	void _freeze_and_hash(typename std::enable_if<!use_locks_template, Hash&>::type buf);

	void get_root_hash(Hash& out);

public:

	void print_offsets() {
		root -> print_offsets();
	}

	struct iterator {
		typename TrieT::iterator iter;
		
		using kv_t = typename TrieT::iterator::kv_t;

		const kv_t operator*() { return *iter;}

		iterator& operator++() { ++iter; return *this;}
		bool at_end() {
			return iter.at_end();
		}

		iterator(_BaseTrie& trie) : iter(*trie.root) {}
	};

	iterator begin() {
		return iterator{*this};
	}
	
	/*template<bool x = std::is_assignable<typename TrieT::trie_ptr_t, const typename TrieT::trie_ptr_t&>::value>
	_BaseTrie& operator=(const std::enable_if_t<x,_BaseTrie&> other) {
		root = other.root;
		hash_modify_mtx = std::make_unique<std::shared_mutex>();
		hash_valid = other.hash_valid;
		if (hash_valid) {
			memcpy(root_hash, other.root_hash, 32);
		}
		return *this;
	}*/

	//TODO deleted partial metadata
	bool partial_metadata_integrity_check() {
		return root -> metadata_integrity_check();//partial_metadata_integrity_check();
	}

	bool metadata_integrity_check() {
		return root -> metadata_integrity_check();
	}

	//Caller is responsible for ensuring value modifications
	//during hashing process.
	//For DB, this essentially just means "no commits"
	//We require hashing before freezing, but we might not always want to freeze.
	//We require the hash bc this inherently forces a commitment to value serialization,
	// but is somewhat parallelizable (whereas freezing is harder to parallelize)

	template<bool x = HAS_SIZE>
	typename std::enable_if<x, size_t>::type size() const {
		std::shared_lock<std::shared_mutex> lock(*hash_modify_mtx);
		if (root) {
			auto sz = root->size();
			if (sz < 0) {
				throw std::runtime_error("invalid trie size");
			}
			return sz;
		}
		return 0;
	}
	template<bool x = HAS_SIZE>
	typename std::enable_if<!x, size_t>::type size() const {
		std::shared_lock<std::shared_mutex> lock(*hash_modify_mtx);
		if (root) {
			return root->uncached_size();
		}
		return 0;
	}


	size_t uncached_size() const {
		std::shared_lock lock(*hash_modify_mtx);
		if (root) {
			return root -> uncached_size();
		}
		return 0;
	}
	void _log(std::string padding) {
		if (get_hash_valid()) {
			Hash buf;
			_freeze_and_hash(buf);

			auto str = DebugUtils::__array_to_str(buf.data(), buf.size());

			LOG("%s root hash: %s", padding.c_str(), str.c_str());
		}
		root->_log(padding);
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, unsigned int>::type num_deleted_subnodes() const {
		std::shared_lock<std::shared_mutex> lock(*hash_modify_mtx);
		if (root) {
			return root->num_deleted_subnodes();
		}
		return 0;
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<!x, unsigned int>::type num_deleted_subnodes() const {
		return 0;
	}


	//returns the metadata sum of all entries with keys <= prefix
	//TODO consider killing this method if we don't use it.
	//(it's been replaced by metadata_traversal)
	MetadataType metadata_query(
		const prefix_t query_prefix, 
		const uint16_t query_len) {
		std::shared_lock<std::shared_mutex> lock(*hash_modify_mtx);
		//requires query_len <= MAX_KEY_LEN
		return root->metadata_query(query_prefix, PrefixLenBits{query_len});
	}

	template<typename MetadataOutputType, typename KeyInterpretationType, typename KeyMakerF, bool x = TrieT::HAS_METADATA>
	std::vector
	<
		IndexedMetadata<MetadataOutputType, KeyInterpretationType, KeyMakerF>
	> metadata_traversal(typename std::enable_if<x, const uint16_t>::type query_len_bits) {

		static_assert(x == TrieT::HAS_METADATA, "no funny business");
		if (query_len_bits > MAX_KEY_LEN_BITS.len){
			throw std::runtime_error("query too long");
		}
		std::vector<IndexedMetadata<MetadataOutputType, KeyInterpretationType, KeyMakerF>> vec;

		vec.reserve(size());
		//vec.clear();
		//otherwise size would double lock hash_modify_mtx
		std::lock_guard lock(*hash_modify_mtx);

		prefix_t zero_prefix;
		//zero_prefix.fill(0);
		//memset(zero_prefix, 0, KEY_LEN_BYTES);

		MetadataOutputType acc{};

		vec.push_back(IndexedMetadata<MetadataOutputType, KeyInterpretationType, KeyMakerF>(
				KeyMakerF::eval(zero_prefix), acc)); // start with 0 at bot

		if (!root) {
			return vec;
		}

		root->metadata_traversal(vec, acc, PrefixLenBits{query_len_bits});
		return vec;
	}

	bool contains_key(const prefix_t key) {
		std::shared_lock lock(*hash_modify_mtx);
		return root -> contains_key(key);
	}



	template<typename ApplyFn>
	void apply(ApplyFn& func) {
		std::lock_guard lock(*hash_modify_mtx);

		if (!root) {
			throw std::runtime_error("root is null!");
		}
		root -> apply(func);
	}

	template<typename ApplyFn>
	void apply(ApplyFn& func) const {
		std::shared_lock lock(*hash_modify_mtx);

		if (!root) {
			throw std::runtime_error("root is null!");
		}
		root -> apply(func);
	}

	template<typename ApplyFn>
	void coroutine_apply(ApplyFn& func) {
		std::lock_guard lock(*hash_modify_mtx);

		CoroutineThrottler throttler(5);

		//CoroutineTrieNodeWrapper<TrieT> wrapper; // TODO figure out a way around this?

		auto root_task = spawn_coroutine_apply(func, root.get(), throttler);//wrapper.coroutine_apply_task(func, root.get(), throttler);


		throttler.spawn(root_task);//spawn_coroutine_apply(func, root.get(), throttler));

		throttler.join();
	}

	template<typename ValueModifyFn>
	void
	parallel_batch_value_modify(ValueModifyFn& fn) const;

	//template<typename ApplyFn>
	//void
	//parallel_apply_threadlocal_acc(ApplyFn& fn) const;

	template<typename ApplyFn>
	void
	parallel_apply(ApplyFn& fn) const;

	template<typename ApplyFn>
	void apply_geq_key(ApplyFn& func, const prefix_t min_apply_key) {
		std::shared_lock lock(*hash_modify_mtx);
		if (!root) {
			throw std::runtime_error("root is null!!!");
		}
		root -> apply_geq_key(func, min_apply_key);
	}

	template<typename ApplyFn>
	void apply_lt_key(ApplyFn& func, const prefix_t threshold_key) {
		std::shared_lock lock(*hash_modify_mtx);
		if (!root) {
			throw std::runtime_error("root is null!!!");
		}
		root -> apply_lt_key(func, threshold_key);
	}


	std::optional<prefix_t>
	get_lowest_key() {
		std::shared_lock lock(*hash_modify_mtx);

		if (!root) {
			throw std::runtime_error("ROOT IS NULL");
		}
		return root -> get_lowest_key();
	}

	MetadataType get_root_metadata() {
		std::lock_guard lock(*hash_modify_mtx);
		return root -> get_metadata_unsafe(); // ok bc exclusive lock on trie
	}

	template<bool x = HAS_VALUE>
	std::optional<ValueType> get_value(typename std::enable_if<x, const prefix_t&>::type query_key) {
		static_assert(x == HAS_VALUE, "no funny business");

		std::shared_lock lock(*hash_modify_mtx);

		return root -> get_value(query_key);
	}

	template<typename VectorType>
	VectorType accumulate_values() const {
		VectorType output;
		root -> accumulate_values(output);
		return output;
	}

	template<typename VectorType>
	void accumulate_values(VectorType& vec) const {
		std::lock_guard lock(*hash_modify_mtx);

		if (!root) {
			return;
		}

		vec.reserve(root -> size());

		//root -> _log("pre acc");

		root -> accumulate_values(vec);
	}

	template<typename VectorType>
	VectorType accumulate_values_parallel() const;

	template<typename VectorType>
	void accumulate_values_parallel(VectorType& output) const;

	template<typename VectorType>
	void coroutine_accumulate_values_parallel(VectorType& output) const;



	template<typename VectorType>
	VectorType accumulate_keys() {
		VectorType output;
		if (!root) {
			return output;
		}
		output.reserve(root -> size());
		root -> accumulate_keys(output);
		return output;
	}

 	TrieT*	
	get_subnode_ref_nolocks(const prefix_t query_prefix, const PrefixLenBits query_len_bits) {
		auto res = root -> get_subnode_ref_nolocks(query_prefix, query_len_bits);
		if (res == nullptr) {
			if (!root) {
				std::printf("root shouldn't be null!!!\n");
				throw std::runtime_error("root can't be null!");
			}
			return root.get();
		}
		return res;
	}

};

//Implement frozen trie so that we can split proof construction off from main thread.
//I.e. db can process txs while proofs are constructed for values in previous blocks
// or db can hold onto a previous hashtree commitment to support incremental updates.

//Freezing retains the metadata (TODO: could do an autoconversion, but this requires
// an extra tree traversal).  No guarantees on metadata validity, unless metadata
// is already in a valid state at freeze time.

//It wouldn't be that hard to do the extra pass/template the freeze method,
// would need to gate the pass, as this seems hard for the compiler to elide (in the void valuetype case.
// if we have to convert non-void valuetypes to bytestrings, we already have to make a
//pass over the tree, so adding metadata conversion isn't hard.

template<
	uint16_t KEY_LEN_BYTES, 
	typename ValueType = EmptyValue, 
	typename MetadataType = EmptyMetadata, 
	bool USE_LOCKS = true,
	unsigned int BRANCH_BITS = 4>
class FrozenMerkleTrie : public _BaseTrie<
									KEY_LEN_BYTES, 
									ValueType, 
									MetadataType, 
									USE_LOCKS,
									BRANCH_BITS> {

	using BaseT = _BaseTrie<KEY_LEN_BYTES, ValueType, MetadataType, BRANCH_BITS>;
	using TrieT = typename BaseT::TrieT;
	using prefix_t = typename BaseT::prefix_t;
	using trie_ptr_t = typename TrieT::trie_ptr_t;
	constexpr static bool HAS_VALUE = BaseT::HAS_VALUE;


public:


	FrozenMerkleTrie(trie_ptr_t&& root_ptr) : BaseT(std::move(root_ptr)) {}
	FrozenMerkleTrie() : BaseT() {}

	Proof generate_proof(prefix_t data) {
		Proof output;
		
		BaseT::root -> create_proof(output, data);

		auto bytes = data.get_bytes_array();

		output.prefix.insert(output.prefix.end(), bytes.begin(), bytes.end());

		output.trie_size = BaseT::size();
		BaseT::root -> copy_hash_to_buf(output.root_node_hash);
		return output;
	}

	void get_hash(Hash& buffer);
};

template<
	uint16_t KEY_LEN_BYTES,
	typename ValueType = EmptyValue,
	typename MetadataType = EmptyMetadata,
	bool USE_LOCKS = true,
	unsigned int BRANCH_BITS = 4>
class MerkleTrie : public _BaseTrie<KEY_LEN_BYTES, ValueType, MetadataType, USE_LOCKS, BRANCH_BITS>{

	using BaseT = _BaseTrie<KEY_LEN_BYTES, ValueType, MetadataType, USE_LOCKS, BRANCH_BITS>;


	using SerialMerkleTrie = MerkleTrie<KEY_LEN_BYTES, ValueType, MetadataType, false, BRANCH_BITS>;

	friend class MerkleTrie<KEY_LEN_BYTES, ValueType, MetadataType, !USE_LOCKS, BRANCH_BITS>;


	constexpr static bool HAS_VALUE = BaseT::HAS_VALUE;
	constexpr static bool METADATA_DELETABLE = BaseT::METADATA_DELETABLE;
	constexpr static bool METADATA_ROLLBACK = BaseT::METADATA_ROLLBACK;

public:
	using TrieT = typename BaseT::TrieT;
	using prefix_t = typename BaseT::prefix_t;
private:
	using trie_ptr_t = typename TrieT::trie_ptr_t;

	

public:
	
	MerkleTrie(trie_ptr_t&& root) 
		: BaseT(std::move(root)) {}


	using FrozenT = FrozenMerkleTrie<KEY_LEN_BYTES, ValueType, MetadataType, USE_LOCKS, BRANCH_BITS>;

/*	template<bool x = std::is_assignable<typename TrieT::trie_ptr_t, const typename TrieT::trie_ptr_t&>::value>
	MerkleTrie& operator=(const std::enable_if_t<x,MerkleTrie&> other) {
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		if (BaseT::root -> get_is_frozen()) {
			BaseT::root = BaseT::root -> duplicate_for_writing();
		}
		BaseT::operator=(other);
		return *this;
	}*/


	MerkleTrie(MerkleTrie&& other) : BaseT(std::move(other)) {}

	MerkleTrie() : BaseT() {}

	MerkleTrie& operator=(MerkleTrie&& other) {		
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		BaseT::invalidate_hash();
		BaseT::root = std::move(other.extract_root());
		return *this;
	}

	void clear_and_reset() {
		BaseT::hash_modify_mtx = std::make_unique<std::shared_mutex>();
		clear();
	}

	void clear() {
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		BaseT::root = TrieT::make_empty_node();
		BaseT::invalidate_hash();
	}

	TrieT* dump_contents_for_detached_deletion_and_clear() {
		std::lock_guard lock(*BaseT::hash_modify_mtx);

		TrieT* out = BaseT::root.release();
		BaseT::root = TrieT::make_empty_node();
		BaseT::invalidate_hash();
		return out;
	}

	void detached_delete() {
		auto* ptr = dump_contents_for_detached_deletion_and_clear();
		std::thread([ptr] { delete ptr;}).detach();
	}

	template<typename InsertFn = OverwriteInsertFn, typename InsertedValueType = ValueType, bool x = HAS_VALUE>
	void insert(typename std::enable_if<x, const prefix_t>::type data, InsertedValueType leaf_value) {
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		//if (BaseT::root->get_is_frozen()) {
		//	BaseT::root = BaseT::root->duplicate_for_writing();
		//}

		BaseT::invalidate_hash();
		BaseT::root->template insert<x, InsertFn, InsertedValueType>(data, leaf_value);
	}

	template<typename InsertFn = OverwriteInsertFn, bool x = HAS_VALUE>
	void insert(typename std::enable_if<!x, const prefix_t>::type data) {
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		//if (BaseT::root->get_is_frozen()) {
		//	BaseT::root = BaseT::root->duplicate_for_writing();
		//}

		BaseT::invalidate_hash();
		BaseT::root->template insert<x, InsertFn>(data);
	}

	template<typename InsertFn = OverwriteInsertFn, bool x = HAS_VALUE>
	void parallel_insert(typename std::enable_if<!x, const prefix_t>::type data) {
		std::shared_lock  lock(*BaseT::hash_modify_mtx);

		//if (BaseT::root->get_is_frozen()) {
		//	lock.unlock();
		//	return insert(data);
		//}
		BaseT::invalidate_hash();
		BaseT::root -> template parallel_insert<x, InsertFn>(data);
	}

	template<typename InsertFn = OverwriteInsertFn, typename InsertedValueType = ValueType, bool x = HAS_VALUE>
	void parallel_insert(typename std::enable_if<x, const prefix_t>::type data, InsertedValueType leaf_value) {
		std::shared_lock  lock(*BaseT::hash_modify_mtx);

		//if (BaseT::root->get_is_frozen()) {
		//	lock.unlock();
		//	return insert(data, leaf_value);
		//}

		BaseT::invalidate_hash();
		BaseT::root -> template parallel_insert<x, InsertFn, InsertedValueType>(data, leaf_value);
	}




	template<typename MergeFn = OverwriteMergeFn>
	void merge_in(MerkleTrie&& other) {
		std::lock_guard<std::shared_mutex> lock(*BaseT::hash_modify_mtx);
		//if (BaseT::root->get_is_frozen()) {
		//	BaseT::root = BaseT::root->duplicate_for_writing();
		//}

		if (!other.root) {
			throw std::runtime_error("BaseTrie: merge_in other.root is null");
		}
		BaseT::invalidate_hash();

		if (BaseT:: root -> size() == 0) {
			BaseT::root = other.extract_root();;
			return;
		}

		if (other.size() == 0) {
		//	std::printf("other was empty, no op\n");
			return;
		}

		BaseT::root->template merge_in<MergeFn>(std::move(other.root));
	}

	template<typename MergeFn = OverwriteMergeFn>
	void parallel_merge_in(MerkleTrie&& other) {
		static_assert(USE_LOCKS, "can't parallel merge without locks on individual trie nodes");

		std::shared_lock<std::shared_mutex> lock(*BaseT::hash_modify_mtx);
		if (BaseT::root -> size() == 0) {
			lock.unlock();
			merge_in(std::move(other));
			return;
		}

		if (other.size() == 0) {
			return;
		}

		//if (BaseT::root->get_is_frozen()) {
		//	BaseT::root = BaseT::root->duplicate_for_writing();
		//}

		BaseT::invalidate_hash();
		BaseT::root->template parallel_merge_in<MergeFn>(std::move(other.root));
	}

	template<typename... ApplyFn>
	void freeze_and_hash(Hash& buf) {
		BaseT::template _freeze_and_hash<USE_LOCKS, ApplyFn...>(buf);
	}

	FrozenT destructive_freeze() {
		return FrozenT(std::move(BaseT::root));
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, std::optional<ValueType>>::type
	mark_for_deletion(const prefix_t key) {
		static_assert(x == METADATA_DELETABLE, "no funny business");
		std::shared_lock<std::shared_mutex> lock(*BaseT::hash_modify_mtx);
		auto [ _, value_out] = BaseT::root->mark_for_deletion(key);

		if (value_out) {
			BaseT::invalidate_hash();
		}

		return value_out;
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, std::optional<ValueType>>::type
	unmark_for_deletion(const prefix_t key) {
		static_assert(x == METADATA_DELETABLE, "no funny business");
		std::shared_lock<std::shared_mutex> lock(*BaseT::hash_modify_mtx);
		auto [_ , value_out] = BaseT::root->unmark_for_deletion(key);

		if (value_out) {
			BaseT::invalidate_hash();
		}
		
		return value_out;
	}
	
	template<bool x = METADATA_DELETABLE, typename DelSideEffectFn>
	typename std::enable_if<x, void>::type
	perform_marked_deletions(DelSideEffectFn& side_effect_handler) {
		static_assert(x == METADATA_DELETABLE, "no funny business");
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		if (BaseT::root->get_metadata_unsafe().num_deleted_subnodes == 0) { //ok bc exclusive lock on root
			return;
		}

		BaseT::invalidate_hash();
		//if (BaseT::root->get_is_frozen()) {
		//	BaseT::root = BaseT::root->duplicate_for_writing();
		//}
		auto res = BaseT::root->perform_marked_deletions(side_effect_handler);

		if (res.first) {
			BaseT::root = TrieT::make_empty_node();
		}
		if (BaseT::root->single_child()) {
			BaseT::root = BaseT::root -> get_single_child();
		}
	}

	void clean_singlechild_nodes(const prefix_t explore_path) {
		std::lock_guard lock(*BaseT::hash_modify_mtx);

		BaseT::invalidate_hash();

		BaseT::root -> clean_singlechild_nodes(explore_path);

		while(BaseT::root->single_child()) {
			BaseT::root = BaseT::root -> get_single_child();
		}
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, void>::type
	perform_marked_deletions() {
		//no locks bc invokes perform_marked_deletions to get lock
		auto null_side_effects = NullOpDelSideEffectFn{};
		perform_marked_deletions(null_side_effects);
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, void>::type
	clear_marked_deletions() {
		std::shared_lock lock(*BaseT::hash_modify_mtx);
		BaseT::root -> clear_marked_deletions();
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, void>::type
	mark_subtree_lt_key_for_deletion(const prefix_t max_key) {
		std::shared_lock lock(*BaseT::hash_modify_mtx);
		BaseT::root -> mark_subtree_lt_key_for_deletion(max_key);
	}

	template<bool x = METADATA_DELETABLE>
	typename std::enable_if<x, void>::type
	mark_entire_tree_for_deletion() {
		std::shared_lock lock(*BaseT::hash_modify_mtx);
		BaseT::root -> mark_subtree_for_deletion();
	}



	template<bool x = METADATA_ROLLBACK>
	typename std::enable_if<x, void>::type
	do_rollback() {
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		BaseT::root -> do_rollback();
		if (BaseT::root -> single_child()) {
			BaseT::root = BaseT::root -> get_single_child();
		}
	}

	template<bool x = METADATA_ROLLBACK>
	typename std::enable_if<x, void>::type
	clear_rollback() {
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		BaseT::root -> clear_rollback();
	}

	template<bool x = METADATA_ROLLBACK>
	typename std::enable_if<x, void>::type
	clear_rollback_parallel() {
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		

		ClearRollbackRange<TrieT> range(BaseT::root);

	//	std::atomic_thread_fence(std::memory_order_release);
		tbb::parallel_for(
			range,
			[](auto r) {
			//	std::atomic_thread_fence(std::memory_order_acquire);
				for (size_t i = 0; i < r.work_list.size(); i++) {
					r.work_list[i]->clear_rollback();
				}
			//	std::atomic_thread_fence(std::memory_order_release);
			});

		//std::atomic_thread_fence(std::memory_order_acquire);

	}



	template<bool x = HAS_VALUE>
	typename std::conditional<x, std::optional<ValueType>, bool>::type
	perform_deletion(const prefix_t key) {
		std::lock_guard lock(*BaseT::hash_modify_mtx);


		//if (BaseT::root->get_is_frozen()) {
		//	BaseT::root = BaseT::root->duplicate_for_writing();
		//}
		TRIE_INFO("starting new delete");
		auto [ delete_child, anything_deleted, _] = BaseT::root->perform_deletion(key);

		if (anything_deleted) {
			 BaseT::invalidate_hash();
		}

		if (delete_child) {
			BaseT::root = TrieT::make_empty_node();
			//BaseT::root->clear();
		}
		if (BaseT::root->single_child()) {
			BaseT::root = BaseT::root -> get_single_child();
		}
		if (BaseT::root -> size() == 0) {
			BaseT::root = TrieT::make_empty_node();
		}

		return (typename std::conditional<x, std::optional<ValueType>, bool>::type)anything_deleted;
	}


	MerkleTrie endow_split(int64_t endow_threshold) {
		std::lock_guard lock(*BaseT::hash_modify_mtx);

		if (endow_threshold == 0) {
			return MerkleTrie();
		}

		BaseT::invalidate_hash();

		if (!BaseT::root) {
			throw std::runtime_error("can't consume from empty trie");
		}

		auto root_endow = BaseT::root -> get_metadata_unsafe().endow; //ok bc exclusive lock on hash_modify_mtx
		if (endow_threshold > root_endow) {
			throw std::runtime_error("not enough endow");
		}
		if (endow_threshold == root_endow) {
			//consuming entire trie
			auto out = MerkleTrie(std::move(BaseT::root));
			BaseT::root = TrieT::make_empty_node();
			return out;
		}

		auto ptr = BaseT::root -> endow_split(endow_threshold);

		if (BaseT::root -> single_child()) {
			BaseT::root = BaseT::root -> get_single_child();
		}
		if (ptr) {
			return MerkleTrie(std::move(ptr));
		} else {
			return MerkleTrie();
		}
	}


	//concurrent modification might cause shorn reads
	int64_t 
	endow_lt_key(const prefix_t max_key) const {
		std::shared_lock lock(*BaseT::hash_modify_mtx);
		return BaseT::root->endow_lt_key(max_key);

	}

	std::unique_ptr<TrieT> extract_root() {
		std::lock_guard lock(*BaseT::hash_modify_mtx);
		auto out = std::unique_ptr<TrieT>(BaseT::root.release());
		BaseT::root = TrieT::make_empty_node();
		BaseT::invalidate_hash();
		return out;
	}

	template<typename MergeFn = OverwriteMergeFn>
	void batch_merge_in(std::vector<std::unique_ptr<TrieT>>&& tries);

	void
	invalidate_hash_to_node_nolocks(TrieT* target) {
		BaseT::invalidate_hash();
		BaseT::root -> invalidate_hash_to_node_nolocks(target);
	}




/*
	template<typename MetadataPredicate>
	MerkleTrie metadata_split(typename MetadataPredicate::param_t split_parameter) {
		if (split_parameter == 0) {
			return MerkleTrie();
		}
	//	if (BaseT::root->get_is_frozen()) {
	//		BaseT::root = BaseT::root->duplicate_for_writing();
	//	}

		auto& metadata = BaseT::root -> get_metadata();

		if (MetadataPredicate::param_at_most_meta(metadata, split_parameter)) {
			if (MetadataPredicate::param_exceeds_meta(metadata, split_parameter)) {
				TRIE_ERROR("param exceeds meta of entire trie %d metadata:%s", split_parameter, metadata.to_string().c_str());
				throw std::runtime_error("invalid split_param strictly exceeds entire trie metadata");
			}
			//entirety of node is consumed
			TRIE_INFO("consuming entire trie in metadata split");

			auto out = std::move(BaseT::root); //for reasons not 100% clear to me we can't do auto&& out = std::move(root); was with shared ptr
			//auto&& out = std::move(BaseT::root);
			BaseT::root = TrieT::make_empty_node();
			if (!BaseT::root) {
				throw std::runtime_error("what the fuck");
			}

			TRIE_INFO("root status=%d", (bool)BaseT::root);
			return MerkleTrie(std::move(out));
		}

		auto ptr = (BaseT::root) -> metadata_split(split_parameter, TypeWrapper<MetadataPredicate>());
		return MerkleTrie(std::move(ptr));
	}*/

	//MerkleTrie deep_copy() {
	//	return MerkleTrie(BaseT::root -> deep_copy());
	//}
};



#define TEMPLATE_SIGNATURE template<uint16_t KEY_LEN_BYTES, typename ValueType, typename MetadataType, bool USE_LOCKS, unsigned int BRANCH_BITS>
#define TEMPLATE_PARAMS KEY_LEN_BYTES, ValueType, MetadataType, USE_LOCKS, BRANCH_BITS

struct __wrapper {
	static std::string __array_to_str(const unsigned char* array, const int len) {
		std::stringstream s;
		s.fill('0');
		for (int i = 0; i < len; i++) {
			s<< std::setw(2) << std::hex << (unsigned short)array[i];
		}
		return s.str();
	}
};
//template instantiations:

TEMPLATE_SIGNATURE
void TrieNode<TEMPLATE_PARAMS>::_log(std::string padding) const {
	LOG("%sprefix %s (len %d bits)",
		padding.c_str(), 
		prefix.to_string(prefix_len).c_str(),
		prefix_len.len);
	if (get_hash_valid()) {
		LOG("%snode hash is: %s", padding.c_str(), __wrapper::__array_to_str(hash.data(), 32).c_str());
	}
	if (prefix_len == MAX_KEY_LEN_BITS) {
		std::vector<unsigned char> buf;
		auto value = children.value();
		//value.serialize();
		value.copy_data(buf);
		auto str = __wrapper::__array_to_str(buf.data(), buf.size());
		LOG("%svalue serialization is %s", padding.c_str(), str.c_str());
		buf.clear();
	}
	LOG("%saddress: %p", padding.c_str(), this);
	LOG("%smetadata: %s", padding.c_str(), metadata.to_string().c_str());
	LOG("%snum children: %d, is_frozen: %d, bv: %x",padding.c_str(), children.size(), 0, children.get_bv());//is_frozen);

	for (unsigned int bits = 0; bits <= MAX_BRANCH_VALUE; bits++) {
		auto iter = children.find(bits);
		if (iter != children.end()) {
			LOG("%schild: %x, parent_status:%s",padding.c_str(), (*iter).first, (((*iter).second)?"true":"false"));
			//if (bits != MAX_BRANCH_VALUE) {
			//	LOG("%spartial_metadata: %s", padding.c_str(), partial_metadata[bits].to_string().c_str());

			//}
			(*iter).second->_log(padding+" |    ");
		}
	}
}

//For BRANCH_BITS = 4, BRANCH_MASK = 0xF0
TEMPLATE_SIGNATURE
unsigned char 
TrieNode<TEMPLATE_PARAMS>::get_branch_bits(const prefix_t& data) const {
	
	return data.get_branch_bits(prefix_len);
}
/*
	if (prefix_len.num_prefix_bytes() > KEY_LEN_BYTES) {
		throw std::runtime_error("trying to read prefix beyond end in branch_bits");
	}
	if (prefix_len == MAX_KEY_LEN_BITS) {
		throw std::runtime_error("Can't get branch_bits on a leaf!");
	}
	unsigned char branch_bits = data.at(prefix_len.len/8) 
			& (BRANCH_MASK>>(prefix_len.len % 8));
	branch_bits>>=(8-BRANCH_BITS-(prefix_len.len % 8));
	if (branch_bits > MAX_BRANCH_VALUE) {
		std::printf("bb=%d prefix=%s len = %u\n", branch_bits,
			DebugUtils::__array_to_str(prefix.data(), prefix_len.num_prefix_bytes()).c_str(), 
			prefix_len.len);
		throw std::runtime_error("invalid branch bits!");
	}
	return branch_bits;
}
*/
TEMPLATE_SIGNATURE
size_t TrieNode<TEMPLATE_PARAMS>::uncached_size() const {
	//TRIE_INFO("computing size, prefix len %d", prefix_len);
	if (prefix_len == MAX_KEY_LEN_BITS) {
	//	TRIE_INFO("value node, return 1");
		return 1;
	} 
	unsigned int sz = 0;

	//auto& children = std::get<children_map_t>(children_or_value);
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		sz += (*iter).second->uncached_size();
	//	TRIE_INFO("child branch bits %d, cumulative sz %d", iter->first, sz);
	}
	return sz;
}

//return metadata of all subnodes <= query_prefix (up to query_len)
// query for 0x1234 with len 16 (bits) matches 0x1234FFFFF but not 0x1235
TEMPLATE_SIGNATURE
const MetadataType 
TrieNode<TEMPLATE_PARAMS>::metadata_query(
	const prefix_t query_prefix, 
	const PrefixLenBits query_len) {
	
	auto lock = locks.template lock<TrieNode::shared_lock_t>();

	auto prefix_match_len = get_prefix_match_len(
		query_prefix, query_len);

	if (prefix_match_len == query_len) {
		return metadata;
	}

	if (prefix_len > query_len) {

		// if prefix_len > query_len (and thus prefix_match_len < query_len), we do not have a match.
		// hence return empty;
		return MetadataType{};
		/*
		int diff = __wrapper::__safe_compare(prefix, query_prefix, query_len);
		//diff shouldn't be 0 bc of prefix_match_len==query_len case

		if (diff > 0) {
			return MetadataType();
		} else {
			return metadata;
		}
		*/
	}

	auto branch_bits = get_branch_bits(query_prefix);
	MetadataType metadata_out{};

	for (unsigned less_bb = 0; less_bb < branch_bits; less_bb++) {
		auto iter = children.find(less_bb);
		if (iter != children.end()) {
			metadata_out += children->second->get_metadata();
		}
	}
	//if (branch_bits) {
	//	metadata_out = partial_metadata[branch_bits-1];
	//}
	auto iter = children.find(branch_bits);
	if (iter == children.end()) {
		return metadata_out;
	}
	metadata_out += iter->second->metadata_query(query_prefix, query_len);
	return metadata_out;
}

TEMPLATE_SIGNATURE
template<typename MetadataOutputType, typename KeyInterpretationType, typename KeyMakerF>
void TrieNode<TEMPLATE_PARAMS>::metadata_traversal(
	std::vector<IndexedMetadata<MetadataOutputType, KeyInterpretationType, KeyMakerF>>& vec, 
	MetadataOutputType& acc_metadata,
	const PrefixLenBits query_len) 
{

	//no lock needed if root gets exclusive lock

	if (prefix_len >= query_len) {
		auto interpreted_key = KeyMakerF::eval(prefix);

		acc_metadata += MetadataOutputType(interpreted_key, metadata.unsafe_load()); // safe bc BaseTrie::metadata_traversal gets exclusive lock on root

		vec.push_back(
			IndexedMetadata<MetadataOutputType, KeyInterpretationType, KeyMakerF>(
				interpreted_key, acc_metadata));
		return;
	}

	//MetadataType meta_accumulator = acc_metadata;

	for (uint8_t branch_bits = 0; branch_bits <= MAX_BRANCH_VALUE; branch_bits++) {
		auto iter = children.find(branch_bits);
		if (iter != children.end()) {
			//MetadataType recurse_metadata = acc_metadata;
			//if (branch_bits) {
			//	recurse_metadata += partial_metadata[branch_bits-1];
			//}
			(*iter).second->metadata_traversal(
				vec, acc_metadata, query_len);

			//meta_accumulator += iter -> second -> get_metadata();

		}
	}
}

/*

NODE:
prefix len in bits: (2bytes)
prefix
bitvector representing which children present
for each child (sorted)
	hash of child

ROOT:
number of children [4bytes]
hash of root node

*/

template <typename ValueType, typename prefix_t>
static void compute_hash_value_node(Hash& hash_buf, const prefix_t prefix, const PrefixLenBits prefix_len, ValueType& value) {
	//try {
	//	value.serialize();
	//} catch(...) {
	//	std::printf("serialize error\n");
	//	throw;
	//}

	std::vector<unsigned char> digest_bytes;
	//size_t num_header_bytes = get_header_bytes(prefix_len);
	//size_t value_bytes = value.data_len();
	//size_t num_bytes = num_header_bytes + value_bytes;


	try {

		//unsigned char* digest_bytes = new unsigned char[num_bytes];

		write_node_header(digest_bytes, prefix, prefix_len);

		value.copy_data(digest_bytes);
		
		SHA256(digest_bytes.data(), digest_bytes.size(), hash_buf.data());
		//delete[] digest_bytes;
	} catch (...) {
		std::printf("error in compute_hash_value_node: digest_bytes.size = %lu\n", digest_bytes.size());
		//std::printf("value.serialization.size() %lu value.serialization.capacity() %lu\n", value.serialization.size(), value.serialization.capacity());
		throw;
	}
}

template<typename Map, unsigned int BRANCH_BITS, bool IGNORE_DELETED_SUBNODES, typename prefix_t, typename... ApplyToValueBeforeHashFn>
static 
typename std::enable_if<!IGNORE_DELETED_SUBNODES, void>::type 
compute_hash_branch_node(Hash& hash_buf, const prefix_t prefix, const PrefixLenBits prefix_len, const Map& children) {
	
	//int num_header_bytes = get_header_bytes(prefix_len);

	SimpleBitVector<BRANCH_BITS> bv;
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		bv.add((*iter).first);
		if (!(*iter).second) {
			throw std::runtime_error("can't recurse hash down null ptr");
		}
		(*iter).second->template compute_hash<ApplyToValueBeforeHashFn...>();

	}
	uint8_t num_children = children.size();

	//const size_t num_bytes = num_header_bytes
	//	+ bv.needed_bytes() 
	//	+ (num_children* 32);
	std::vector<unsigned char> digest_bytes;

	try {

		//auto* digest_bytes = new unsigned char[num_bytes];

		write_node_header(digest_bytes, prefix, prefix_len);

//		int cur_write_loc = num_header_bytes;
		bv.write(digest_bytes);
		//cur_write_loc += bv.needed_bytes();

		for (uint8_t i = 0; i < num_children; i++) {
			auto iter = children.find(bv.pop());

			if (!(*iter).second) {
				throw std::runtime_error("bv gave invalid answer!");
			}
			(*iter).second->append_hash_to_vec(digest_bytes);
	//		iter->second->copy_hash_to_buf(
	//			&(digest_bytes[cur_write_loc]));
	//		cur_write_loc += 32;

	//		if (cur_write_loc >= num_bytes) {
	//			throw std::runtime_error("wrote too many bytes!");
	//		}
		}

		TRIE_INFO("hash input:%s", DebugUtils::__array_to_str(digest_bytes.data(), digest_bytes.size()).c_str());

		SHA256(digest_bytes.data(), digest_bytes.size(), hash_buf.data());
		//delete[] digest_bytes;
	} catch (...) {
		std::printf("error in compute_hash_value_node: digest_bytes.size = %lu\n", digest_bytes.size());
		throw;
	}
}

template<typename Map, unsigned int BRANCH_BITS, bool IGNORE_DELETED_SUBNODES, typename prefix_t, typename... ApplyToValueBeforeHashFn>
static 
typename std::enable_if<IGNORE_DELETED_SUBNODES, void>::type 
compute_hash_branch_node(Hash& hash_buf, const prefix_t prefix, const PrefixLenBits prefix_len, const Map& children) {
	
	//int num_header_bytes = get_header_bytes(prefix_len);

	SimpleBitVector<BRANCH_BITS> bv;
	for (auto iter = children.begin(); iter != children.end(); iter++) {

		auto child_meta = (*iter).second->get_metadata_unsafe(); // ok bc either root has exclusive lock or caller has exclusive lock on node

		if (child_meta.size > child_meta.num_deleted_subnodes) {
			bv.add((*iter).first);
			if (!(*iter).second) {
				throw std::runtime_error("can't recurse hash down null ptr");
			}
			(*iter).second->template compute_hash<ApplyToValueBeforeHashFn...>();
		}
		if (child_meta.size < child_meta.num_deleted_subnodes) {
			std::printf("child_meta size: %lu child num_deleted_subnodes: %d\n", child_meta.size, child_meta.num_deleted_subnodes);
			(*iter).second->_log("my subtree:");
			throw std::runtime_error("invalid num deleted subnodes > size");
		}

	}
	uint8_t num_children = bv.size();

	if (num_children == 1) {
		// single valid subnode, subsume hash of the child instead of hashing this

		auto iter = children.find(bv.pop());
		if (!(*iter).second) {
			throw std::runtime_error("tried to look for nonexistent child node! (bv err)");
		}

		(*iter).second->copy_hash_to_buf(hash_buf);
		return;
	}


	//uint16_t num_bytes = num_header_bytes
	//	+ bv.needed_bytes() 
	//	+ (num_children* 32);
	std::vector<unsigned char> digest_bytes;

	try {

		//auto* digest_bytes = new unsigned char[num_bytes];

		write_node_header(digest_bytes, prefix, prefix_len);

		bv.write(digest_bytes);
//		int cur_write_loc = num_header_bytes;
//		bv.write_to(digest_bytes + cur_write_loc);
//		cur_write_loc += bv.needed_bytes();

		for (uint8_t i = 0; i < num_children; i++) {
			auto iter = children.find(bv.pop());
			if (!(*iter).second) {
				throw std::runtime_error("bv error?");
			}
			(*iter).second -> append_hash_to_vec(digest_bytes);
	//		iter->second->copy_hash_to_buf(
	//			&(digest_bytes[cur_write_loc]));
	//		cur_write_loc += 32;
		}

		TRIE_INFO("hash input:%s", DebugUtils::__array_to_str(digest_bytes.data(), digest_bytes.size()).c_str());

		SHA256(digest_bytes.data(), digest_bytes.size(), hash_buf.data());
	} catch (...) {
		std::printf("error in compute_hash_value_node: digest_bytes len = %lu\n", digest_bytes.size());
		throw;
	}
}



TEMPLATE_SIGNATURE
template<typename... ApplyToValueBeforeHashFn>
void TrieNode<TEMPLATE_PARAMS>::compute_hash() {

	//[[maybe_unused]]
	//auto lock = locks.template lock<TrieNode::exclusive_lock_t>();
	//std::lock_guard lock(*mtx);
	TRIE_INFO("starting compute_hash on prefix %s (len %d bits)",
		prefix.to_string(prefix_len).c_str(),
		//__wrapper::__array_to_str(
		//	prefix, 
		//	__num_prefix_bytes(prefix_len)).c_str(), 
		prefix_len.len);

	auto hash_buffer_is_valid = get_hash_valid();

	if (hash_buffer_is_valid) return;

	if (children.empty()) {

		auto& value = children.value();

		(ApplyToValueBeforeHashFn::apply_to_value(value),...);
		compute_hash_value_node(hash, prefix, prefix_len, value);
	} else {
		compute_hash_branch_node<children_map_t, BRANCH_BITS, METADATA_DELETABLE, prefix_t, ApplyToValueBeforeHashFn...>(hash, prefix, prefix_len, children);
	}

	validate_hash();
}

//assumes max block size is at most 2^32
TEMPLATE_SIGNATURE
void 
_BaseTrie<TEMPLATE_PARAMS>::get_root_hash(Hash& out) {
	//caller must lock node, so no lock here

	uint32_t num_children = root->size() - root -> num_deleted_subnodes();
	TRIE_INFO("hash num children: %d", num_children);
	constexpr int buf_size = 4 + 32;
	std::array<unsigned char, buf_size> buf;
	buf.fill(0);
	//unsigned char buf[buf_size];
	//buf[0] = num_children;

	PriceUtils::write_unsigned_big_endian(buf, num_children);
	//memcpy(buf, (unsigned char*) &num_children, 4);
	if (num_children > 0) {
		root->copy_hash_to_buf(buf.data() + 4);
	} else if (num_children == 0) {
		//taken care of by fill above
		//memset(buf + 4, 0, 32);
	} else {
		throw std::runtime_error("invalid number of children");
	}
	INFO("top level hash in num children: %lu", num_children);
	INFO("top level hash in: %s", __wrapper::__array_to_str(buf, 36).c_str());
	SHA256(buf.data(), buf.size(), out.data());
	INFO("top level hash out: %s", __wrapper::__array_to_str(out.data(), out.size()).c_str());
}

TEMPLATE_SIGNATURE
void TrieNode<TEMPLATE_PARAMS>::accumulate_and_freeze_unhashed_nodes(
	std::vector<std::reference_wrapper<TrieNode<TEMPLATE_PARAMS>>>& nodes) {

	static_assert(USE_LOCKS, "can't use this parallized hash method without locks on individual nodes");

	//caller has exclusive lock
	
	if (get_hash_valid()) return;
	//if (!is_frozen) {
	//	is_frozen = true;
		if (HAS_VALUE && prefix_len == MAX_KEY_LEN_BITS) {
			children.value().serialize();
		}
	//}

	nodes.push_back(std::ref(*this));
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		(*iter).second->accumulate_and_freeze_unhashed_nodes(nodes);
	}
}


TEMPLATE_SIGNATURE
void FrozenMerkleTrie<TEMPLATE_PARAMS>::get_hash(Hash& buffer) {
	std::lock_guard lock(*BaseT::hash_modify_mtx);
	if (BaseT::get_hash_valid()) {
		buffer = BaseT::root_hash;
		return;
	}

	//root->reset_hash_flags();
	BaseT::root->compute_hash();
	BaseT::get_root_hash(BaseT::root_hash);
	buffer = BaseT::root_hash;
	//if (buffer != nullptr) {
	//	memcpy(buffer, BaseT::root_hash, 32);
	//}
	BaseT::validate_hash();
}

TEMPLATE_SIGNATURE
template <bool use_locks_template, typename ...ApplyFn>
void _BaseTrie<TEMPLATE_PARAMS>::_freeze_and_hash(typename std::enable_if<!use_locks_template, Hash&>::type buffer) {
	static_assert(use_locks_template == USE_LOCKS, "no funny business");
	std::lock_guard lock(*hash_modify_mtx);

	if (get_hash_valid()) {
		buffer = root_hash;
		return;
	}

	if (!root) {
		throw std::runtime_error("root should not be nullptr in _freeze_and_hash");	
	}

	root -> compute_hash();

	get_root_hash(root_hash);
	//if (buffer != nullptr) {
	//	memcpy(buffer, root_hash, 32);
	//}
	buffer = root_hash;
	validate_hash();
}

template<typename TrieT, typename MetadataType>
struct BatchMergeRange {
	using map_value_t = std::vector<TrieT*>;

	//maps from nodes of main trie to lists of new subtries that will get merged in at that node.
	//the new subtries are owned by this object.
	std::unordered_map<TrieT*, map_value_t> entry_points; // maps to unpropagated metadata
	//Whichever thread executes the contents of a map_value_t own the memory.
	//Tbb needs a copy constructor for unclear reasons, but seems to only execute a range once.

	//brances of the main trie that some other node has taken responsibility for.
	std::unordered_set<TrieT*> banned_branches;

	using exclusive_lock_t = typename TrieT::exclusive_lock_t;
	using prefix_t = typename TrieT::prefix_t;


	TrieT * const root;

	//std::mutex range_mtx;

	uint64_t num_children;

	bool empty() const {
		return entry_points.size() == 0;
	}

	bool is_divisible() const {
		return (num_children >= 100) && (entry_points.size() != 0);
	}

	BatchMergeRange (TrieT* root, std::vector<std::unique_ptr<TrieT>>&& list)
		: entry_points()
		, banned_branches()
		, root(root)
		, num_children(root -> size())
		{

			map_value_t value;
			for (auto& ptr : list) {
				value.push_back(ptr.release());
			}

			entry_points.emplace(root, value);
		}

	BatchMergeRange(BatchMergeRange& other, tbb::split)
		: entry_points()
		, banned_branches(other.banned_branches)
		, root (other.root)
		, num_children(0)
	//	, lock(std::make_shared<std::mutex>()) 
	//	, already_executed(false) 
		 {

			//std::lock_guard lock_local(*other.lock);
			//if (other.already_executed) {
			//	already_executed = true;
			//	return;
			//}

			//std::lock_guard other_range_lock(other.range_mtx);


			constexpr bool print_debugs = false;

			if (other.entry_points.size() == 0) {
				std::printf("other entry pts is nonzero!\n");
				throw std::runtime_error("what the fuck");
			}

			auto original_sz = other.num_children;
			//std::printf("doing a split, original_sz = %lu\n", original_sz);

			while (num_children < original_sz /2) {
				if (other.entry_points.size() > 1) {
					auto iter = other.entry_points.begin();
					num_children += (*iter).first -> size();
					other.num_children -= (*iter).first -> size();

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
				TrieT* theft_root = other.entry_points.begin()->first;

				std::vector<std::pair<uint8_t, TrieT*>> stealable_subnodes;

				if (print_debugs) {
					std::printf("stealing from entry point %p : size %lu  prefix %s (len %u)\n", 
						theft_root, 
						theft_root->size(), 
						theft_root->get_prefix().to_string(theft_root->get_prefix_len()).c_str(),
						//DebugUtils::__array_to_str(theft_root -> get_prefix().data(), theft_root-> get_prefix_len().num_prefix_bytes()).c_str(),
						theft_root -> get_prefix_len().len);
				}
				//other entry_points is a single TrieT*.
				//We will iterate through the children of this node, stealing until we size is high enough.
				//A stolen child of theft_root (theft_candidate) becomes an entry point for us, and a banned subnode for them.
				// The TrieT*s in the map corresponding to theft_candidate are the children of the merge_in tries that correspond to theft_candidate.
				//Anything that matches theft_root + candidate branch bits can get merged in to theft_candidate.
				// I read through the TBB source, at least the points where deleting the copy ctor causes problems, and this seemed to be not an issue.
				// In fact, in two of the three spots, there's "TODO: std::move?"

				map_value_t merge_in_tries = other.entry_points.begin() -> second;
				//std::printf("tries at entry point in other: %lu\n", other.entry_points.begin()->second.size());


				prefix_t steal_prefix;

				while (num_children + 10 < other.num_children) { // + 10 for rounding error
					

					if (stealable_subnodes.size() > 1) {

						auto lock = theft_root->get_lock_ref().template lock<exclusive_lock_t>();

						auto [stolen_bb, theft_candidate] = stealable_subnodes.back();
						stealable_subnodes.pop_back();
						//do steal
						other.banned_branches.insert(theft_candidate);
						std::vector<TrieT*> stolen_merge_in_tries;
						steal_prefix = theft_root->get_prefix();
						//memcpy(steal_prefix, theft_root -> get_prefix(), TrieT::KEY_LEN_BYTES_EXPORT);

						for (auto iter = merge_in_tries.begin(); iter != merge_in_tries.end(); iter++) {

							if (theft_root -> is_leaf()) {
							//	std::printf("hit leaf\n");
								break;
							}
							//std::printf("starting set_next_branch_bits\n");
							//memcpy(steal_prefix, theft_root -> get_prefix(), TrieT::KEY_LEN_BYTES_EXPORT);
							set_next_branch_bits<prefix_t, TrieT::BRANCH_BITS_EXPORT>(steal_prefix, theft_root -> get_prefix_len(), stolen_bb);

							if (print_debugs) {
								std::printf("trial key %s (len %u) (bb = %u)\n",
									
									steal_prefix.to_string(theft_root -> get_prefix_len() + TrieT::BRANCH_BITS_EXPORT).c_str(),
									//DebugUtils::__array_to_str(steal_prefix.data(), (theft_root -> get_prefix_len() + TrieT::BRANCH_BITS_EXPORT).num_prefix_bytes()).c_str(), 
									(theft_root -> get_prefix_len() + TrieT::BRANCH_BITS_EXPORT).len,
									stolen_bb);
							}
								
							auto [do_steal_entire_subtree, metadata_delta, theft_result] = ((*iter)->destructive_steal_child(steal_prefix, theft_root -> get_prefix_len() + TrieT::BRANCH_BITS_EXPORT));
							if (theft_result) {
								
								if (print_debugs) {
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
								}
								stolen_merge_in_tries.emplace_back(theft_result.release());
							} else if (do_steal_entire_subtree) {
								//std::printf("asked to steal entire subtree!\n");
								//throw std::runtime_error("should not happen if stealing from theft_root");

							}
						}
						if (stolen_merge_in_tries.size() != 0) {
							//std::printf("stole %lu tries\n", stolen_merge_in_tries.size());
							entry_points.emplace(theft_candidate, std::move(stolen_merge_in_tries));
							num_children += theft_candidate-> size();
						} else {
							//std::printf("nothing stolen #sad so continuing, losing %lu size\n", theft_candidate -> size());
						}



						other.num_children -= theft_candidate->size();
						//std::printf("my size:%lu, my entry_points size() = %lu, other remaining size: %lu\n", num_children, entry_points.size(), other.num_children);
					} else {
						if (stealable_subnodes.size() != 0) {
							theft_root = stealable_subnodes.back().second;
							stealable_subnodes.pop_back();
						}

						auto lock = theft_root->get_lock_ref().template lock<exclusive_lock_t>();


						//std::printf("listing stealable children of node %p (len %lu, size %lu)\n", theft_root, theft_root -> get_prefix_len(), theft_root -> size());
						stealable_subnodes = theft_root -> children_list_with_branch_bits();
						//std::printf("stealable size pre filter: %lu\n", stealable_subnodes.size());
						if (theft_root -> is_leaf()) {
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

			//std::lock_guard lock(range_mtx);

			//std::lock_guard<std::mutex> lock_local(*lock);
		//	if (already_executed) {
			//	return;
			//}
			//already_executed = true;
			//std::printf("starting execution\n");
			for (auto iter = entry_points.begin(); iter != entry_points.end(); iter++) {
				auto metadata = MetadataType{};
				//std::printf("executing merge in to entry point %p (prefix: %s len %d)\n",
				//	 iter->first, DebugUtils::__array_to_str(iter->first-> get_prefix(), __num_prefix_bytes(iter->first -> get_prefix_len())).c_str(), iter->first -> get_prefix_len());
				TrieT* entry_pt = iter -> first;
				for (TrieT* node : iter -> second) {

					//This is not a race condition because:
					// if this range is being executed, it's not getting split.
					// And a range can't get executed while being split.
					// The only concern would be if tbb copies a range, then splits the copy.  This seems to not happen?  And wouldn't really make sense?
					// But it's not technically disallowed by tbb's documentation?  Unclear.  TBB's documentation is not clear.
					//TODO see if we can figure out TBB's actual contract.
					//auto lock = node -> get_lock_ref().template lock<TrieNode::exclusive_lock_t>();
					std::unique_ptr<TrieT> ptr = std::unique_ptr<TrieT>(node);
					if (ptr->size() > 0) {
						metadata += entry_pt->template _merge_in<MergeFn>(std::move(ptr));
					}
				//	std::printf("merge in node is %p (prefix: %s len %d)\n",
				//		node, DebugUtils::__array_to_str(node-> get_prefix(), __num_prefix_bytes(node -> get_prefix_len())).c_str(), node -> get_prefix_len());

				}

				try {
					root -> propagate_metadata(entry_pt, metadata);
				} catch (...) {
					std::printf("METADATA PROPAGATION FAILED\n");
					std::printf("root: %p target: %p\n", root, entry_pt);
					root -> _log("root ");
					entry_pt -> _log("entry pt ");
					std::fflush(stdout);
					throw;
				}
			}
			//std::printf("ending execution\n");
		}
};

template<typename MergeFn>
struct BatchMergeReduction {

	template<typename TrieT, typename MetadataType>
	void operator()(BatchMergeRange<TrieT, MetadataType>& range) {
		range.template execute<MergeFn>();
	}

	BatchMergeReduction() {}

	BatchMergeReduction(BatchMergeReduction& other, tbb::split) {}

	void join(BatchMergeReduction& other) {}
};

TEMPLATE_SIGNATURE
template<typename MergeFn>
void
MerkleTrie<TEMPLATE_PARAMS>::batch_merge_in(std::vector<std::unique_ptr<TrieT>>&& tries) {
	BatchMergeRange<TrieT, MetadataType> range(BaseT::root.get(), std::move(tries));

	static_assert(USE_LOCKS, "need locks on individual nodes to do parallel merging");

	//throw std::runtime_error("shouldn't get here right now, it's broken");
	BatchMergeReduction<MergeFn> reduction{};

	tbb::parallel_reduce(range, reduction);
}

//Destructive because it destroys invariant that says children.size() has to be bigger than 0
//If we added nullchecking to merge in, we could perhaps make this a shared_lock_t if we choose not to modify children map (just leave null unique_ptrs in place)
TEMPLATE_SIGNATURE
std::tuple<bool, MetadataType, typename TrieNode<TEMPLATE_PARAMS>::trie_ptr_t> // bool: tell parent to remove entirety of child
TrieNode<TEMPLATE_PARAMS>::destructive_steal_child(const prefix_t stealing_prefix, const PrefixLenBits stealing_prefix_len) {

	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::exclusive_lock_t>();

	auto prefix_match_len = get_prefix_match_len(stealing_prefix, stealing_prefix_len);

	if (prefix_match_len == stealing_prefix_len) { // prefix_match_len <= prefix_len
		//full match, steal entire subtree
		return std::make_tuple(true, get_metadata_unsafe(), nullptr);
	}

	if (prefix_match_len == prefix_len) {
		//Implies perfect match up until prefix_len < stealing_prefix_len, so we can do a recursion

		auto branch_bits = get_branch_bits(stealing_prefix);

		auto iter = children.find(branch_bits);
		if (iter == children.end()) {
			//nothing to do
			return std::make_tuple(false, MetadataType{}, nullptr);
		}

		if (!(*iter). second) {
			std::printf("nullptr errror!");
			throw std::runtime_error("destructive_steal_child found a nullptr");
		}

		auto [do_steal_entire_subtree, meta_delta, ptr] = (*iter). second -> destructive_steal_child(stealing_prefix, stealing_prefix_len);

		if (do_steal_entire_subtree) {
			update_metadata(branch_bits, -meta_delta);

			auto out = children.extract((*iter).first);
//			auto out = std::move((*iter). second);
//			children.erase(iter);
			return std::make_tuple(false, meta_delta, std::move(out));
		} else {
			if (ptr) {
				update_metadata(branch_bits, -meta_delta);
				return std::make_tuple(false, meta_delta, std::move(ptr));
			} else {
				return std::make_tuple(false, MetadataType{}, nullptr);
			}
		}
	}

	//prefix_len > prefix_match_len, so there's no valid subtree to steal
	return std::make_tuple(false, MetadataType{}, nullptr);
}

//propagates a metadata down to target (does not add metadata to target)
TEMPLATE_SIGNATURE
void
TrieNode<TEMPLATE_PARAMS>::propagate_metadata(TrieNode* target, MetadataType metadata) {

	//_log("current node:");
	//std::printf("target: %p\n", target);
	invalidate_hash();
	if (target == this) {
		return;
	}
	
//	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();


	auto branch_bits = get_branch_bits(target -> prefix);

	auto iter = children.find(branch_bits);
	if (iter == children.end()) {
		throw std::runtime_error("can't propagate metadata to nonexistent node");
	}


	update_metadata(branch_bits, metadata);
	(*iter).second -> propagate_metadata(target, metadata);
}

TEMPLATE_SIGNATURE
void
TrieNode<TEMPLATE_PARAMS>::invalidate_hash_to_node_nolocks(TrieNode* target) {
	invalidate_hash(); // invalidate hash is threadsafe
	if (target == this) {
		return;
	}

	auto branch_bits = get_branch_bits(target -> prefix);

	auto iter = children.find(branch_bits);
	if (iter == children.end()) {
		throw std::runtime_error("can't propagate metadata to nonexistent node");
	}

	(*iter).second -> invalidate_hash_to_node_nolocks(target);
}




TEMPLATE_SIGNATURE
template <bool use_locks_template, typename ...ApplyFn>
void 
_BaseTrie<TEMPLATE_PARAMS>::_freeze_and_hash(typename std::enable_if<use_locks_template, Hash&>::type buffer) {

	static_assert(use_locks_template == USE_LOCKS, "no funny business");
	std::lock_guard lock(*hash_modify_mtx);

	if (get_hash_valid()) { // hash_valid is true only if is_frozen is true
		//if (buffer != nullptr) {
		//	memcpy(buffer, root_hash, 32);
		//}
		buffer = root_hash;
		return;
	}


	//No fences needed if other locations have acquires to sync
	tbb::parallel_for(
		HashRange<TrieT>(root),
		[] (const auto& r) {
			for (size_t idx = 0; idx < r.num_nodes(); idx++) {
				r[idx] -> template compute_hash<ApplyFn...>();
			}
		});



	//std::vector<std::reference_wrapper<TrieNode<TEMPLATE_PARAMS>>> nodes;
//	root->accumulate_and_freeze_unhashed_nodes(nodes);

	/*tbb::parallel_for(
		tbb::blocked_range<std::size_t>(0, nodes.size()),
		[this, &nodes] (auto r) {
			for (auto i = r.begin(); i < r.end(); i++) {
				//nodes[i].get().compute_hash();
				auto& node_ref = nodes[i].get();
				node_ref.template compute_hash<ApplyFn...>();
			}
		});
*/
	//precompute all the node hashes
	/*std::for_each(
		std::execution::par_unseq,
		nodes.begin(),
		nodes.end(),
		[] (std::reference_wrapper<TrieNode<TEMPLATE_PARAMS>> node) {
			node.get().compute_hash();
		});*/

	root -> template compute_hash<ApplyFn...>();

	get_root_hash(root_hash);
	//if (buffer != nullptr) {	
	//	memcpy(buffer, root_hash, 32);
	//}
	buffer = root_hash;
	validate_hash();
	//std::printf("done parallelized freeze_and_hash\n");
}

TEMPLATE_SIGNATURE
ProofNode TrieNode<TEMPLATE_PARAMS>::create_proof_node() {
	if (prefix_len == MAX_KEY_LEN_BITS) {
		ProofNode output;
		PriceUtils::write_unsigned_big_endian(output.prefix_length_and_bv.data(), prefix_len.len);
		return output;
	}

	ProofNode output;

	SimpleBitVector<BRANCH_BITS> bv;
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		bv.add((*iter).first);
	}

	PriceUtils::write_unsigned_big_endian(output.prefix_length_and_bv.data(), prefix_len.len);


	bv.write_to(output.prefix_length_and_bv.data() + 2);

	PROOF_INFO("prefix_len = %u data=%s", prefix_len, DebugUtils::__array_to_str(output.prefix_length_and_bv.data(), 4).c_str());


	while (!bv.empty()) {
		auto cur_child_bits = bv.pop();
		Hash h;
		children.at(cur_child_bits)->copy_hash_to_buf(h);
		output.hashes.push_back(h);
	}
	return output;
}

TEMPLATE_SIGNATURE
void TrieNode<TEMPLATE_PARAMS>::create_proof(Proof& proof, prefix_t data) {

	proof.nodes.push_back(create_proof_node());

	if (prefix_len == MAX_KEY_LEN_BITS) {
		proof.membership_flag = 1;
		children.value().copy_data(proof.value_bytes);
		return;
	}
	
	auto branch_bits = get_branch_bits(data);
	auto iter = children.find(branch_bits);

	if (iter != children.end()) {
		(*iter).second->create_proof(proof, data);
	}

}

TEMPLATE_SIGNATURE
template<bool x, typename InsertFn, typename InsertedValueType>
void TrieNode<TEMPLATE_PARAMS>::insert(
	typename std::enable_if<x, const prefix_t&>::type data, const InsertedValueType& leaf_value) {
	static_assert(x == HAS_VALUE, "no funny games");
	_insert<InsertFn, InsertedValueType>(data, leaf_value);
}

TEMPLATE_SIGNATURE
template<bool x, typename InsertFn>
void TrieNode<TEMPLATE_PARAMS>::insert(
	typename std::enable_if<!x, const prefix_t&>::type data) {
	static_assert(x == HAS_VALUE, "no funny games");

	TRIE_INFO("Starting insert of value %s",
		data.to_string(MAX_KEY_LEN_BITS).c_str());

	TRIE_INFO("current size:%d", size());

	_insert<InsertFn, EmptyValue>(data, EmptyValue());
}

TEMPLATE_SIGNATURE
template<bool x, typename InsertFn, typename InsertedValueType>
void TrieNode<TEMPLATE_PARAMS>::parallel_insert(
	typename std::enable_if<x, const prefix_t&>::type data, const InsertedValueType& leaf_value) {
	static_assert(x == HAS_VALUE, "no funny games");
	auto res = _parallel_insert<InsertFn, InsertedValueType>(data, leaf_value);
	if (!res) {
		_insert<InsertFn, InsertedValueType>(data, leaf_value);
	}
}

TEMPLATE_SIGNATURE
template<bool x, typename InsertFn>
void TrieNode<TEMPLATE_PARAMS>::parallel_insert(
	typename std::enable_if<!x, const prefix_t&>::type data) {
	static_assert(x == HAS_VALUE, "no funny games");

	TRIE_INFO("Starting parallel insert of value %s",
		__wrapper::__array_to_str(
			data, 
			KEY_LEN_BYTES).c_str(), 
		KEY_LEN_BYTES*8);

	TRIE_INFO("current size:%d", size());

	auto res = _parallel_insert<InsertFn, EmptyValue>(data, EmptyValue{});
	if (!res) {
		_insert<InsertFn, EmptyValue>(data, EmptyValue{});
	}
}


TEMPLATE_SIGNATURE
template<typename InsertFn, typename InsertedValueType>
const std::optional<MetadataType>
TrieNode<TEMPLATE_PARAMS>::_parallel_insert(const prefix_t& data, const InsertedValueType& leaf_value) {
	
	static_assert(USE_LOCKS, "can't parallelize insertion without locks");

	throw std::runtime_error("I took the locks off of _insert to speed performance there!  parallel_insert requires those locks.");

	auto volatile lock = locks.template lock<TrieNode::shared_lock_t>();
	//std::shared_lock lock(*mtx);

	invalidate_hash();


	//if (is_frozen) {
	//	throw std::runtime_error("can't modify frozen trie node");
	//}

	auto prefix_match_len = get_prefix_match_len(data);

	if (prefix_len.len == 0 && children.empty()) {
		return std::nullopt;
		//lock.unlock();
		//return _insert<InsertFn, InsertedValueType>(data, leaf_value);
	} else if (prefix_match_len >= prefix_len) {
		if (prefix_len == MAX_KEY_LEN_BITS) {
			return std::nullopt;
			//lock.unlock();
			//return _insert<InsertFn, InsertedValueType>(data, leaf_value);
		}

		auto branch_bits = get_branch_bits(data);
		auto iter = children.find(branch_bits);
		if (iter != children.end()) {
			TRIE_INFO("parallel insert found previous child");
			auto& child = (*iter).second;
		//	if (child->is_frozen) {
		//		lock.unlock();
		//		return _insert<InsertFn, InsertedValueType>(data, leaf_value);
		//	}
			if (HAS_METADATA) {
				auto res_metadata_delta = child->template _parallel_insert<InsertFn, InsertedValueType>(data, leaf_value);
				if (res_metadata_delta) {
					update_metadata(branch_bits, *res_metadata_delta);
					return *res_metadata_delta;
				}
				auto serial_metadata_res = child -> template _insert<InsertFn, InsertedValueType>(data, leaf_value);
				update_metadata(branch_bits, serial_metadata_res);
				return serial_metadata_res;
			} else {
				auto res = child->template _parallel_insert<InsertFn, InsertedValueType>(data, leaf_value);
				if (res) {
					return MetadataType{};
				} else {
					return child-> template _insert<InsertFn, InsertedValueType>(data, leaf_value);
				}
			}
		}
	}
	return std::nullopt;
}

//TEMPLATE_SIGNATURE
//template<typename InsertFn, typename InsertedValueType>
//const MetadataType
//TrieNode<TEMPLATE_PARAMS>::_coroutine_insert(const prefix_t& data, )

TEMPLATE_SIGNATURE
template<typename InsertFn, typename InsertedValueType>
const MetadataType 
TrieNode<TEMPLATE_PARAMS>::_insert(const prefix_t& data, const InsertedValueType& leaf_value) {
	
//	[[maybe_unused]]
//	auto lock = locks.template lock<exclusive_lock_t>();
	//std::lock_guard lock = locks.template lock<std::lock_guard<std::shared_mutex>>();

	invalidate_hash();

	TRIE_INFO("Starting insert to prefix %s (len %u bits) %p",
		prefix.to_string(prefix_len).c_str(),
		prefix_len.len,
		this);
	TRIE_INFO("num children: %d", children.size());
	//_log("current trie");

	if (children.size() == 1) {
		std::printf("fucked\n");
		_log("what the fuck");
		std::fflush(stdout);
		throw std::runtime_error("children size should never be 1 (insert)");
	}

	if (children.empty() && !(prefix_len == MAX_KEY_LEN_BITS || prefix_len.len == 0)) {
		std::printf("what the actual fuck, prefix_len=%d, num children = %lu, max len = %u\n", prefix_len.len, children.size(), MAX_KEY_LEN_BITS.len);
		throw std::runtime_error("invalid initialization somewhere");
	}

	//if (is_frozen) {
	//	throw std::runtime_error("can't modify frozen trie node"); //TODO remove this check post debug
	//}

	auto prefix_match_len = get_prefix_match_len(data);

	if (prefix_match_len > MAX_KEY_LEN_BITS) {
		throw std::runtime_error("invalid prefix match len!");
	}

	TRIE_INFO("prefix match len is %d", prefix_match_len.len);
	if (prefix_len.len == 0 && children.empty()) {
		TRIE_INFO("node is empty, no children");
		//initial node ONLY
		prefix = data;
		//memcpy(prefix, data, KEY_LEN_BYTES);
		prefix_len = MAX_KEY_LEN_BITS;
		//new value
		children.set_value(InsertFn::template new_value<ValueType>(prefix));
//		children = children_map_t(InsertFn::template new_value<ValueType>(prefix));
//		value = InsertFn::template new_value<ValueType>(prefix);
		InsertFn::value_insert(children.value(), leaf_value);
		//value = leaf_value;
		if (HAS_METADATA) {
			metadata.clear();
			metadata += (InsertFn::template new_metadata<MetadataType>(children.value()));//MetadataType{value};


			//update_metadata(MetadataType(leaf_value));
			//metadata.substitute(MetadataType(value));

			//update_metadata()
			//metadata = MetadataType(leaf_value);
		}
		//new leaf: metadata change is += leaf_metadata
		return metadata.unsafe_load();//metadata.duplicate();
	} else if (prefix_match_len > prefix_len) {
		throw std::runtime_error("invalid prefix match len!");
	}
	else if (prefix_match_len == prefix_len) {
		if (prefix_len == MAX_KEY_LEN_BITS) {
			TRIE_INFO("overwriting existing key");
			InsertFn::value_insert(children.value(), leaf_value);
			//value = leaf_value;
			if (HAS_METADATA) {
				return InsertFn::metadata_insert(metadata, children.value()); // value = leaf_value already, this gets around leaf_value being potentially different type

				//auto new_metadata = MetadataType(leaf_value);
				//auto metadata_delta = new_metadata;
				//metadata_delta -= metadata.substitute(new_metadata);



				//return metadata_delta;
				//overwrite leaf: metadata change is += (new - old)
			}
			return MetadataType();
		}
		TRIE_INFO("full prefix match, recursing");
		auto branch_bits = get_branch_bits(data);
		auto iter = children.find(branch_bits);
		if (iter != children.end()) {
			TRIE_INFO("found previous child");
			auto& child = (*iter).second;//children[branch_bits];
			
			//OPTIONAL PREFETCH
			//child->prefetch_full_write();
			
			/*if (child->is_frozen) {
				children[branch_bits] = child->duplicate_for_writing();
				child = children[branch_bits];
			}*/
			if (HAS_METADATA) {
				auto metadata_delta = child->template _insert<InsertFn>(data, leaf_value);
				update_metadata(branch_bits, metadata_delta);
				return metadata_delta;
			} else {
				child->template _insert<InsertFn>(data, leaf_value);
				return MetadataType{};
			}
		} else {
			TRIE_INFO("make new leaf");
			auto new_child = TrieNode<TEMPLATE_PARAMS>::template make_value_leaf<InsertFn, InsertedValueType>(data, leaf_value);
			MetadataType new_child_meta;
			if (HAS_METADATA) {
				new_child_meta = new_child->metadata.unsafe_load(); // ok bc exclusive lock on *this
				update_metadata(branch_bits, new_child_meta);
			}
			children.emplace(branch_bits, std::move(new_child));
			return new_child_meta;
		}

	} else {
		TRIE_INFO("i don't extend current prefix, doing a break after %d", 
			prefix_match_len);
		//_log("premove ");
		auto original_child_branch = move_contents_to_new_node();
		if (HAS_METADATA) {
			original_child_branch->set_metadata_unsafe(metadata); // ok bc exclusive lock on *this, and original_child_branch is local var
		}

		auto new_child = TrieNode::template make_value_leaf<InsertFn, InsertedValueType>(data, leaf_value);
		
		//TRIE_INFO("starting clear");
		//_log("preclear ");
		children.clear();
		//TRIE_INFO("done clear");
//		value = ValueType{};

		//this becomes the join of original_child_branch and new_child
		prefix_len = prefix_match_len;
		auto branch_bits = get_branch_bits(data);
		
		auto new_child_metadata = new_child->metadata.unsafe_load(); //ok bc exclusive lock on *this
		auto original_child_metadata = metadata.unsafe_load(); // ok bc exclusive lock on *this

		TRIE_INFO("new_child_metadata:%s", new_child_metadata.to_string().c_str());
		TRIE_INFO("original_child_metadata:%s", original_child_metadata.to_string().c_str());

		children.emplace(branch_bits, std::move(new_child));
		auto old_branch_bits = get_branch_bits(prefix);
		children.emplace(old_branch_bits, std::move(original_child_branch));

		if (branch_bits == old_branch_bits) {
			throw std::runtime_error("we split at the wrong index!");
		}

		if (children.size() != 2) {
			throw std::runtime_error("invalid children size!");
		}

		if(prefix_len.len / 8 >= KEY_LEN_BYTES) {
			throw std::runtime_error("invalid prefix_len");
		}

		prefix.truncate(prefix_len);

		/*prefix.at(prefix_len.len/8) &= (0xFF << (8-(prefix_len.len % 8)));
		unsigned int zero_start = 1 + (prefix_len.len/8);
		for (size_t i = zero_start; i < KEY_LEN_BYTES; i++) {
			prefix.at(i) = 0;
		}*/
	//	if (zero_start < KEY_LEN_BYTES) {
	//		memset(&(prefix[zero_start]), 0, KEY_LEN_BYTES-zero_start);
	//	}

		if (HAS_METADATA) {
			clear_partial_metadata();
			metadata.clear();
			update_metadata(branch_bits, new_child_metadata);
			update_metadata(old_branch_bits, original_child_metadata);
			return new_child_metadata;
		}
		return MetadataType();
	}
}

TEMPLATE_SIGNATURE
template<typename MergeFn>
const std::optional<MetadataType>
TrieNode<TEMPLATE_PARAMS>::_parallel_merge_in(trie_ptr_t&& other) {
	
	static_assert(USE_LOCKS, "can't parallel merge without locks");

	auto lock = locks.template lock<TrieNode::shared_lock_t>();


	auto prefix_match_len = get_prefix_match_len(other->prefix, other->prefix_len);
	//std::min(
//		get_prefix_match_len(other->prefix), 
	//	other->prefix_len);

	invalidate_hash();



// Merge Case 0: two nodes are both leaves

	if (prefix_match_len == MAX_KEY_LEN_BITS) { //merge case 0
		return std::nullopt;
	}

	//Merge Case 1:
	// both nodes have identical prefixes
	// we just take union of children maps, merge duplicate children

	if (prefix_len == other->prefix_len && prefix_len == prefix_match_len) {
		return std::nullopt;
		//TODO could do something clever here with "if children sets are same, parallel merge them all individually" but this breaks as soon as children starts changing.

	/*	TRIE_INFO("Merging nodes with prefix %s (len %d bits)",
			__wrapper::__array_to_str(
				prefix, 
				__num_prefix_bytes(prefix_len)).c_str(), 
			prefix_len);

		MetadataType metadata_delta = MetadataType();

		for (auto other_iter = other->children.begin(); 
			other_iter != other->children.end(); 
			other_iter++) {

			auto main_iter = children.find(other_iter->first);
			TRIE_INFO("Processing BRANCH_BITS = %x", other_iter->first);
			if (main_iter == children.end()) {
				TRIE_INFO("didn't preexist");
				if (HAS_METADATA) {
					auto child_metadata = other_iter->second->metadata.unsafe_load(); //safe bc exclusive lock on this implies exclusive lock on children
					metadata_delta += child_metadata;
					update_metadata(other_iter->first, child_metadata);
				}
				children.emplace(other_iter->first, std::move((other_iter->second)));
			} else { //main_iter->first == other_iter->first, i.e. duplicate child node
				TRIE_INFO("preexisting");
				auto& merge_child = main_iter->second;

				//if (merge_child->is_frozen) {
				//	children[main_iter->first] = merge_child->duplicate_for_writing();
				//	merge_child = children[main_iter->first];
				//}

				if (HAS_METADATA) {
					auto child_metadata = (merge_child->template _merge_in<MergeFn>((std::move(other_iter->second))));
					metadata_delta += child_metadata;
					update_metadata(main_iter->first, child_metadata);
				} else {
					merge_child->template _merge_in<MergeFn>(
						(std::move(other_iter->second)));
				}
			}
			TRIE_INFO("current meta_delta:%s", metadata_delta.to_string().c_str());
		}
		TRIE_INFO("done merge");
		return metadata_delta;*/
	}

	// Merge Case 2
	// complete match on this prefix up to main_iter's prefix len, but other's prefix extends
	// thus other must become a child of this

	if (prefix_len == prefix_match_len /* and thus other->prefix_len > prefix_match_len*/) {
		//merge in with a child
		TRIE_INFO("recursing down subtree");
		auto branch_bits = get_branch_bits(other->prefix);
		auto iter = children.find(branch_bits);
		if (iter == children.end()) {
			return std::nullopt;
		}
		TRIE_INFO("using existing subtree");

		auto& merge_child = (*iter).second;

		//if (merge_child->is_frozen) {
		//	children[branch_bits] = merge_child->duplicate_for_writing();
		//	merge_child = children[branch_bits];
		//}
		if (HAS_METADATA) {
			auto delta = merge_child-> template _parallel_merge_in<MergeFn>((std::move(other)));
			if (delta) {
				update_metadata(branch_bits, *delta);
				return *delta;
			} else {
				auto serial_delta = merge_child -> template _merge_in<MergeFn>((std::move(other)));
				update_metadata(branch_bits, serial_delta);
				return serial_delta;
			}

		} else {
			if(merge_child-> template _parallel_merge_in<MergeFn>((std::move(other)))) {
				return MetadataType();
			}
			merge_child -> template _merge_in<MergeFn>(std::move(other));
			return MetadataType();
		}
	}

	// Merge Case 3 (Converse of case 2)
	// this prefix is an extension of other's prefix.  Hence, this must become child of other

	if (other->prefix_len == prefix_match_len /* and thus prefix_len > prefix_match_len*/) {
		//when using shared_ptr i decided that other should always be destructible, leading to a nontrivial
		//assymetry between cases 2 and 3.
		//this could be undone - which could enable some parallelism here.
		return std::nullopt;
	

	}

	// Merge case 4:
	// We must create a common ancestor of both this and other.

	/* other->prefix_len > prefix_match_len && prefix_len > prefix_match_len */ //merge case 4

	return std::nullopt;

}


TEMPLATE_SIGNATURE
template<typename MergeFn>
const MetadataType 
TrieNode<TEMPLATE_PARAMS>::_merge_in(trie_ptr_t&& other) {

	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::exclusive_lock_t>();
	invalidate_hash();

	if (!other) {
		throw std::runtime_error("other is null!!!");
	}

	//can happen during intermediate stages of batch_merge_in bc of subtrie stealing.
	/*if (other->size() == 0) {
		_log("bad node ");
		std::fflush(stdout);
		throw std::runtime_error("should not _merge in an empty trie!");
	}*/

	if (children.size() == 0 && prefix_len.len == 0) {
		throw std::runtime_error("cannot _merge into empty trie!  Must call merge or somehow guard against this.");
	}

	/*
	//this can actually happen during batch_merge.  We are no longer using batch_merge, so shouldn't happen
	//TODO run an integrity check post batch_merge
	if (children.size() == 1) {
		_log("bad node ");
		std::fflush(stdout);
		throw std::runtime_error("children size should never be 1! (merge)");
	}*/

	TRIE_INFO("Starting merge_in to prefix %s (len %d bits)",
		prefix.to_string(prefix_len).c_str(),
		prefix_len.len);
	TRIE_INFO("num children: %d", children.size());
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		TRIE_INFO("child: %x, parent_status:%s", 
			(*iter).first, 
			(((*iter).second)?"true":"false"));
		if ((*iter). first > MAX_BRANCH_VALUE) {
			throw std::runtime_error("invalid iter->first!");
		}
	}
	TRIE_INFO_F(_log("current state:    "));

	TRIE_INFO("other is not null?:%s", (other?"true":"false"));
	TRIE_INFO("other prefix len %d", other->prefix_len);
	auto prefix_match_len = get_prefix_match_len(other -> prefix, other -> prefix_len);

	//std::min(
	//	get_prefix_match_len(other->prefix), 
	//	other->prefix_len);

	if (prefix_match_len > MAX_KEY_LEN_BITS) {
		throw std::runtime_error("invalid too long prefix_match_len");
	}

	// Merge Case 0: two nodes are both leaves

	if (prefix_match_len == MAX_KEY_LEN_BITS) { //merge case 0
		TRIE_INFO("Full match, nothing to do but copy value");

		if (HAS_VALUE) {
			MergeFn::value_merge(children.value(), other->children.value());
			//value = other->value;
		}
		if (HAS_METADATA) {
			TRIE_INFO("\toriginal_metadata: %s", metadata.unsafe_load().to_string().c_str());

			auto metadata_delta = MergeFn::metadata_merge(metadata, other->metadata);

			//auto original_metadata = metadata.substitute(other->metadata);
			//auto metadata_delta = metadata.duplicate();
			//metadata_delta -= original_metadata;
			//auto metadata_delta = other->metadata;
			//metadata_delta -= metadata;
			//metadata = other->metadata;
			TRIE_INFO("\tnew metadata:%s", metadata.unsafe_load().to_string().c_str());
			TRIE_INFO("\tmetadata delta:%s", metadata_delta.to_string().c_str());
			clear_partial_metadata();
			return metadata_delta;
			//overwrite leaf: metadata change is += (new - old)
		}
		return MetadataType();
	}

	//Merge Case 1:
	// both nodes have identical prefixes
	// we just take union of children maps, merge duplicate children

	if (prefix_len == other->prefix_len && prefix_len == prefix_match_len) {
		TRIE_INFO("Merging nodes with prefix %s (len %d bits)",
			prefix.to_string(prefix_len).c_str(),
			prefix_len.len);

		MetadataType metadata_delta = MetadataType();

		for (auto other_iter = other->children.begin(); 
			other_iter != other->children.end(); 
			other_iter++) {

			if ((*other_iter).first > MAX_BRANCH_VALUE) {
				throw std::runtime_error("invalid other->first");
			}

			auto main_iter = children.find((*other_iter).first);
			TRIE_INFO("Processing BRANCH_BITS = %x", (*other_iter).first);
			if (main_iter == children.end()) {
				TRIE_INFO("didn't preexist");
				if (HAS_METADATA) {
					auto child_metadata = (*other_iter).second->metadata.unsafe_load(); //safe bc exclusive lock on this implies exclusive lock on children
					metadata_delta += child_metadata;
					update_metadata((*other_iter).first, child_metadata);
				}
				children.emplace((*other_iter).first, other->children.extract((*other_iter).first));//std::move(((*other_iter).second)));
			} else { //main_iter->first == other_iter->first, i.e. duplicate child node
				TRIE_INFO("preexisting");
				auto& merge_child = (*main_iter).second;

				if (!merge_child) {
					throw std::runtime_error("merging into nullptr!");
				}

				//if (merge_child->is_frozen) {
				//	children[main_iter->first] = merge_child->duplicate_for_writing();
				//	merge_child = children[main_iter->first];
				//}

				if (HAS_METADATA) {
					auto child_metadata = (merge_child->template _merge_in<MergeFn>(other->children.extract((*other_iter).first)));//(std::move((*other_iter).second))));
					metadata_delta += child_metadata;
					update_metadata((*main_iter).first, child_metadata);
				} else {
					merge_child->template _merge_in<MergeFn>(
						other->children.extract((*other_iter).first));
	//					(std::move((*other_iter).second)));
				}
			}
			TRIE_INFO("current meta_delta:%s", metadata_delta.to_string().c_str());
		}
		TRIE_INFO("done merge");
		return metadata_delta;
	}

	// Merge Case 2
	// complete match on this prefix up to main_iter's prefix len, but other's prefix extends
	// thus other must become a child of this

	if (prefix_len == prefix_match_len /* and thus other->prefix_len > prefix_match_len*/) {
		//merge in with a child
		TRIE_INFO("recursing down subtree");

		if (other -> prefix_len < prefix_len) {
			std::printf("prefix_len %u other->prefix_len %u\n", prefix_len.len, other->prefix_len.len);
			throw std::runtime_error("cannot happen!");
		}
		auto branch_bits = get_branch_bits(other->prefix);
		auto iter = children.find(branch_bits);
		if (iter == children.end()) {
			TRIE_INFO("making new subtree");
			auto other_metadata = other->metadata.unsafe_load(); // safe bc moving other into this fn gives us implicit exclusive ownership
			children.emplace(branch_bits, std::move(other));
			if (HAS_METADATA) {
				//metadata += other_metadata;
				update_metadata(branch_bits, other_metadata);
				return other_metadata;
			} else {
				return MetadataType();
			}
		}
		TRIE_INFO("using existing subtree");

		auto& merge_child = (*iter).second;

		//if (merge_child->is_frozen) {
		//	children[branch_bits] = merge_child->duplicate_for_writing();
		//	merge_child = children[branch_bits];
		//}
		if (HAS_METADATA) {
			auto delta = merge_child-> template _merge_in<MergeFn>((std::move(other)));
			update_metadata(branch_bits, delta);
			//metadata += delta;
			return delta;
		} else {
			merge_child-> template _merge_in<MergeFn>((std::move(other)));
			return MetadataType();
		}
	}

	// Merge Case 3 (Converse of case 2)
	// this prefix is an extension of other's prefix.  Hence, this must become child of other

	if (other->prefix_len == prefix_match_len /* and thus prefix_len > prefix_match_len*/) {
		TRIE_INFO("merge case 3");

		auto original_child_branch = move_contents_to_new_node();
		if (HAS_METADATA) {
			original_child_branch->set_metadata_unsafe(metadata); // safe bc obj is threadlocal rn and exc lock on metadata
		}

		children.clear();
		children = std::move((other->children));
		
		other -> children.clear();

		prefix_len = other -> prefix_len;
		//value = ValueType{};

		auto original_child_branch_bits = get_branch_bits(prefix);
		
		prefix = other->prefix;
		//memcpy(prefix, other->prefix, KEY_LEN_BYTES);

		set_metadata_unsafe(other->metadata);// safe bc move gives us implicit exclusive ownership of other and lock on this
		
		TRIE_INFO ("original_child_branch_bits: %d", original_child_branch_bits);



		//prefix[prefix_len/8] &= (0xFF << (8-(prefix_len % 8)));
		//int zero_start = 1 + (prefix_len/8);
		//memset(&(prefix[zero_start]), 0, KEY_LEN_BYTES-zero_start);

		auto iter = children.find(original_child_branch_bits);
		if (iter==children.end()) {
			TRIE_INFO("no recursion case 3");
			auto original_child_metadata = original_child_branch->metadata.unsafe_load(); // safe bc obj is threadlocal rn
			children.emplace(original_child_branch_bits, std::move(original_child_branch));
			//TRIE_INFO_F(_log("post case 3 action:    "));
			if (HAS_METADATA) {
				update_metadata(original_child_branch_bits, original_child_metadata);
				return other->metadata.unsafe_load(); //safe bc move gives exclusive access to thread
			} else {
				return MetadataType{};
			}
		} else {
			TRIE_INFO("case 3 recursing");
			auto original_metadata = original_child_branch->metadata.unsafe_load(); // safe bc original_child_branch is threadlocal

			auto matching_subtree_of_other = children.extract((*iter).first);//std::move(((*iter).second)); // children was replaced by other's children
			//children.erase(iter);

			//if ((*iter).second) {
			//	throw std::runtime_error("didn't take ownership of iter??");
			//} 
			
			
			if (!matching_subtree_of_other) {
				throw std::runtime_error("matching_subtree_of_other is null???");
			}



			//We do the swap here so that the input to _merge is always destructible, as per invariant.
			//Metadata is commutative, so this doesn't change return value of metadata_delta.  FAMOUS LAST WORDS LMAO
			children.emplace(original_child_branch_bits, std::move(original_child_branch));


			//metadata adjustment corresponding to swapping pre-merge the matching subtrees
			auto metadata_reduction = matching_subtree_of_other->metadata.unsafe_load(); // safe bc obj is threadlocal after the std::move call
			metadata_reduction -= original_metadata;

			auto meta_delta = children.at(original_child_branch_bits)->template _merge_in<MergeFn>(std::move(matching_subtree_of_other));

			meta_delta -= metadata_reduction;


			
			//auto meta_delta = iter->second->_merge_in(std::move(original_child_branch));
			//TRIE_INFO_F(_log("post case 3 recursion:    "));
			if (HAS_METADATA) {
				update_metadata(original_child_branch_bits, meta_delta);
				//metadata += meta_delta;
				auto change_from_original = metadata.unsafe_load(); // safe bc exclusive lock on this
				change_from_original -= original_metadata;
				return change_from_original;
			}
			else {
				return MetadataType{};
			}
		}

	}

	// Merge case 4:
	// We must create a common ancestor of both this and other.

	/* other->prefix_len > prefix_match_len && prefix_len > prefix_match_len */ //merge case 4

	auto original_child_branch = move_contents_to_new_node();
	if (HAS_METADATA) {
		original_child_branch->set_metadata_unsafe(metadata);// safe bc obj is threadlocal and exclusive lock on metadata
	}
	children.clear();
	//value = ValueType{};
	prefix_len = prefix_match_len;

	auto original_branch_bits = get_branch_bits(prefix);
	auto other_branch_bits = get_branch_bits(other->prefix);
	auto other_metadata = other->metadata.unsafe_load(); // safe bc other is xvalue etc

	children.emplace(original_branch_bits, std::move(original_child_branch));
	children.emplace(other_branch_bits, std::move(other));

	//>= instead of > because we don't want equality here - prefix_len has been reduced to match len from it's potentially maximal length.
	if (prefix_len.num_fully_covered_bytes() >= KEY_LEN_BYTES) {
		throw std::runtime_error("invalid prefix_len");
	}

	prefix.truncate(prefix_len);

/*	prefix.at(prefix_len.len/8) &= (0xFF << (8-(prefix_len.len % 8)));
	size_t zero_start = 1 + (prefix_len.len/8);
	for (size_t i = zero_start; i < KEY_LEN_BYTES; i++) {
		prefix.at(i) = 0;
	}*/
	//if (zero_start < KEY_LEN_BYTES) {
	//	memset(&(prefix[zero_start]), 0, KEY_LEN_BYTES-zero_start);
	//}


	if (HAS_METADATA) {
		clear_partial_metadata();

		auto original_child_metadata = metadata.unsafe_load(); // safe bc exclusve lock on self
		metadata.clear();

		update_metadata(original_branch_bits, original_child_metadata);
		update_metadata(other_branch_bits, other_metadata);
		//metadata += other->metadata;
		return other_metadata;
	} else {
		return MetadataType();
	}
}
/*
TEMPLATE_SIGNATURE
bool TrieNode<TEMPLATE_PARAMS>::check_prefix(const prefix_t other) {
	int num_full_bytes = prefix_len.num_fully_covered_bytes();
	int res = memcmp(prefix.data(), other.data(), num_full_bytes);
	int remainder = (prefix_len.len) % 8;
	if (res!= 0) {
		return false;
	}
	char mask = (0xFF)<< (8-remainder);
	if (num_full_bytes == KEY_LEN_BYTES) {
		//don't want to accidentally read beyond end.
		return true;
	}
	return (other.at(num_full_bytes)&mask) == (prefix.at(num_full_bytes) & mask);
}*/

TEMPLATE_SIGNATURE
PrefixLenBits TrieNode<TEMPLATE_PARAMS>::get_prefix_match_len(
	const prefix_t& other, const PrefixLenBits other_len) const {

	return prefix.get_prefix_match_len(prefix_len, other, other_len);
	/*uint16_t res = MAX_KEY_LEN_BITS.len;
	for (unsigned int i = 0; i < other_len.num_prefix_bytes(); i++) { // fucking bullshit line
		unsigned char byte_dif = other.at(i) ^ prefix.at(i);
		if (byte_dif!=0) {
			res = (i * 8) + (__builtin_clz(byte_dif)-24); // TODO replace with appropriate sizeof
			break;
		}
	}
	res -= res % BRANCH_BITS;
	return std::min({PrefixLenBits{res}, prefix_len, other_len});
*/
}

TEMPLATE_SIGNATURE
template<bool x>
typename std::enable_if<x, std::tuple<MetadataType, std::optional<ValueType>>>::type
TrieNode<TEMPLATE_PARAMS>::unmark_for_deletion(const prefix_t key) {
	static_assert(x == METADATA_DELETABLE, "no funny business");

	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();

	auto prefix_match_len = get_prefix_match_len(key);

	if (prefix_match_len < prefix_len) {
		//incomplete match, which means that key doesn't exist.
		return std::make_tuple(MetadataType(), std::nullopt);
	}

	if (prefix_len == MAX_KEY_LEN_BITS) {
		auto swap_lambda = [] (AtomicDeletableMixin& object) {
			AtomicDeletableMixin::BaseT::value_t expect = 1, desired = 0;
			return AtomicDeletableMixin::compare_exchange(
				object, expect, desired);
		};
		bool res = swap_lambda(metadata);//metadata.try_execute_function(swap_lambda);

		//bool res = metadata.num_deleted_subnodes
		//	.compare_exchange_strong(1, 0);
		//swaps 1 to 0 if it's 1, otherwise it's 0 already.
		auto meta_out = MetadataType();
		meta_out.num_deleted_subnodes = res?-1:0;
		if (!res) {
			return std::make_tuple(meta_out, std::nullopt);
		} else {

			invalidate_hash();
			return std::make_tuple(meta_out, children.value());
			/*if (is_frozen) {
				return std::make_tuple(meta_out, MutableValueType(value.frozen_value));
			} else {
				return std::make_tuple(meta_out, value.mutable_value);
			}*/
		}
	}

	auto branch_bits = get_branch_bits(key);

	auto iter = children.find(branch_bits);
	if (iter == children.end()) {
		return std::make_tuple(MetadataType(), std::nullopt);
	}

	auto res = (*iter).second->unmark_for_deletion(key);
	auto [metadata_change, deleted_obj] = res;

	if (deleted_obj) {
		invalidate_hash();
	}
	update_metadata(branch_bits, metadata_change);
	return res;

}

TEMPLATE_SIGNATURE
template<bool x>
typename std::enable_if<x, std::tuple<MetadataType, std::optional<ValueType>>>::type
TrieNode<TEMPLATE_PARAMS>::mark_for_deletion(const prefix_t key) {

	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();

	auto prefix_match_len = get_prefix_match_len(key);

	if (prefix_match_len < prefix_len) {
		//incomplete match, which means that key doesn't exist.
		return std::make_tuple(MetadataType(), std::nullopt);
	}

	if (prefix_match_len == MAX_KEY_LEN_BITS) {

		auto swap_lambda = [] (AtomicDeletableMixin& object) {
			AtomicDeletableMixin::BaseT::value_t expect = 0, desired = 1;
			return AtomicDeletableMixin::compare_exchange(
				object, expect, desired);
		};
		bool res = swap_lambda(metadata);
		//swaps 0 to 1 if it's 0, otherwise it's 1 already.
		auto meta_out = MetadataType();
		meta_out.num_deleted_subnodes = res?1:0;
		if (!res) {
			return std::make_tuple(meta_out, std::nullopt);

			//hash not invalidated because nothing deleted
		} else {
			invalidate_hash();
			return std::make_tuple(meta_out, children.value());

			/*if (is_frozen) {
				return std::make_tuple(meta_out, MutableValueType(value.frozen_value));
			} else {
				return std::make_tuple(meta_out, value.mutable_value);
			}*/
		}

	}

	auto branch_bits = get_branch_bits(key);

	auto iter = children.find(branch_bits);
	if (iter == children.end()) {
		return std::make_tuple(MetadataType(), std::nullopt);

		//hash not invalidated because nothing marked as deleted
	}

	invalidate_hash();
	auto res = (*iter).second->mark_for_deletion(key);
	auto [ metadata_change, deleted_obj] = res;

	if (deleted_obj) {
		invalidate_hash();
	}
	update_metadata(branch_bits, metadata_change);
	return res;
}

TEMPLATE_SIGNATURE
template<bool x, typename DelSideEffectFn>
typename std::enable_if<x, std::pair<bool, MetadataType>>::type
TrieNode<TEMPLATE_PARAMS>::perform_marked_deletions(DelSideEffectFn& side_effect_handler) {
	static_assert(x == METADATA_DELETABLE, "no funny business");

	//no lock needed because BaseTrie gets exclusive lock


	TRIE_INFO("Starting perform_marked_deletions to prefix %s (len %d bits)",
		prefix.to_string(prefix_len).c_str(),
		prefix_len.len);
	TRIE_INFO("num children: %d", children.size());
	TRIE_INFO("metadata:%s", metadata.to_string().c_str());
	TRIE_INFO_F(_log("current subtree:    "));

	if (metadata.num_deleted_subnodes == 0) {
		TRIE_INFO("no subnodes, returning");
		return std::make_pair(false, MetadataType());
	}

	invalidate_hash();

	if (prefix_len == MAX_KEY_LEN_BITS && metadata.num_deleted_subnodes == 1) {
		side_effect_handler(prefix, children.value());
		return std::make_pair(true, -metadata.unsafe_load()); // safe bc exclusive lock
	}

	if (prefix_len == MAX_KEY_LEN_BITS && metadata.num_deleted_subnodes != 0) {
		throw std::runtime_error("can't have num deleted subnodes not 0 or 1 at leaf");
	}

	//hash_buffer_contents_valid = false;

	//if (metadata.num_deleted_subnodes == size()) {
	//	TRIE_INFO("deleting entire subtree");
	//	return std::make_pair(true, -metadata.duplicate());
	//}

	auto metadata_delta = MetadataType();


	//for (auto iter = children.begin(); iter != children.end();) {
	for (unsigned int branch_bits = 0; branch_bits <= MAX_BRANCH_VALUE; branch_bits++) {
		TRIE_INFO("scanning branch bits %d", branch_bits);

		auto iter = children.find(branch_bits);
		if (iter == children.end()) {
			continue;
		}

		auto& child_ptr = (*iter).second;
		/*if (child_ptr->metadata.num_deleted_subnodes > 0 && child_ptr ->is_frozen) {
			children[branch_bits] = std::move(child_ptr->duplicate_for_writing());
			child_ptr = children[branch_bits];
		}*/
		if (!child_ptr) {
			_log("bad node");
			throw std::runtime_error("tried to perform marked deletions on null child");
		}


		auto result = child_ptr->perform_marked_deletions(side_effect_handler);
		update_metadata(branch_bits, result.second);
		if (result.first) {
			TRIE_INFO("deleting subtree");
			children.erase(branch_bits);
		} else {
			//auto child_sz = iter->second->size();
			if (child_ptr->single_child()) {

				auto single_child = child_ptr->get_single_child();//std::move((*iter).second->get_single_child());
				TRIE_INFO("contracting size 1 subtree, prefix len %d", prefix_len);
				//auto&& replacement_child = std::move(
				//	child_ptr->children.begin()->second);
				children.emplace((*iter).first, std::move(single_child));//std::move((*iter). second -> get_single_child()));
				//children[iter->first].swap(replacement_child);
				//children.emplace(iter->first, replacement_child);
			} 
			//iter++;
		}
		metadata_delta += result.second;
	}
	TRIE_INFO("done scanning");
	return std::make_pair(children.empty(), metadata_delta);
}

TEMPLATE_SIGNATURE
void 
TrieNode<TEMPLATE_PARAMS>::clean_singlechild_nodes(const prefix_t explore_path) {
	[[maybe_unused]]
	auto lock = locks.template lock<exclusive_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {
		return;
	}


	for (uint8_t bb = 0; bb <= MAX_BRANCH_VALUE;) {
	//for (auto iter = children.begin(); iter != children.end(); iter++) {
		auto iter = children.find(bb);
		if (iter == children.end()) {
			bb++;
		} else {
			auto& ptr = (*iter).second;
			if (ptr->single_child()) {
				children.emplace((*iter).first, std::move(ptr->get_single_child()));
			} else {
				bb++;
			}
		}
	}

	auto bb = get_branch_bits(explore_path);

	auto next_iter = children.find(bb);
	if (next_iter != children.end()) {
		(*next_iter).second -> clean_singlechild_nodes(explore_path);
	}
}
	



TEMPLATE_SIGNATURE
template<bool x>
typename std::enable_if<x, void>::type
TrieNode<TEMPLATE_PARAMS>::clear_marked_deletions() {
	static_assert(x == METADATA_DELETABLE, "no funny business");

	if (metadata.unsafe_load().num_deleted_subnodes == 0) { // ok bc root gets exclusive lock
		return;
	}
	metadata.num_deleted_subnodes = 0;

	for (auto iter = children.begin(); iter != children.end(); iter++) {
		(*iter).second -> clear_marked_deletions();
	}
}

TEMPLATE_SIGNATURE
template<bool x>
typename std::enable_if<x, void>::type
TrieNode<TEMPLATE_PARAMS>::clear_rollback() {
	static_assert(x == METADATA_ROLLBACK, "no funny business");

	if (metadata.unsafe_load().num_rollback_subnodes == 0) { // ok bc root gets exclusive lock
		return;
	}
	metadata.num_rollback_subnodes = 0;

	for (auto iter = children.begin(); iter != children.end(); iter++) {
		(*iter).second -> clear_rollback();
	}
}

TEMPLATE_SIGNATURE
template<bool x>
typename std::enable_if<x, std::pair<bool, MetadataType>>::type
TrieNode<TEMPLATE_PARAMS>::do_rollback() {
	static_assert(x == METADATA_ROLLBACK, "no funny business");

	//no lock needed because BaseTrie gets exclusive lock

	if (metadata.num_rollback_subnodes == 0) {
		TRIE_INFO("no subnodes, returning");
		return std::make_pair(false, MetadataType());
	}

	invalidate_hash();

	if (prefix_len == MAX_KEY_LEN_BITS && metadata.num_rollback_subnodes == 1) {
		return std::make_pair(true, -metadata.unsafe_load()); // safe bc exclusive lock
	}

	if (prefix_len == MAX_KEY_LEN_BITS && metadata.num_rollback_subnodes > 1) {
		throw std::runtime_error("can't have num rollback subnodes > 1 at leaf");
	}

	//hash_buffer_contents_valid = false;

	//if (metadata.num_deleted_subnodes == size()) {
	//	TRIE_INFO("deleting entire subtree");
	//	return std::make_pair(true, -metadata.duplicate());
	//}

	auto metadata_delta = MetadataType();


	//for (auto iter = children.begin(); iter != children.end();) {
	for (unsigned int branch_bits = 0; branch_bits <= MAX_BRANCH_VALUE; branch_bits++) {
		TRIE_INFO("scanning branch bits %d", branch_bits);

		auto iter = children.find(branch_bits);
		if (iter == children.end()) {
			continue;
		}

		auto& child_ptr = (*iter).second;
		/*if (child_ptr->metadata.num_deleted_subnodes > 0 && child_ptr ->is_frozen) {
			children[branch_bits] = std::move(child_ptr->duplicate_for_writing());
			child_ptr = children[branch_bits];
		}*/


		auto result = child_ptr->do_rollback();
		update_metadata(branch_bits, result.second);
		if (result.first) {
			TRIE_INFO("deleting subtree");
			children.erase(branch_bits);
		} else {
			//auto child_sz = iter->second->size();
			if (child_ptr->single_child()) {
				TRIE_INFO("contracting size 1 subtree, prefix len %d", prefix_len);
				
				auto replacement_child_ptr = child_ptr->get_single_child();
				children.emplace((*iter).first, std::move(replacement_child_ptr));

				//auto&& replacement_child = std::move(
				//	(*child_ptr->children.begin()).second);
				//children[(*iter).first] = std::move(replacement_child);
				

				//children[iter->first].swap(replacement_child);
				//children.emplace(iter->first, replacement_child);
			} 
			//iter++;
		}
		metadata_delta += result.second;
	}
	TRIE_INFO("done scanning");
	return std::make_pair(children.empty(), metadata_delta);
}



//INVARIANT : this is not frozen
// will cause a write copy on no-op delete of nonexistent key, nbd
TEMPLATE_SIGNATURE
//First value is TRUE IFF parent should delete child
//next is "has anything been deleted"
std::tuple<bool, std::optional<ValueType>, MetadataType>
TrieNode<TEMPLATE_PARAMS>::perform_deletion(const prefix_t key) {
	
	//no lock needed bc root gets exclusive lock
	//std::lock_guard lock(*mtx);
	
	TRIE_INFO("deleting key %s",
		key.to_string(MAX_KEY_LEN_BITS).c_str());

	TRIE_INFO("deleting from current prefix %s (len %d bits)",
		prefix.to_string(prefix_len).c_str(),
		prefix_len.len);
	TRIE_INFO("num children: %d", children.size());

	//hash_buffer_contents_valid = false;

	auto prefix_match_len = get_prefix_match_len(key);

	if (prefix_match_len < prefix_len) {
		//incomplete match, which means that key doesn't exist.
		TRIE_INFO("key doesn't exist");
		return std::make_tuple(
			false, std::nullopt, MetadataType());
	}

	if (prefix_match_len == MAX_KEY_LEN_BITS) {
		TRIE_INFO("key deleted, removing");
		TRIE_INFO("metadata out:%s", (-metadata.unsafe_load()).to_string().c_str());

		return std::make_tuple(true, children.value(), -metadata.unsafe_load()); // safe bc exclusive lock on *this
	}

	auto branch_bits = get_branch_bits(key);
	auto iter = children.find(branch_bits);
	if (iter == children.end()) {
		//key isn't present, only 
		TRIE_INFO("no partial match, key must not exist");
		return std::make_tuple(false, std::nullopt, MetadataType());
	}

	auto& child_ptr = (*iter).second;

	if (!child_ptr) {
		throw std::runtime_error("perform_deletion found nullptr");
	}

	/*if (child_ptr->is_frozen) {
		children[branch_bits] = std::move(iter->second->duplicate_for_writing());
		child_ptr = children[branch_bits];
		//careful: this invalidates iter
	}*/

	auto [delete_child, deleted_obj, metadata_delta] 
		= child_ptr->perform_deletion(key);

	if (deleted_obj) {
		invalidate_hash();
	}

	TRIE_INFO("key deleted, delete_child=%d", delete_child);
	if (delete_child) {
		children.erase(branch_bits);
		//children.erase(iter);
	} else if (child_ptr->children.size() == 1) {
		TRIE_INFO("only one child, subsuming");
		TRIE_INFO_F(_log("pre subsuming:"));

		auto replacement_child_ptr = child_ptr -> get_single_child();//children.extract((*child_ptr->children.begin()).first);
		TRIE_INFO("extracted");
		children.emplace(branch_bits, std::move(replacement_child_ptr));

		//auto& replacement_child = 
		//	(*child_ptr->children.begin()).second;
		//children[branch_bits] = std::move(replacement_child);
		//children.emplace(iter->first, std::move(replacement_child));
	}
	update_metadata(branch_bits, metadata_delta);
	//TRIE_INFO_F(_log("post deletion:"));
	return std::make_tuple(false, deleted_obj, metadata_delta);
}

TEMPLATE_SIGNATURE
bool TrieNode<TEMPLATE_PARAMS>::contains_key(const prefix_t key) {
	
	[[maybe_unused]]
	auto lock = locks.template lock<std::shared_lock<std::shared_mutex>>();
	//std::shared_lock lock(*mtx);
	auto prefix_match_len = get_prefix_match_len(key);
	if (prefix_match_len < prefix_len) {
		return false;
	}
	if (prefix_match_len == MAX_KEY_LEN_BITS) {
		return true;
	}
	auto branch_bits = get_branch_bits(key);
	auto iter = children.find(branch_bits);
	if (iter == children.end()) {
		return false;
	}
	return (*iter).second->contains_key(key);
}


TEMPLATE_SIGNATURE
typename TrieNode<TEMPLATE_PARAMS>::trie_ptr_t
TrieNode<TEMPLATE_PARAMS>::endow_split(
	int64_t endow_threshold) {

	//no lock needed because root gets exclusive lock
	[[maybe_unused]]
	auto lock = locks.template lock<exclusive_lock_t>();

	//std::lock_guard lock(*mtx);

	if (children.empty()) {
		TRIE_INFO("hit leaf, returning null (endow_threshold < endowment, so no split");
		return nullptr;
	}

	if (endow_threshold >= metadata.endow) {
		throw std::runtime_error("shouldn't have reached this far down - entire node is consumed");
	}

	if (endow_threshold < 0) {
		throw std::runtime_error("endow threshold can't be negative");
	}

	int64_t acc_endow = 0;

	invalidate_hash();

	children_map_t new_node_children;

	for(unsigned char branch_bits = 0; branch_bits <= MAX_BRANCH_VALUE; branch_bits ++) {


		auto iter = children.find(branch_bits);
		if (iter != children.end()) {
			auto fully_consumed_subtree = acc_endow + (*iter).second->metadata.endow;

			if (fully_consumed_subtree <= endow_threshold) {
				//fully consume subnode

				update_metadata((*iter).first, -((*iter).second->metadata.unsafe_load())); // safe bc exclusive lock on *this, which owns the children
				auto new_child = children.extract((*iter).first);
				new_node_children.emplace((*iter).first, std::move(new_child));//std::move((*iter).second));
				//children.erase(iter);

			} else {

				if (!(*iter).second) {
					throw std::runtime_error("invalid iter in endow_split!");
				}
				auto split_obj = (*iter).second->endow_split(endow_threshold - acc_endow);
				if (split_obj) {
					update_metadata((*iter).first, -(split_obj->metadata.unsafe_load()));
					new_node_children.emplace((*iter).first, std::move(split_obj));
				}

				if ((*iter).second -> single_child()) {
					auto single_child = (*iter).second -> get_single_child();
					children.emplace((*iter).first, std::move(single_child));
					//children[(*iter).first] = std::move(single_child);
					//children.emplace(iter->first, std::move(single_child));
					if (!children.at((*iter).first)) {
						throw std::runtime_error("what the fuck children map");
					}
				}
			}
			acc_endow = fully_consumed_subtree;
		}

		if (acc_endow >= endow_threshold) {
			break;
		}
	}
	if (new_node_children.size()) {

		auto output = duplicate_node_with_new_children(std::move(new_node_children));
		output->compute_partial_metadata_unsafe(); // ok because output is threadlocal right now
		TRIE_INFO("current metadata:%s", output->metadata.to_string().c_str());
		//TRIE_INFO("returning exceeds %d split", split_idx);
		TRIE_INFO_F(output->_log("returned value:"));

		return output;
	}

	return nullptr;
}

/*
TEMPLATE_SIGNATURE
template<typename MetadataPredicate>
typename TrieNode<TEMPLATE_PARAMS>::trie_ptr_t
TrieNode<TEMPLATE_PARAMS>::metadata_split(
	typename MetadataPredicate::param_t split_parameter, TypeWrapper<MetadataPredicate> overload) {

	TRIE_INFO("Starting metadata_split to prefix %s (len %d bits), parameter %ld",
		__wrapper::__array_to_str(
			prefix, 
			__num_prefix_bytes(prefix_len)).c_str(), 
		prefix_len,
		split_parameter);
	TRIE_INFO("num children: %d", children.size());
	TRIE_INFO("metadata:%s", metadata.to_string().c_str());
	TRIE_INFO_F(_log("current subtree:    "));

	std::lock_guard lock(*mtx);

	if (children.empty()) {
		TRIE_INFO("Hit a leaf, returning null (param was less than smallest value, so no split)");
		return nullptr;
	}


	if (MetadataPredicate::param_at_most_meta(metadata, split_parameter)) {
		//entirety of node is consumed

		TRIE_ERROR("why am i here?  split_param=%d", split_parameter);
		TRIE_ERROR("metadata:%s", metadata.to_string().c_str());
		throw std::runtime_error("shouldn't have recursed this far down, exact match shouldn't happen");
	}

	if (!MetadataPredicate::param_exceeds_meta(MetadataType(), split_parameter)) {
		//return empty node
		TRIE_INFO("split_parameter is %d (compared with %d)", split_parameter, MetadataType().endow);
		throw std::runtime_error("shouldn't have recursed this far down, split_param is below 0");
	}


	unsigned char test_branch = 0;
	MetadataType meta{};

	while (!MetadataPredicate::param_exceeds_meta(meta, split_parameter)) {
		children_map_t new_node_children;

		auto iter = children.find(test_branch);
		if (iter != children.end()) {
			auto new_metadata = meta.duplicate();
			new_metadata += iter->second->get_metadata();


		}
	}




	unsigned char split_idx = MAX_BRANCH_VALUE;


	// iterate down from max to 0, asking whether everything less than current idx provides enough
	// param to satisfy predicate.
	// There are two cases:  Either a split gives an exact match or it doesn't.  Exact matches means we can cleanly split tree 
	// and do no further recursion.
	while (split_idx > 0) {
		//TRIE_INFO("%d %s", split_idx, partial_metadata[split_idx-1].to_string().c_str());

		//partial_metadata[split_idx] too high, [split_idx-1] too low (both strictly)
		if (MetadataPredicate::param_exceeds_meta(partial_metadata[split_idx - 1], split_parameter)) {
			TRIE_INFO("exceeds meta at split_idx %d", split_idx);
			children_map_t new_node_children;
			for (auto iter = children.begin(); iter != children.end();) {
				TRIE_INFO("partial_metadata[split_idx-1] %s", partial_metadata[split_idx-1].to_string().c_str());
				if (iter->first < split_idx) {
					update_metadata(iter->first, -(iter->second->metadata));
					split_parameter = MetadataPredicate::subtract_meta(iter->second->metadata, split_parameter);
					new_node_children.emplace(iter->first, std::move(iter->second));
					iter = children.erase(iter);
				} 
				else {
					if (iter -> first == split_idx) {
						auto recurse_param = MetadataPredicate::subtract_meta(partial_metadata[split_idx-1], split_parameter);
						TRIE_INFO("split_param %d recurse_param %d partial_metadata[split_idx-1] %s", 
							split_parameter, recurse_param, partial_metadata[split_idx-1].to_string().c_str());
						TRIE_INFO("recursing on %d", iter->first);

						auto& split_iter = iter->second;
						//if (split_iter -> is_frozen) {
						//	split_iter = iter->second->duplicate_for_writing();
						//}
						auto new_child = split_iter->metadata_split(recurse_param, overload);
						if (new_child) { //TODO ?
							update_metadata(iter->first, -(new_child->metadata));
							new_node_children.emplace(iter->first, std::move(new_child)); //ok bc new_child is returned and therefore not part of anything bigger
						}
					}
					++iter;
				}
			}

			if (new_node_children.size()) {
				//std::unique_ptr<TrieNode> output = std::make_unique<TrieNode>(
				//		std::move(new_node_children),
				//	prefix, 
				//	prefix_len,
				//	value);
				auto output = duplicate_node_with_new_children(std::move(new_node_children));
				output->compute_partial_metadata();
				TRIE_INFO("current metadata:%s", output->metadata.to_string().c_str());
				TRIE_INFO("returning exceeds %d split", split_idx);
				TRIE_INFO_F(output->_log("returned value:"));

				return output;
				//split on split_idx recursively, return
			}
			return nullptr;
		}
		else if (MetadataPredicate::param_at_most_meta(partial_metadata[split_idx - 1], split_parameter)) {
			//no recursion, split between split_idx and split_idx -1

			children_map_t new_node_children;
			for (auto iter = children.begin(); iter != children.end();) {
				if (iter->first < split_idx) {
					update_metadata(iter->first, -(iter->second->metadata));
					new_node_children.emplace(iter->first, std::move(iter->second));
					iter = children.erase(iter); // safe to delete bc children is mutable because this is not frozen.
				} else {
					++iter;
				}
			}
			if (new_node_children.size()) {

				auto output = duplicate_node_with_new_children(std::move(new_node_children));

				TRIE_INFO("current metadata:%s", output->metadata.to_string().c_str());
				output->compute_partial_metadata();
				TRIE_INFO("current metadata:%s", output->metadata.to_string().c_str());

				TRIE_INFO("returning meets %d split", split_idx);
				TRIE_INFO_F(output->_log("returned value:"));
				return output;
			}
			return nullptr;
		}
		split_idx--;
	}

	//recurse in the 0 branch_bits
	children_map_t new_node_children;

	auto iter = children.find(0);
	if (iter == children.end()) {
		throw std::runtime_error("metadata calculation error");
	}

	TRIE_INFO("recursing on 0");

	auto& child_0_ptr = iter->second;
	//if (child_0_ptr->is_frozen) {
	//	child_0_ptr = child_0_ptr->duplicate_for_writing();
	//}
	auto new_child = child_0_ptr->metadata_split(split_parameter, overload);

	if (new_child) {
		update_metadata(iter->first, -(new_child->metadata));
		new_node_children.emplace(iter->first, std::move(new_child));
		auto output = duplicate_node_with_new_children(std::move(new_node_children));
		output->compute_partial_metadata();

		TRIE_INFO("returning from 0 split");
		TRIE_INFO_F(output->_log("returned value:"));
		return output;
	}
	return nullptr;
}*/




TEMPLATE_SIGNATURE
template<typename VectorType>
VectorType 
_BaseTrie<TEMPLATE_PARAMS>::accumulate_values_parallel() const {
	VectorType output;
	accumulate_values_parallel(output);
	return output;
}


TEMPLATE_SIGNATURE
template<typename VectorType>
void 
_BaseTrie<TEMPLATE_PARAMS>::coroutine_accumulate_values_parallel(VectorType& output) const {

	std::lock_guard lock(*hash_modify_mtx);
	if (!root) {
		return;
	}

	try {
		output.resize(root -> size());
	} catch (...) {
		std::printf("failed to resize with size %lu\n", root -> size());
		throw;
	}

	AccumulateValuesRange<TrieT> range(root);

	tbb::parallel_for(
		range,
		[&output] (const auto& range) {
			auto vector_offset = range.vector_offset;
			CoroutineThrottler throttler(2);
			//std::printf("starting range with work_list sz %lu\n", range.work_list.size());

			for (size_t i = 0; i < range.work_list.size(); i++) {



				if (throttler.full()) {
					range.work_list[i]->coroutine_accumulate_values_parallel_worker(output, throttler, vector_offset);
				} else {
					throttler.spawn(spawn_coroutine_accumulate_values_parallel_worker(output, *range.work_list[i], throttler, vector_offset));
				}
				vector_offset += range.work_list[i]->size();
			}
			throttler.join();
		});
}

TEMPLATE_SIGNATURE
template<typename VectorType>
void 
_BaseTrie<TEMPLATE_PARAMS>::accumulate_values_parallel(VectorType& output) const {

	std::lock_guard lock(*hash_modify_mtx);

	//root -> _log("foo");

	//VectorType output;
	if (!root) {
		return;
	}

	//std::printf("reserving %lu\n", root -> size());
	try {
		output.resize(root -> size());
	} catch (...) {
		std::printf("failed to resize with size %lu\n", root -> size());
		throw;
	}

	AccumulateValuesRange<TrieT> range(root);

	//no fences necessary, because readonly
	tbb::parallel_for(
		range,
		[&output] (const auto& range) {

			//std::printf("starting range of size %lu, startin gfrom offset %lu\n", range.work_list.size(), range.vector_offset);
			auto vector_offset = range.vector_offset;
			for (size_t i = 0; i < range.work_list.size(); i++) {
				//std::printf("working on %p\n", range.work_list[i]);
				range.work_list[i]->accumulate_values_parallel_worker(output, vector_offset);
				//std::printf("size access\n");
				vector_offset += range.work_list[i]->size();
			}
			//std::printf("done\n");

		});
	//std::printf("done parallel_for\n");
}

template<typename VectorType, typename TrieT>
auto 
spawn_coroutine_accumulate_values_parallel_worker(VectorType& vec, TrieT& target, CoroutineThrottler& throttler, size_t offset) -> RootTask {
	//auto& target_dereferenced = co_await PrefetchAwaiter{target, throttler.scheduler};
	target.coroutine_accumulate_values_parallel_worker(vec, throttler, offset);
	co_return;
}


TEMPLATE_SIGNATURE
template<typename VectorType>
NoOpTask
TrieNode<TEMPLATE_PARAMS>::coroutine_accumulate_values_parallel_worker(VectorType& vec, CoroutineThrottler& throttler, size_t offset) {

	//std::printf("starting accumulate_values_parallel_worker\n");
	if (prefix_len == MAX_KEY_LEN_BITS) {
		//will cause problems if this we use custom accs, instead of just vecs
		auto& set_loc = co_await WriteAwaiter(&(vec[offset]), throttler.scheduler);
		set_loc = children.value();
		//vec[offset] = value;
		co_return;
	}
	for (auto iter = children.begin(); iter != children.end(); iter++) {

		auto& child = co_await ReadAwaiter{(*iter).second.get(), throttler.scheduler};
		size_t child_sz = child.size();

		if (!throttler.scheduler.full()) {
			throttler.spawn(spawn_coroutine_accumulate_values_parallel_worker(vec, child, throttler, offset));
		} else {
			child.coroutine_accumulate_values_parallel_worker(vec, throttler, offset);
		}
		offset += child_sz;
	}
	co_return;
}


TEMPLATE_SIGNATURE
template<typename VectorType>
void
TrieNode<TEMPLATE_PARAMS>::accumulate_values_parallel_worker(VectorType& vec, size_t offset) const {
	if (prefix_len == MAX_KEY_LEN_BITS) {
		//std::printf("inserting offset %lu account %lu (sz: %lu) \n", offset, value.owner, vec.size());
		if (offset > vec.size()) {
			throw std::runtime_error("invalid access");
		}
		vec[offset] = children.value();
		return;
	}
	for (unsigned int branch_bits = 0; branch_bits <= MAX_BRANCH_VALUE; branch_bits ++) {
		auto iter = children.find(branch_bits);
		if (iter != children.end()) {
			if (!(*iter).second) {
				_log("bad node");
				throw std::runtime_error("tried to accumulate value from empty node");
			}
			(*iter).second -> accumulate_values_parallel_worker(vec, offset);
			offset += (*iter).second -> size();
		}
	}
	//std::printf("done worker\n");
}

TEMPLATE_SIGNATURE
template<typename ApplyFn>
void
_BaseTrie<TEMPLATE_PARAMS>::parallel_apply(ApplyFn& fn) const {

	std::shared_lock lock(*hash_modify_mtx);

	ApplyRange<TrieT> range(root);

//	std::printf("starting parallel apply\n");

	//fences in case parallel_apply modifies stuff in the applyfn callback
//	std::atomic_thread_fence(std::memory_order_release);

	tbb::parallel_for(
		range,
		[&fn] (const auto& r) {
		//	std::atomic_thread_fence(std::memory_order_acquire);
			for (size_t i = 0; i < r.work_list.size(); i++) {
				r.work_list.at(i)->apply(fn);
			}
		//	std::atomic_thread_fence(std::memory_order_release);
		});
//	std::atomic_thread_fence(std::memory_order_acquire);
}

/*
TEMPLATE_SIGNATURE
template<typename ApplyFn>
void
_BaseTrie<TEMPLATE_PARAMS>::parallel_apply_threadlocal_acc(ApplyFn& fn) const {
	std::lock_guard lock(*hash_modify_mtx);

	ApplyRange<TrieT> range(root);

	tbb::parallel_for(
		range,
		[&fn] (const auto& r) {
			auto threadlocal = fn.new_threadlocal();
			for (unsigned int i = 0; i < r.work_list.size(); i++) {
				threadlocal(r.work_list[i]);
			}
			fn.finish_local(std::move(threadlocal));
		});
}*/

TEMPLATE_SIGNATURE
template<typename ValueModifyFn>
void
_BaseTrie<TEMPLATE_PARAMS>::parallel_batch_value_modify(ValueModifyFn& fn) const {

	std::lock_guard lock(*hash_modify_mtx);

	ApplyRange<TrieT> range(root);
	//guaranteed that range.work_list contains no overlaps

	//Fences are present because ValueModifyFn can modify various
	//memory locations in MemoryDatabase
	//std::atomic_thread_fence(std::memory_order_release);
	tbb::parallel_for(
		range,
		[&fn] (const auto& range) {
		//	std::atomic_thread_fence(std::memory_order_acquire);
			for (unsigned int i = 0; i < range.work_list.size(); i++) {
				fn(range.work_list[i]);
			}
		//	std::atomic_thread_fence(std::memory_order_release);
		});
	//std::atomic_thread_fence(std::memory_order_acquire);
}

//concurrent modification will cause problems here
TEMPLATE_SIGNATURE
TrieNode<TEMPLATE_PARAMS>*
TrieNode<TEMPLATE_PARAMS>::get_subnode_ref_nolocks(const prefix_t query_prefix, const PrefixLenBits query_len) {

	//auto lock = locks.template lock<exclusive_lock_t>();

//	std::printf("starting get_subnode_ref_nolocks at node %p prefix_len %u query_len %d\n", this, prefix_len, query_len);
	auto prefix_match_len = get_prefix_match_len(query_prefix, query_len);

	if (prefix_match_len == query_len) {
		return this;
	}

	if (prefix_match_len > prefix_len) {
		throw std::runtime_error("cannot happen!");
	}

	if (prefix_match_len < prefix_len) {
		return nullptr;
	}

	if (prefix_match_len == prefix_len) {
		auto bb = get_branch_bits(query_prefix);
		auto iter = children.find(bb);
		if (iter == children.end()) {
			throw std::runtime_error("can't recurse down nonexistent subtree!");
		}
		if (!(*iter).second) {
			_log("bad node: ");
			throw std::runtime_error("found a null iter in get_subnode_ref_nolocks");
		}

		auto child_candidate = (*iter).second -> get_subnode_ref_nolocks(query_prefix, query_len);
		if (child_candidate == nullptr) {
			return this;
		}
		return child_candidate;
	}
	throw std::runtime_error("invalid recursion");
}

TEMPLATE_SIGNATURE
template<typename ModifyFn>
void
TrieNode<TEMPLATE_PARAMS>::modify_value_nolocks(const prefix_t query_prefix, ModifyFn& fn) {
	
	invalidate_hash();

	//auto lock = locks.template lock<exclusive_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {
		fn(children.value());
		return;
	}

	auto prefix_match_len = get_prefix_match_len(query_prefix);
	if (prefix_match_len != prefix_len) {

		std::printf("my prefix: %s %d\n query: %s\n", prefix.to_string(prefix_len).c_str(), prefix_len.len,
			query_prefix.to_string(MAX_KEY_LEN_BITS).c_str());
		throw std::runtime_error("invalid recurison: value nonexistend");
	}

	auto bb = get_branch_bits(query_prefix);

	auto iter = children.find(bb);
	if (iter == children.end()) {
		throw std::runtime_error("branch bits not found: can't modify nonexistent value");
	}

	if (!(*iter). second) {
		throw std::runtime_error("modify_value_nolocks found a nullptr lying around");
	}

	(*iter).second -> modify_value_nolocks(query_prefix, fn);
}

TEMPLATE_SIGNATURE
template<typename ApplyFn>
void 
TrieNode<TEMPLATE_PARAMS>::apply(ApplyFn& func) {

	//[[maybe_unused]]
	//auto lock = locks.template lock<TrieNode::shared_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {
		if (children.size() != 0) {
			throw std::runtime_error("leaves have no children");
		}
		if (size() != 1) {
			_log("failed node: ");
			throw std::runtime_error("invalid size in apply");
		}
		func(children.value());
		return;
	}

	//if (prefix_len > MAX_KEY_LEN_BITS) {
	//	throw std::runtime_error("Can't have prefix_len bigger than max length");
	//}

	for (auto iter = children.begin(); iter != children.end(); iter++) {
		if (!(*iter).second) {
			throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
		}
		if ((*iter).first > MAX_BRANCH_VALUE) {
			throw std::runtime_error("invalid branch bits!");
		}
		(*iter).second->apply(func);
	}
}

template<typename ApplyFn, typename TrieT>
auto 
spawn_coroutine_apply(ApplyFn& func, TrieT* target, CoroutineThrottler& throttler) -> RootTask {

	//std::printf("spawning new coroutine! %p\n", target);

	auto& target_dereferenced = co_await PrefetchAwaiter{target, throttler.scheduler};

	//std::printf("woke up! %p\n", target);
	target_dereferenced.coroutine_apply(func, throttler);//.h_.resume();
	//std::printf("done apply\n");
	co_return;
}


/*
template<typename TrieT>
struct CoroutineTrieNodeWrapper {
	//TrieT* current_node;

	template<typename ApplyFn>
	auto coroutine_apply_task(ApplyFn& fn, TrieT* target, CoroutineThrottler& throttler) -> RootTask {


		auto coroutine_apply_lambda = [this] (ApplyFn& fn, TrieT* target, CoroutineThrottler& throttler, auto& recursion_callback) -> NoOpTask {

			auto& this_node = co_await PrefetchAwaiter{target, throttler.scheduler};

			if (this_node.prefix_len == TrieT::MAX_KEY_LEN_BITS) {
				fn(this_node.value);
				co_return;
			}




			for (auto iter = this_node.children.begin(); iter != this_node.children.end(); iter++) {
				if (!throttler.scheduler.full()) {

					auto new_task = coroutine_apply_task(fn, (*iter).second.get(), throttler);
					throttler.spawn(new_task);//spawn_coroutine_apply(fn, (*iter).second.get(), throttler));
				} else {
					recursion_callback(fn, (*iter).second.get(), throttler, recursion_callback);
				}
			}
			co_return;
		};

		coroutine_apply_lambda(fn, target, throttler, coroutine_apply_lambda);
		co_return;

	}


};*/


TEMPLATE_SIGNATURE
template<typename ApplyFn>
NoOpTask
TrieNode<TEMPLATE_PARAMS>::coroutine_apply(ApplyFn& func, CoroutineThrottler& throttler) {

	if (prefix_len == MAX_KEY_LEN_BITS) {
		func(children.value());
		co_return;
	}
	for (auto iter = children.begin(); iter != children.end(); iter++) {

		if (!throttler.scheduler.full()) {
			throttler.spawn(spawn_coroutine_apply(func, (*iter).second, throttler));
		} else {
			auto& child = co_await PrefetchAwaiter{(*iter).second, throttler.scheduler};
			child.coroutine_apply(func, throttler);
		}
	}

	co_return;
}

TEMPLATE_SIGNATURE
template<typename ApplyFn>
void 
TrieNode<TEMPLATE_PARAMS>::apply(ApplyFn& func) const {

	//[[maybe_unused]]
	//auto lock = locks.template lock<TrieNode::shared_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {
	/*	if (children.size() != 0) {
			throw std::runtime_error("leaves have no children");
		}
		if (size() != 1) {
			_log("failed node: ");
			throw std::runtime_error("invalid size in apply");
		}*/
		func(children.value());
		return;
	}

	//if (prefix_len > MAX_KEY_LEN_BITS) {
	//	throw std::runtime_error("Can't have prefix_len bigger than max length");
	//}

	for (auto iter = children.begin(); iter != children.end(); iter++) {
	//	if (!(*iter).second) {
	//		throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
	//	}
	//	if ((*iter).first > MAX_BRANCH_VALUE) {
	//		throw std::runtime_error("invalid branch bits!");
	//	}
		(*iter).second->apply(func);
	}
}


TEMPLATE_SIGNATURE
template<typename ApplyFn>
void TrieNode<TEMPLATE_PARAMS>::apply_geq_key(ApplyFn& func, const prefix_t min_apply_key) {
	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {

		if (prefix >= min_apply_key) {
//		if (memcmp(prefix, min_apply_key, KEY_LEN_BYTES) >= 0) {
			func(prefix, children.value());
			if (children.size() != 0) {
				throw std::runtime_error("what the fuck");
			}
		}
		return;	
	}

	prefix_t min_apply_key_truncated = min_apply_key;
//	memcpy(min_apply_key_truncated, min_apply_key, KEY_LEN_BYTES);

	truncate_prefix(min_apply_key_truncated, prefix_len, MAX_KEY_LEN_BITS);

	//auto prev_diff_res = memcmp(prefix, min_apply_key_truncated, KEY_LEN_BYTES);

	if (prefix > min_apply_key_truncated) {
	//if (prev_diff_res > 0) {
		if (USE_LOCKS) {
			//can't acquire lock twice
			for (auto iter = children.begin(); iter != children.end(); iter++) {
				(*iter).second->apply(func);
			}
		} else {
			apply(children.value());
		}
		return;
	}

	if (prefix < min_apply_key_truncated) {
//	if (prev_diff_res < 0) {
		
		//is possible, not a problem
		//throw std::runtime_error("should be impossible this time");
		//std::printf("%s too low, ignoring\n", DebugUtils::__array_to_str(prefix, __num_prefix_bytes(prefix_len)).c_str());
		return;
	}


	auto min_branch_bits = get_branch_bits(min_apply_key);

	for (auto iter = children.begin(); iter != children.end(); iter++) {
		if ((*iter).first == min_branch_bits) {
			if (!(*iter).second) {
				throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
			}

			(*iter).second->apply_geq_key(func, min_apply_key);
		} else if ((*iter).first > min_branch_bits) {
			if (!(*iter).second) {
				throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
			}
			(*iter).second -> apply(func);
		}
	}
}


TEMPLATE_SIGNATURE
template<typename ApplyFn>
void TrieNode<TEMPLATE_PARAMS>::apply_lt_key(ApplyFn& func, const prefix_t threshold_key) {
	//std::shared_lock lock(*mtx);

	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {
		if (prefix < threshold_key) {
//		if (memcmp(prefix.data(), threshold_key.data(), KEY_LEN_BYTES) < 0) {
			func(children.value());
			if (children.size() != 0) {
				throw std::runtime_error("what the fuck");
			}
		}
		return;	
	}

	prefix_t threshold_key_truncated;
	threshold_key_truncated = threshold_key;
	//memcpy(threshold_key_truncated, threshold_key, KEY_LEN_BYTES);

	truncate_prefix(threshold_key_truncated, prefix_len, MAX_KEY_LEN_BITS);

	//auto prev_diff_res = memcmp(prefix.data(), threshold_key_truncated.data(), KEY_LEN_BYTES);

	if (prefix < threshold_key_truncated) {
//	if (prev_diff_res < 0) {
		if (USE_LOCKS) {
			//can't acquire lock twice
			for (auto iter = children.begin(); iter != children.end(); iter++) {
				(*iter).second->apply(func);
			}
		} else {
			apply(func);
		}
		return;
	}

	if (prefix > threshold_key_truncated) {
	//if (prev_diff_res > 0) {
		
		//is possible, not a problem
		//throw std::runtime_error("should be impossible this time");
		//std::printf("%s too low, ignoring\n", DebugUtils::__array_to_str(prefix, __num_prefix_bytes(prefix_len)).c_str());
		return;
	}


	auto min_branch_bits = get_branch_bits(threshold_key);

	for (auto iter = children.begin(); iter != children.end(); iter++) {
		if ((*iter).first == min_branch_bits) {
			if (!(*iter).second) {
				throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
			}

			(*iter).second->apply_lt_key(func, threshold_key);
		} else if ((*iter).first < min_branch_bits) {
			if (!(*iter).second) {
				throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
			}
			(*iter).second -> apply(func);
		}
	}
}

TEMPLATE_SIGNATURE
template<bool x>
typename std::enable_if<x, MetadataType>::type
TrieNode<TEMPLATE_PARAMS>::mark_subtree_for_deletion() {

	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {
		if (children.size() != 0) {
			throw std::runtime_error("leaves have no children");
		}
		auto swap_lambda = [] (AtomicDeletableMixin& object) {
			AtomicDeletableMixin::BaseT::value_t expect = 0, desired = 1;
			return AtomicDeletableMixin::compare_exchange(
				object, expect, desired);
		};
		bool res = swap_lambda(metadata);
		//swaps 0 to 1 if it's 0, otherwise it's 1 already.
		auto meta_out = MetadataType{};
		meta_out.num_deleted_subnodes = res?1:0;
		if (!res) {
			return meta_out;
			//hash not invalidated because nothing deleted
		} else {
			invalidate_hash();
			return meta_out;

		}
	}

	MetadataType meta_delta{};

	for (auto iter = children.begin(); iter != children.end(); iter++) {
		if (!(*iter).second) {
			throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
		}
		auto local_delta = (*iter).second->mark_subtree_for_deletion();
		update_metadata((*iter).first, local_delta);
		meta_delta += local_delta;
	}
	if (meta_delta.num_deleted_subnodes != 0) {
		invalidate_hash();
	}
	return meta_delta;
}



//strictly below max_key is deleted
TEMPLATE_SIGNATURE
template<bool x>
typename std::enable_if<x, MetadataType>::type
TrieNode<TEMPLATE_PARAMS>::mark_subtree_lt_key_for_deletion(const prefix_t max_key) {

	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();

	//std::printf("starting mark_subtree_lt key=%s\n", __wrapper::__array_to_str(max_key.data(), max_key.size()).c_str());

	if (prefix_len == MAX_KEY_LEN_BITS) {
		if (prefix < max_key) {
//		if (memcmp(prefix.data(), max_key.data(), KEY_LEN_BYTES) < 0) {
			if (children.size() != 0) {
				throw std::runtime_error("what the fuck");
			}

			auto swap_lambda = [] (AtomicDeletableMixin& object) {
				AtomicDeletableMixin::BaseT::value_t expect = 0, desired = 1;
				return AtomicDeletableMixin::compare_exchange(
					object, expect, desired);
			};
			bool res = swap_lambda(metadata);
			//swaps 0 to 1 if it's 0, otherwise it's 1 already.
			auto meta_out = MetadataType();
			meta_out.num_deleted_subnodes = res?1:0;
			if (!res) {
				return meta_out;
				//hash not invalidated because nothing deleted
			} else {
				invalidate_hash();
				return meta_out;

			}
		}
		return MetadataType{};
	}

	prefix_t max_key_truncated = max_key;
	//memcpy(max_key_truncated, max_key, KEY_LEN_BYTES);

	truncate_prefix(max_key_truncated, prefix_len, MAX_KEY_LEN_BITS);
	//std::printf("truncated prefix to %s (len %lu)\n", __wrapper::__array_to_str(max_key_truncated.data(), max_key_truncated.size()).c_str(), prefix_len);

	//auto prev_diff_res = memcmp(prefix.data(), max_key_truncated.data(), KEY_LEN_BYTES);

	if (prefix < max_key_truncated) {
	//if (prev_diff_res < 0) {
		if (USE_LOCKS) {
			auto metadata_out = MetadataType{};
			//can't acquire lock twice
			for (auto iter = children.begin(); iter != children.end(); iter++) {
				auto local_delta = (*iter).second->mark_subtree_for_deletion();
				update_metadata((*iter).first, local_delta);
				metadata_out += local_delta;
			}
			return metadata_out;
		} else {
			return mark_subtree_for_deletion();
		}
	} else if (prefix > max_key_truncated) {
//	if (prev_diff_res > 0) {
		return MetadataType{};
		//is possible, not a problem
		//throw std::runtime_error("mark lt key should be impossible this time");
		//std::printf("%s too high, ignoring\n", DebugUtils::__array_to_str(prefix, __num_prefix_bytes(prefix_len)).c_str());
	}


	auto max_branch_bits = get_branch_bits(max_key);
	auto meta_delta = MetadataType{};
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		if ((*iter).first == max_branch_bits) {
			if (!(*iter).second) {
				throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
			}

			auto local_delta = (*iter).second->mark_subtree_lt_key_for_deletion(max_key);
			update_metadata((*iter).first, local_delta);
			meta_delta += local_delta;
		} else  if ((*iter).first < max_branch_bits) {
			if (!(*iter).second) {
				throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
			}
			auto local_delta = (*iter).second -> mark_subtree_for_deletion();
			update_metadata((*iter).first, local_delta);
			meta_delta += local_delta;

		}
	}
	if (meta_delta.num_deleted_subnodes != 0) {
		invalidate_hash();
	}
	return meta_delta;
}

TEMPLATE_SIGNATURE
int64_t 
TrieNode<TEMPLATE_PARAMS>::endow_lt_key(const prefix_t max_key) const {
	
	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {
		if (prefix < max_key) {
//		if (memcmp(prefix.data(), max_key.data(), KEY_LEN_BYTES) < 0) {
			if (children.size() != 0) {
				throw std::runtime_error("what the fuck");
			}
			return metadata.unsafe_load().endow;
			
		}
		return 0;
	}

	prefix_t max_key_truncated = max_key;
	//memcpy(max_key_truncated, max_key, KEY_LEN_BYTES);

	truncate_prefix(max_key_truncated, prefix_len, MAX_KEY_LEN_BITS);

	//auto prev_diff_res = memcmp(prefix.data(), max_key_truncated.data(), KEY_LEN_BYTES);

	if (prefix < max_key_truncated) {
//	if (prev_diff_res < 0) {
		return metadata.unsafe_load().endow;
	}
	else if (prefix > max_key_truncated) {
		return 0;
	}
//	if (prev_diff_res > 0) {
//		
//		return 0;
//		
		//actually is possible, is not a problem.
		//throw std::runtime_error("should be impossible this time");
		//std::printf("%s too low, ignoring\n", DebugUtils::__array_to_str(prefix, __num_prefix_bytes(prefix_len)).c_str());
//	}


	auto max_branch_bits = get_branch_bits(max_key);
	int64_t valid_endow = 0;
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		if ((*iter).first == max_branch_bits) {
			if (!(*iter).second) {
				throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
			}

			valid_endow += (*iter).second->endow_lt_key(max_key);
		} else  if ((*iter).first < max_branch_bits) {
			if (!(*iter).second) {
				throw std::runtime_error("null pointer len = " + std::to_string(prefix_len.len));
			}
			valid_endow += (*iter).second -> get_metadata_unsafe().endow;
		}
	}
	return valid_endow;
}

TEMPLATE_SIGNATURE
std::optional<typename TrieNode<TEMPLATE_PARAMS>::prefix_t>
TrieNode<TEMPLATE_PARAMS>::get_lowest_key() {
	//std::shared_lock lock(*mtx);
	
	[[maybe_unused]]
	auto lock = locks.template lock<shared_lock_t>();

	if (prefix_len == MAX_KEY_LEN_BITS) {
		//unsigned char* output = new unsigned char[KEY_LEN_BYTES];
		//memcpy(output, prefix, KEY_LEN_BYTES);
		return prefix;
	}
	for (unsigned char i = 0; i <= MAX_BRANCH_VALUE; i++) {
		auto iter = children.find(i);
		if (iter != children.end()) {
			if (!(*iter).second) {
				throw std::runtime_error("what the fuck!");
			}
			return (*iter).second->get_lowest_key();
		}
	}
	return std::nullopt; // empty trie
}

/*
TEMPLATE_SIGNATURE
typename TrieNode<TEMPLATE_PARAMS>::trie_ptr_t 
TrieNode<TEMPLATE_PARAMS>::deep_copy() {
	std::lock_guard lock(*mtx);
	auto output = duplicate_node_only_unsafe(); //safe bc exclusive lock on this

	for (auto iter = children.begin(); iter != children.end(); iter++) {
		output->children[iter->first] = std::move(iter->second->deep_copy());
	}
	return output;
}

TEMPLATE_SIGNATURE
void
TrieNode<TEMPLATE_PARAMS>::deep_freeze() {
	std::lock_guard lock(*mtx);

	//if (is_frozen) return;
	//is_frozen = true;
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		iter->second->deep_freeze();
	}
}*/

TEMPLATE_SIGNATURE
template<bool x>
std::optional<ValueType> 
TrieNode<TEMPLATE_PARAMS>::get_value(typename std::enable_if<x, const prefix_t&>::type query_key) {
	
	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t> ();
	//std::shared_lock lock(*mtx);

	if (prefix_len == MAX_KEY_LEN_BITS) {
		return std::make_optional(children.value());
	}

	auto branch_bits = get_branch_bits(query_key);

	auto iter = children.find(branch_bits);
	if (iter == children.end()) {
		return std::nullopt;
	}

	return (*iter).second->get_value(query_key);
}


TEMPLATE_SIGNATURE
template<typename VectorType>
void
TrieNode<TEMPLATE_PARAMS>::accumulate_values(VectorType& output) const {	
	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();
	if (prefix_len == MAX_KEY_LEN_BITS) {
		output.push_back(children.value());
		return;
	}

	if (size() < 0 || size() > 1000000000) {
		//sanity check, TODO remove.
		std::printf("size=%lu\n", size());
		_log("trie: ");
		throw std::runtime_error("invalid size!!!");
	}



	for (unsigned bb = 0; bb <= MAX_BRANCH_VALUE; bb++) {
		auto iter = children.find(bb);
		if (iter != children.end()) {
			(*iter).second -> accumulate_values(output);
		}
	}
}

TEMPLATE_SIGNATURE
template<typename VectorType>
void
TrieNode<TEMPLATE_PARAMS>::accumulate_keys(VectorType& output) {
	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();
	if (prefix_len == MAX_KEY_LEN_BITS) {
		output.add_key(prefix);
		return;
	}



	for (unsigned bb = 0; bb <= MAX_BRANCH_VALUE; bb++) {
		auto iter = children.find(bb);
		if (iter != children.end()) {
			(*iter).second -> accumulate_keys(output);
		}
	}
}

/*
TEMPLATE_SIGNATURE
const trie_ptr_t& 
TrieNode<TEMPLATE_PARAMS>::get_node(const unsigned char* query, const int query_len) {
	[[maybe_unused]]
	auto lock = locks.template lock<TrieNode::shared_lock_t>();
	auto match_len = get_prefix_match_len(query, query_len);

	if (match_len < query_len) {
		return null_node;
	}

	if (match_len > que)
}*/



#undef TEMPLATE_PARAMS
#undef TEMPLATE_SIGNATURE
}
