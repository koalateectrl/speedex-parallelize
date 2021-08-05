#ifndef _CONNECTION_INFO_H_INCLUDED_
#define _CONNECTION_INFO_H_INCLUDED_ 1

#include <xdrpp/msgsock.h>

namespace edce {

struct rpcsockptr {
    xdr::rpc_sock* sock_ptr;
};

}

#endif