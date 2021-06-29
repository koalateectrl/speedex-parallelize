
#if defined(XDRC_HH) || defined(XDRC_SERVER)
%#include "xdr/block.h"
%#include "xdr/experiments.h"
#endif


namespace edce {

typedef string hostname<>;

program MyProg {
	version MyProgV1 {
		void print_hello_world(void) = 1;
	} = 1;
} = 0x11111115;

}
