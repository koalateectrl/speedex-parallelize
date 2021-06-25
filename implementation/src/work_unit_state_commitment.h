#pragma once

#include "fixed_point_value.h"
#include "price_utils.h"

#include <vector>
#include <memory>
#include <mutex>

#include "xdr/block.h"

namespace edce {

struct SingleValidationStatistics {
	FractionalAsset activated_supply;

	SingleValidationStatistics& operator+=(const SingleValidationStatistics& other);
};

struct ValidationStatistics {
	std::vector<SingleValidationStatistics> stats;

	SingleValidationStatistics& operator[](std::size_t idx);
	SingleValidationStatistics& at(size_t idx);
	ValidationStatistics& operator+=(const ValidationStatistics& other);
	void make_minimum_size(std::size_t sz);

	void log() const;

	std::size_t size() const;
};

struct ThreadsafeValidationStatistics : public ValidationStatistics {
	std::unique_ptr<std::mutex> mtx;
	ThreadsafeValidationStatistics(std::size_t minimum_size) : ValidationStatistics(), mtx(std::make_unique<std::mutex>()) {
		make_minimum_size(minimum_size);
	}

	ThreadsafeValidationStatistics& operator+=(const ValidationStatistics& other);
	void log();
};

struct SingleWorkUnitStateCommitmentChecker : public SingleWorkUnitStateCommitment {

	SingleWorkUnitStateCommitmentChecker(const SingleWorkUnitStateCommitment& internal) 
		: SingleWorkUnitStateCommitment(internal) {}

	FractionalAsset fractionalSupplyActivated() const;

	FractionalAsset partialExecOfferActivationAmount() const;

	bool check_sanity() const;
};

struct WorkUnitStateCommitmentChecker {

	const std::vector<SingleWorkUnitStateCommitmentChecker> commitments;
	const std::vector<Price> prices;
	const uint8_t tax_rate;

	WorkUnitStateCommitmentChecker(
		const WorkUnitStateCommitment& internal, 
		const std::vector<Price> prices,
		const uint8_t tax_rate) 
	: commitments(internal.begin(), internal.end())
	, prices(prices)
	, tax_rate(tax_rate) {}

	const SingleWorkUnitStateCommitmentChecker& operator[](std::size_t idx) const {
		return commitments[idx];
	}

	const SingleWorkUnitStateCommitmentChecker& at(size_t idx) const {
		return commitments.at(idx);
	}

	void log() const;
	bool check(ThreadsafeValidationStatistics& fully_cleared_stats);

	size_t size() const {
		return commitments.size();
	}
};

}
