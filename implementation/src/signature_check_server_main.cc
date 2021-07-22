#include "signature_check_api_server.h"
#include "signature_shard_api_server.h"


using namespace edce;

int main(int argc, char const *argv[]) {

    if (argc != 2) {
        std::printf("usage: ./signature_check_server_main is_shard\n");
        return 0;
    }

    if (argc == 2) {
        int is_shard = std::stoi(argv[1]);
        if (is_shard == 0) {
            SignatureCheckApiServer signature_check_server;
        } else if (is_shard == 1) {
            SignatureShardApiServer signature_shard_server;
        } else {
            throw std::runtime_error("The is_shard argument must be 0 or 1!!!");
        }
    }

    return 0;

}