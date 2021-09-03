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


void sum_two_vecs(const std::vector<std::string> &vec_one, const std::vector<std::string> &vec_two,
    std::vector<std::string> &vec_sum, uint64_t n) {

    for (size_t i = 0; i < vec_one.size(); i++) {
        unsigned long long orig_sum = std::bitset<SQRT_OUTPUT_BITS>(vec_one[i]).to_ullong() +
            std::bitset<SQRT_OUTPUT_BITS>(vec_two[i]).to_ullong();
        vec_sum.push_back(std::bitset<SQRT_OUTPUT_BITS>(orig_sum % n).to_string());
    }
}


int main(int argc, char const *argv[]) {

    auto overall_timestamp = init_time_measurement();

    if (argc != 2) {
        std::printf("usage: ./test_multiset_hash_speed experiment_name\n");
    }

    std::string experiment_root = std::string("experiment_data/") + std::string(argv[1]);

    EdceManagementStructures management_structures(
        20,
        ApproximationParameters {
            .tax_rate = 10,
            .smooth_mult = 10
        });

    AccountIDList account_id_list;

    auto accounts_filename = experiment_root + std::string("/accounts");
    if (load_xdr_from_file(account_id_list, accounts_filename.c_str())) {
        throw std::runtime_error("failed to load accounts list " + accounts_filename);
    }

    std::vector<AccountIDWithPK> account_with_pks;
    account_with_pks.resize(account_id_list.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, account_id_list.size()),
        [&key_gen, &account_id_list, &account_with_pks](auto r) {
            for (size_t i = r.begin(); i < r.end(); i++) {
                AccountIDWithPK account_id_with_pk;
                account_id_with_pk.account = account_id_list[i];
                auto [_, pk] = key_gen.deterministic_key_gen(account_id_list[i]);
                account_id_with_pk.pk = pk;
                account_with_pks[i] = account_id_with_pk;
            }
        });

    size_t num_accounts = account_with_pks.size();

    std::cout << "Total Number of Accounts: " << num_accounts << std::endl;

    float overall_res = measure_time(overall_timestamp);

    std::cout << "Hashed " << num_accounts << " signatures in " << overall_res << std::endl;

    if (sodium_init() == -1) {
        std::cout << "FAILED" << std::endl;
        return 1;
    }

    uint64_t n = pow(2, SQRT_OUTPUT_BITS);

    std::string message = "test";
    std::string message2 = "test2";

    std::vector<std::string> str_vec;
    std::vector<std::string> str_vec2;
    create_str_vec(str_vec, message, n);
    create_str_vec(str_vec2, message2, n);

    std::vector<std::string> summed_vec;

    sum_two_vecs(str_vec, str_vec2, summed_vec, n);

    for (size_t i = 0; i < summed_vec.size(); i++) {
        std::cout << "Vec1: " << str_vec[i] << " Vec2: " << str_vec2[i] << " Sum: " << summed_vec[i] << std::endl;
    }

    return 0;

}