#include "signature_check_api_server.h"


using namespace edce;
    
int main(int argc, char const *argv[]) {

    if (argc != 2) {
        std::printf("usage: ./signature_check_server_main experiment_name\n");
        return 0;
    }

    if (argc == 2) {
        SignatureCheckApiServer signature_check_server;
    }

    return 0;

}