#pragma once

#include "xdr/types.h"
#include "xdr/block.h"

#include <sodium.h>
#include <array>

#include "edce_management_structures.h"

namespace edce {

class SamBlockSignatureChecker {

    EdceManagementStructures& management_structures;

public:
    SamBlockSignatureChecker(EdceManagementStructures& management_structures) 
    : management_structures(management_structures) {
        if (sodium_init() == -1) {
            throw std::runtime_error("could not init sodium");
        }
    }

    bool check_all_sigs(const SerializedBlock& block);
}

}
