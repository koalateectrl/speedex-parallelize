#include "merkle_trie.h"
#include <cstring>
#include <execution>


namespace edce {
/*
template <unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
unsigned char TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>::get_branch_bits(unsigned char* data) {
	unsigned char branch_bits = data[prefix_len/8] 
			& (BRANCH_MASK>>(prefix_len % 8));
	branch_bits>>=(8-BRANCH_BITS-(prefix_len % 8));
	return branch_bits;
}

template<unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
int TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>::size() {
		if (prefix_len == 8 * KEY_LEN) {
			return 1;
		} 
		int sz = 0;
		for (auto iter = children.begin(); iter != children.end(); iter++) {
			sz += iter->second->size();
		}
		return sz;
	}
*/	

/*

NODE:
prefix len (in blocks of BRANCH_BITS bits) 1byte
marginal prefix
for each child:
	child prefix, hash of child

ROOT:
number of children [4bytes]
hash of root node

We're keeping prefix bits byte aligned.

so if we split a say 1111 into 11 and 11,
then the first prefix is written as 1100 and the next is as 
0011.


*/
	/*
template<unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
void TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>::compute_hash(int prefix_dropped_bytes) {
	if (hash_computed) {
		return;
	}
	int num_bytes;
	unsigned char* digest_bytes;
	if (children.empty()) {
		//leaf node
		int num_prefix_bytes = (prefix_len / 8) - prefix_dropped_bytes;
		num_bytes = num_prefix_bytes + 1;

		digest_bytes = new unsigned char[num_bytes];
		digest_bytes[0] = prefix_len / BRANCH_BITS;
		memcpy(&digest_bytes[1], 
			&prefix[prefix_dropped_bytes], 
			num_prefix_bytes);
	} else {
		int num_prefix_bytes = (prefix_len / 8) - prefix_dropped_bytes;
		num_bytes = num_prefix_bytes + 1 + (children.size() * 33);

		digest_bytes = new unsigned char[num_bytes];
		digest_bytes[0] = prefix_len / BRANCH_BITS;
		memcpy(&digest_bytes[1], 
			&prefix[prefix_dropped_bytes], 
			num_prefix_bytes);

		int cur_write_loc = 1 + num_prefix_bytes;
		for (unsigned char i = 0; i <= MAX_BRANCH_VALUE; i++) {
			auto iter = children.find(i);
			if (iter != children.end()) {
				digest_bytes[cur_write_loc] = i;
				iter->second->compute_hash(prefix_len/8);
				iter->second->copy_hash_to_buf(
					&(digest_bytes[cur_write_loc+1]));
				cur_write_loc += 33;
			}
		}
	}
	std::unique_lock<std::mutex> lock(*mtx);
	SHA256(digest_bytes, num_bytes, hash);
	hash_computed = true;
	delete[] digest_bytes;

}

//assumes max block size is at most 2^32

template<unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
void BlockTrie<KEY_LEN, VALUE_LEN, BRANCH_BITS>::get_hash(unsigned char* out) {
	uint32_t num_children = root.size();
	int buf_size = 4 + 32;
	unsigned char buf[36];//buf_size
	memcpy(buf, (unsigned char*) &num_children, 4);
	//buf[0] = (unsigned char) (num_children >> 24);
	//buf[1] = (unsigned char) (num_children >> 16);
	//buf[2] = (unsigned char) (num_children >> 8);
	//buf[3] = (unsigned char) (num_children);
	
	//memcpy(buf, static_cast<unsigned char*>(&num_children), 4);
	root.copy_hash_to_buf(&(buf[4]));
	//memcpy(&(buf[4]), root.get_hash(0), 32);
	SHA256(buf, buf_size, out);
}

template<unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
void TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>::insert(prefix_t data) {
	INFO("Starting insert");
	INFO("my prefix length is %d", prefix_len);
	INFO("num children: %d", children.size());
	std::unique_lock<std::mutex> lock(*mtx);
	int prefix_match_len = get_prefix_match_len(data);
	INFO("prefix match len is %d", prefix_match_len);
	if (prefix_len == 0 && children.empty()) {
		INFO("node is empty, no children");
		//initial node
		memcpy(prefix, data, KEY_LEN);
		prefix_len = 8 * KEY_LEN;
		return;
	}
	else if (prefix_match_len >= prefix_len) {
		INFO("full prefix match, recursing");
		auto branch_bits = get_branch_bits(data);
		auto iter = children.find(branch_bits);
		if (iter != children.end()) {
			INFO("found previous child");
			auto& child = children[branch_bits];
			lock.unlock();
			child->insert(data);
			return;
		} else {
			INFO("make new leaf");
			auto new_child = std::make_unique<TrieNode>(&(data[0]));
			children[branch_bits] = std::move(new_child);
			return;
		}

	} else {
		INFO("i don't extend current prefix, doing a break after %d", 
			prefix_match_len);
		auto original_child_branch = std::make_unique<TrieNode>(
			std::move(children), prefix, prefix_len);
		auto new_child = std::make_unique<TrieNode>(
			data);
		children.clear();
		prefix_len = prefix_match_len;
		auto branch_bits = get_branch_bits(data);
		children[branch_bits] = std::move(new_child);
		auto old_branch_bits = get_branch_bits(prefix);
		children[old_branch_bits] = std::move(original_child_branch);

		prefix[prefix_len/8] &= (0xFF << (8-(prefix_len % 8)));
		int zero_start = 1 + (prefix_len/8);
		memset(&(prefix[zero_start]), 0, KEY_LEN-zero_start);
		//for (int i = (prefix_len/8) + 1; i < _MAX_PREFIX_LEN; i++) {
		//	prefix[i] = 0;
		//}
		return;
	}
}

template<unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
bool TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>::check_prefix(prefix_t other) {
	int num_bytes = (prefix_len)/8;
	int res = memcmp(prefix, other, num_bytes);
	int remainder = (prefix_len) % 8;
	if (res!= 0) {
		return false;
	}
	char mask = (0xFF)<< (8-remainder);
	return (other[num_bytes]&mask) == (prefix[num_bytes] & mask);
}

template<unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
int TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>::get_prefix_match_len(prefix_t other) {
	int res = 8 * KEY_LEN;
	for (int i = 0; i < KEY_LEN; i++) {
		unsigned char byte_dif = other[i]^ prefix[i];
		if (byte_dif!=0) {
			res = (i * 8) + (__builtin_clz(byte_dif)-24);
			break;
		}
	}
	res -= res % BRANCH_BITS;
	return std::min(res, prefix_len);
}

template<unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
void TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>::accumulate_nodes(
	std::vector<std::pair<int, std::reference_wrapper<TrieNode>>>& nodes) {
	nodes.push_back(std::make_pair(prefix_len/8, std::ref(*this)));
	for (auto iter = children.begin(); iter != children.end(); iter++) {
		iter->second->accumulate_nodes(nodes);
	}
}

template<unsigned int KEY_LEN, unsigned int VALUE_LEN, unsigned int BRANCH_BITS>
void BlockTrie<KEY_LEN, VALUE_LEN, BRANCH_BITS>::parallel_get_hash(unsigned char* buffer) {
	std::vector<std::pair<int, std::reference_wrapper<TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>>>> nodes;
	root.accumulate_nodes(nodes);

	//precompute all the node hashes
	std::for_each(
		std::execution::par_unseq,
		nodes.begin(),
		nodes.end(),
		[] (std::pair<int, std::reference_wrapper<TrieNode<KEY_LEN, VALUE_LEN, BRANCH_BITS>>> pair) {
			pair.second.get().compute_hash(pair.first);
		});

	get_hash(buffer);

}
*/
}