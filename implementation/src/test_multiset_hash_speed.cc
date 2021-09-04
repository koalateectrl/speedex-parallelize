#include "xdr/experiments.h"
#include "xdr/signature_shard_api.h"
#include "rpc/rpcconfig.h"
#include <xdrpp/srpc.h>
#include <thread>
#include <chrono>

#include <cstdint>
#include <vector>

#include "crypto_utils.h"
#include "utils.h"
#include "xdr/experiments.h"
#include "edce_management_structures.h"
#include "tbb/global_control.h"

#include <set>
#include <iostream>
#include <sodium.h>
#include <math.h>
#include <vector>
#include <cstdlib>
#include <bitset>

#define OUTPUT_BITS 256
#define SQRT_OUTPUT_BITS 16


void create_str_vec(std::vector<std::string> &str_vec, const std::string& message, uint64_t n) {
    unsigned char out[crypto_hash_sha256_BYTES];
    uint64_t msg_size = message.size();
    const unsigned char* msg = reinterpret_cast<const unsigned char *> (message.c_str());
    crypto_hash_sha256(out, msg, msg_size);

    std::string concat;

    for (size_t i = 0; i < sizeof(out)/sizeof(out[0]); i++) {
        concat.append(std::bitset<8>(out[i]).to_string());
    }

    for (size_t i = 0; i < SQRT_OUTPUT_BITS; i++) {
        std::string substr = concat.substr(i * SQRT_OUTPUT_BITS, SQRT_OUTPUT_BITS);
        str_vec.push_back(substr);
    }
}


void sum_two_vecs(std::vector<std::string> &vec_sum, const std::vector<std::string> &vec_to_add, uint64_t n) {

    for (size_t i = 0; i < vec_sum.size(); i++) {
        unsigned long long orig_sum = std::bitset<SQRT_OUTPUT_BITS>(vec_sum[i]).to_ullong() +
            std::bitset<SQRT_OUTPUT_BITS>(vec_to_add[i]).to_ullong();
        vec_sum[i] = std::bitset<SQRT_OUTPUT_BITS>(orig_sum % n).to_string();
    }
}


int main(int argc, char const *argv[]) {

    auto overall_timestamp = edce::init_time_measurement();

    if (argc != 2) {
        std::printf("usage: ./test_multiset_hash_speed i\n");
    }

    uint64_t num_test_accounts = std::stoi(1);

    if (sodium_init() == -1) {
        std::cout << "FAILED" << std::endl;
        return 1;
    }

    uint64_t n = pow(2, SQRT_OUTPUT_BITS);

    std::vector<std::string> msg_vec;

    auto create_timestamp = edce::init_time_measurement();

    // create vector of strings
    for (size_t i = 0; i < num_test_accounts; i++) {
        std::string int_str = std::to_string(i);
        const unsigned char* msg = reinterpret_cast<const unsigned char *> (int_str.c_str());
        unsigned char out[crypto_hash_sha256_BYTES];
        crypto_hash_sha256(out, msg, int_str.size());
        msg_vec.push_back(std::string(out, out + sizeof(out)/sizeof(out[0])));
    }

    float create_res = edce::measure_time(create_timestamp);

    std::cout << "Create " << num_test_accounts << " 256 bit inputs in " << create_res << std::endl;

    std::vector<std::string> summed_vec;
    create_str_vec(summed_vec, msg_vec[0], n);

    auto hash_timestamp = edce::init_time_measurement();

    // hashing + summing 
    for (size_t i = 0; i < msg_vec.size() - 1; i++) {
        std::vector<std::string> str_vec;
        create_str_vec(str_vec, msg_vec[i + 1], n);
        sum_two_vecs(summed_vec, str_vec, n);
    }

    float hash_res = edce::measure_time(hash_timestamp);

    std::cout << "Hash and Sum " << num_test_accounts << " 256 bit inputs in " << hash_res << std::endl;

    for (size_t i = 0; i < summed_vec.size(); i++) {
        std::cout << "Sum: " << summed_vec[i] << std::endl;
    }

    float overall_res = edce::measure_time(overall_timestamp);

    std::cout << "Overall: " << num_accounts << " in " << overall_res << std::endl;

    return 0;

}