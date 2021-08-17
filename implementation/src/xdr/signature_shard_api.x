
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

typedef string ip_str<>;

program SignatureShard {
    version SignatureShardV1 {
        uint32 init_shard(SerializedAccountIDWithPK, uint32, uint32, uint32, uint32, uint64) = 1;
        uint32 check_block(SerializedBlockWithPK, uint64) = 2;
        uint32 init_checker(void) = 3;
        uint32 move_virt_shard(ip_str, uint64) = 4;
    } = 1;
} = 0x11111117;

}
