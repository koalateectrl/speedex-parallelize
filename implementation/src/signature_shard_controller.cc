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


using namespace edce;

std::string hostname_from_idx(int idx) {
    return std::string("10.10.1.") + std::to_string(idx);
}

void
init_shard(int idx, const SerializedAccountIDWithPK& account_with_pk, 
    const ExperimentParameters& params, uint16_t num_assets = 20,
    uint8_t tax_rate = 10, uint8_t smooth_mult = 10) {

    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_SHARD_PORT);
    auto client = xdr::srpc_client<SignatureShardV1>(fd.get());

    uint32_t return_value = *client.init_shard(account_with_pk, params, num_assets, tax_rate, 
        smooth_mult);
    std::cout << return_value << std::endl;
}


void
poll_node(int idx) {
    
    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_SHARD_PORT);
    auto client = xdr::srpc_client<SignatureShardV1>(fd.get());

    // if works return 0 else if failed return 1
    client.print_hello_world();
}

int main(int argc, char const *argv[]) {

    if (argc != 3) {
        std::printf("usage: ./signature_shard_controller experiment_name num_shards\n");
        return -1;
    }

    DeterministicKeyGenerator key_gen;

    ExperimentParameters params;

    std::string experiment_root = std::string("experiment_data/") + std::string(argv[1]);

    std::string params_filename = experiment_root + std::string("/params");

    if (load_xdr_from_file(params, params_filename.c_str())) {
        throw std::runtime_error("failed to load params file");
    }

    EdceManagementStructures management_structures(
        20,
        ApproximationParameters {
            .tax_rate = 10,
            .smooth_mult = 10
        });

    std::printf("num accounts: %u\n", params.num_accounts);

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

    for (int32_t i = 0; i < params.num_accounts; i++) {

        //std::printf("%lu %s\n", account_id_list[i], DebugUtils::__array_to_str(pks.at(i).data(), pks[i].size()).c_str());
        management_structures.db.add_account_to_db(account_with_pks[i].account, account_with_pks[i].pk);
    }

    management_structures.db.commit(0);

    AccountIDWithPKList account_with_pk_list;

    account_with_pk_list.insert(account_with_pk_list.end(), account_with_pks.begin(), account_with_pks.end());

    SerializedAccountIDWithPK serialized_account_with_pk = xdr::xdr_to_opaque(account_with_pk_list);

    init_shard(2, serialized_account_with_pk, params);

    return 0;

}




















