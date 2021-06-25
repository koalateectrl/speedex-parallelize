#pragma once
#include <cstdint>

#include "xdr/types.h"
#include "merkle_trie_utils.h"

namespace edce {

//This is a dupliation of SizeMixin.  Kinda gross to do the duplication, but not sure what else to do
struct AtomicWorkUnitMetadata;

struct WorkUnitMetadata {

	using AtomicT = AtomicWorkUnitMetadata;
	int64_t endow;

	WorkUnitMetadata(const Offer& offer) : endow(offer.amount) {}

	WorkUnitMetadata() : endow(0) {}

	WorkUnitMetadata(const WorkUnitMetadata& v) : endow(v.endow) {}


	WorkUnitMetadata& operator+=(const WorkUnitMetadata& other) {
		endow += other.endow;
		return *this;
	}
	WorkUnitMetadata& operator-=(const WorkUnitMetadata& other) {
		endow -= other.endow;
		return *this;
	}

	bool operator==(const WorkUnitMetadata& other) {
		return endow == other.endow;
	}

	std::string to_string() const {
		std::stringstream s;
		s << "endow:"<<endow<<" ";
		return s.str();
	}

	template<typename AtomicType>
	void unsafe_load_from(const AtomicType& s) {
		endow = s.endow.load(load_order);
	}
};



struct AtomicWorkUnitMetadata {
	std::atomic_int64_t endow;

	AtomicWorkUnitMetadata(const Offer& offer) : endow(offer.amount) {}

	AtomicWorkUnitMetadata() : endow(0) {}

	AtomicWorkUnitMetadata(const WorkUnitMetadata& v) : endow(v.endow) {}


	void operator+= (const WorkUnitMetadata& other) {
		endow.fetch_add(other.endow, store_order);
	}

	void operator-= (const WorkUnitMetadata& other) {
		endow.fetch_sub(other.endow, store_order);
	}

	bool operator== (const WorkUnitMetadata& other) {
		return endow.load(load_order) == other.endow;
	}
	void clear() {
		endow = 0;
	}

	void unsafe_store(const WorkUnitMetadata& other) {
		endow.store(other.endow, store_order);
	}

	std::string to_string() const {
		std::stringstream s;
		s << "endow:"<<endow<<" ";
		return s.str();
	}

};
} /* edce */
