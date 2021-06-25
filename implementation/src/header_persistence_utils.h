#pragma once

#include "xdr/block.h"

namespace edce {

std::string header_filename(const uint64_t round_number);

bool check_if_header_exists(const uint64_t round_number);

HashedBlock load_header(const uint64_t round_number);

void save_header(const HashedBlock& header);
}
