#include "work_unit_state_commitment.h"

namespace edce {

SingleValidationStatistics& 
SingleValidationStatistics::operator+=(const SingleValidationStatistics& other) {
	activated_supply += other.activated_supply;
	return *this;
}

SingleValidationStatistics& 
ValidationStatistics::operator[](std::size_t idx) {
	return stats[idx];
}

SingleValidationStatistics& 
ValidationStatistics::at(size_t idx) {
	return stats.at(idx);
}

ValidationStatistics& 
ValidationStatistics::operator+=(const ValidationStatistics& other) {
	while (stats.size() < other.stats.size()) {
		stats.emplace_back();
	}


	for (std::size_t i = 0; i < other.stats.size(); i++) {
		stats.at(i) += other.stats.at(i);
	}
	return *this;
}

void 
ValidationStatistics::make_minimum_size(std::size_t sz) {
	while (stats.size() <= sz) {
		stats.emplace_back();
	}
	INFO("boosted size to %lu", stats.size());

}

void 
ValidationStatistics::log() const {
	for (std::size_t i = 0; i < stats.size(); i++) {
		std::printf("%lf ", stats[i].activated_supply.to_double());
	}
	std::printf("\n");
}

std::size_t
ValidationStatistics::size() const {
	return stats.size();
}

ThreadsafeValidationStatistics& 
ThreadsafeValidationStatistics::operator+=(const ValidationStatistics& other) {
	std::lock_guard lock(*mtx);
	ValidationStatistics::operator+=(other);
	return *this;
}

void 
ThreadsafeValidationStatistics::log() {
	std::lock_guard lock(*mtx);
	ValidationStatistics::log();
}

FractionalAsset 
SingleWorkUnitStateCommitmentChecker::fractionalSupplyActivated() const {
	FractionalAsset::value_t value;
	PriceUtils::read_unsigned_big_endian(SingleWorkUnitStateCommitment::fractionalSupplyActivated, value);
	return FractionalAsset::from_raw(value);
}

FractionalAsset 
SingleWorkUnitStateCommitmentChecker::partialExecOfferActivationAmount() const {
	FractionalAsset::value_t value;
	PriceUtils::read_unsigned_big_endian(SingleWorkUnitStateCommitment::partialExecOfferActivationAmount, value);
	return FractionalAsset::from_raw(value);
}

bool 
SingleWorkUnitStateCommitmentChecker::check_sanity() const {
	OfferKeyType zero_key;
	zero_key.fill(0);

	static_assert(sizeof(zero_key) == sizeof(SingleWorkUnitStateCommitment::partialExecThresholdKey), "size mismatch");
	if (memcmp(zero_key.data(), SingleWorkUnitStateCommitment::partialExecThresholdKey.data(), zero_key.size()) != 0) {
	//	std::printf("threshold key not null\n");
	//	std::printf("%ld\n", SingleWorkUnitStateCommitment::thresholdKeyIsNull);
		return SingleWorkUnitStateCommitment::thresholdKeyIsNull == 0;
	}
	//std::printf("threshold key is null\n");
	//std::printf("%ld\n", SingleWorkUnitStateCommitment::thresholdKeyIsNull);
	return SingleWorkUnitStateCommitment::thresholdKeyIsNull == 1;
}

void 
WorkUnitStateCommitmentChecker::log() const {
	std::printf("fractionalSupplyActivated\n");
	for (std::size_t i = 0; i < commitments.size(); i++) {
		std::printf("%lf ", commitments[i].fractionalSupplyActivated().to_double());
	}
	std::printf("\npartialExecOfferActivationAmount\n");

	for (std::size_t i = 0; i < commitments.size(); i++) {
		std::printf("%lf ", commitments[i].partialExecOfferActivationAmount().to_double());
	}
	std::printf("\n");
}

bool
WorkUnitStateCommitmentChecker::check(ThreadsafeValidationStatistics& fully_cleared_stats) {
	fully_cleared_stats.make_minimum_size(commitments.size());
	for (std::size_t i = 0; i < commitments.size(); i++) {
		INFO("%lf %lf %lf", fully_cleared_stats[i].activated_supply.to_double(), commitments.at(i).partialExecOfferActivationAmount().to_double(),
			commitments[i].fractionalSupplyActivated().to_double());
		if (fully_cleared_stats.at(i).activated_supply + commitments.at(i).partialExecOfferActivationAmount() != 
			commitments.at(i).fractionalSupplyActivated()) {
			std::printf("%lu additive mismatch: computed %lf + %lf, expected %lf\n", i,
				fully_cleared_stats[i].activated_supply.to_double(), commitments[i].partialExecOfferActivationAmount().to_double(), 
				commitments[i].fractionalSupplyActivated().to_double());
			return false;
		}
		if (!commitments.at(i).check_sanity()) {
			std::printf("insanity\n");
			return false;
		}
	}
	return true;
}

} /* edce */
