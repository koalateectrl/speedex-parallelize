
#if defined(XDRC_HH) || defined(XDRC_SERVER)
%#include "xdr/block.h"
%#include "xdr/experiments.h"
%#include "crypto_utils.h"
#endif

#if defined(XDRC_PXDI)
%from types_xdr cimport *
%from block_xdr cimport *
%from experiments_xdr cimport *
#endif

#if defined(XDRC_PXD)
%from types_xdr cimport *
%from block_xdr cimport *
%from experiments_xdr cimport *
%from consensus_api_includes cimport *
#endif

#if defined(XDRC_PYX)
%from types_xdr cimport *
%from block_xdr cimport *
%from experiments_xdr cimport *
%from consensus_api_includes cimport *
#endif

namespace edce {

typedef string experimentname<>;

program SignatureCheck {
    version SignatureCheckV1 {
        uint32 check_all_signatures(experimentname, SerializedBlock, SerializedPKs, SerializedBlockWithPK, uint64) = 1;
    } = 1;
} = 0x11111116;

}
