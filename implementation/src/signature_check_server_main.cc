#include "signature_check_api_server.h"
#include "signature_shard_api_server.h"


using namespace edce;

int main(int argc, char const *argv[]) {

    if (argc != (2 || 3)) {
        std::printf("usage: ./signature_check_server_main is_shard shard_ip(if sig_checker)\n");
        return 0;
    }

    int is_shard = std::stoi(argv[1]);

    if (argc == 2) {
        if (is_shard == 1) {
            SignatureShardApiServer signature_shard_server;
        } else {
            throw std::runtime_error("The is_shard argument must be 1 to use 2 params!!!");
        }
    }

    if (argc == 3) {
        if (is_shard == 0) {
            SignatureCheckApiServer signature_check_server;
        } else {
            throw std::runtime_error("The is_shard argument must be 0 to use 3 params!!!")
        }
    }

    return 0;

}