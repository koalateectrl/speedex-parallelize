
#include "rpc/signature_check_api.h"
#include <xdrpp/marshal.h>
#include <iostream>

#include "simple_debug.h"


#include "crypto_utils.h"
#include "utils.h"

#include "xdr/experiments.h"

#include "edce_management_structures.h"
#include "tbb/global_control.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace edce {

//rpc

std::unique_ptr<unsigned int>
SignatureCheckV1_server::check_all_signatures(const SerializedBlockWithPK& block_with_pk, 
  const uint64& num_threads)
{

  auto timestamp = init_time_measurement();

  SamBlockSignatureChecker sam_checker;

  tbb::global_control control(
    tbb::global_control::max_allowed_parallelism, num_threads);

  if (!sam_checker.check_all_sigs(block_with_pk)) {
    return std::make_unique<unsigned int>(1);
  } 

  float res = measure_time(timestamp);
  std::cout << "Total time for check_all_signatures RPC call: " << res << std::endl;

  return std::make_unique<unsigned int>(0);

}

// non rpc
uint32_t
SignatureCheckV1_server::print_local_ip() {
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    sockaddr_in loopback;

    if (sock == -1) {
        std::cerr << "Could not socket\n";
        return 1;
    }

    std::memset(&loopback, 0, sizeof(loopback));
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = 1337;   // can be any IP address
    loopback.sin_port = htons(9);      // using debug port

    if (connect(sock, reinterpret_cast<sockaddr*>(&loopback), sizeof(loopback)) == -1) {
        close(sock);
        std::cerr << "Could not connect\n";
        return 1;
    }

    socklen_t addrlen = sizeof(loopback);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&loopback), &addrlen) == -1) {
        close(sock);
        std::cerr << "Could not getsockname\n";
        return 1;
    }

    close(sock);

    char buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &loopback.sin_addr, buf, INET_ADDRSTRLEN) == 0x0) {
        std::cerr << "Could not inet_ntop\n";
        return 1;
    } else {
        std::cout << "Local ip address: " << buf << "\n";
    }
    return 0;
}

}
