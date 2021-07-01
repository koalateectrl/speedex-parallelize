#include "signature_check_api_server.h"


using namespace edce;
    
int main(int argc, char const *argv[]) {

    if (argc != 1) {
        std::printf("usage: ./signature_check_server_main \n");
        return 0;
    }

    if (argc == 1) {
        SignatureCheckApiServer signaturecheck_server;
    }

    return 0;

}