%#include "xdr/types.h"

namespace edce {

typedef opaque SigningKey[128]; //libsodium docs are vague about signing key len (seems like it might also include public key)

//hacky api for controlling server as it runs
program ServerControl {
	version ServerControlV1 {
		SigningKey get_root_sk(void) = 1;
		void add_sig_check_threads(int) = 2;
		void add_tx_processing_threads(int) = 3;
		void generate_block(void) = 4;
	} = 12;

} = 0x12345678;
	
}