#include "hello_world_api_server.h"


namespace edce {
    
int main(int argc, char const *argv[]) {

    if (argc != 1) {
        std::printf("usage: ./hello_world_server_main \n");
        return 0;
    }

    if (argc == 1) {
        MyProgApiServer myprog_server();
    }

}
}