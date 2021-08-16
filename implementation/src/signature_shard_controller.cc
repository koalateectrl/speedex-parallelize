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

template<class ForwardIt, class Condition>
auto shard_accounts(ForwardIt first, ForwardIt last, Condition condition, uint64_t num_shards) {
    std::vector<ForwardIt> shard_vec;

    if (num_shards < 2) {
        shard_vec.push_back(last);
        return shard_vec;
    }

    shard_vec.push_back(std::partition(first, last, [&](const auto &v) {return condition(v) == 0;}));
    for (size_t i = 0; i < num_shards - 2; i++) {
        shard_vec[i + 1] = std::partition(shard_vec[i], last, [&](const auto &v) {return condition(v) == (i + 1);});
    }

    return shard_vec;
}

uint32_t
init_shard(int idx, const SerializedAccountIDWithPK& account_with_pk, 
    const ExperimentParameters& params,  uint64_t virt_shard_idx, uint16_t num_assets = 20,
    uint8_t tax_rate = 10, uint8_t smooth_mult = 10) {

    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_SHARD_PORT);
    auto client = xdr::srpc_client<SignatureShardV1>(fd.get());

    uint32_t return_value = *client.init_shard(account_with_pk, params, idx, num_assets, tax_rate, smooth_mult, virt_shard_idx);
    return return_value;
}


uint32_t
poll_node(int idx, const SerializedBlockWithPK& block_with_pk, 
    const uint64_t& num_threads) {
    
    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_SHARD_PORT);
    auto client = xdr::srpc_client<SignatureShardV1>(fd.get());

    uint32_t return_value = *client.check_block(block_with_pk, num_threads);
    return return_value;
}

int main(int argc, char const *argv[]) {

    if (argc != 5) {
        std::printf("usage: ./signature_shard_controller experiment_name block_number num_shards num_threads\n");
        return -1;
    }

    auto timestamp = init_time_measurement();
    auto setup_timestamp = init_time_measurement();

    std::cout << "SETUP (READING XDR AND LOADING PARAMS ETC)" << std::endl;

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

    std::cout << "Total Number of Accounts: " << params.num_accounts << std::endl;

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
        management_structures.db.add_account_to_db(account_with_pks[i].account, account_with_pks[i].pk);
    }

    management_structures.db.commit(0);

    AccountIDWithPKList account_with_pk_list;

    account_with_pk_list.insert(account_with_pk_list.end(), account_with_pks.begin(), account_with_pks.end());

    size_t num_shards = std::stoul(argv[3]);
    size_t NUM_VIRTUAL_SHARDS = 64;

    auto shard_ptrs = shard_accounts(account_with_pks.begin(), account_with_pks.end(), 
        [&NUM_VIRTUAL_SHARDS] (auto x) {return x.account % NUM_VIRTUAL_SHARDS;}, NUM_VIRTUAL_SHARDS);

    std::vector<AccountIDWithPKList> account_with_pk_shard_list;

    AccountIDWithPKList first_shard;
    first_shard.insert(first_shard.end(), account_with_pks.begin(), shard_ptrs[0]);
    account_with_pk_shard_list.push_back(first_shard);

    if (num_shards > 1) {
        for (size_t i = 1; i < num_shards - 1; i++) {
            AccountIDWithPKList curr_shard;
            curr_shard.insert(curr_shard.end(), shard_ptrs[i - 1], shard_ptrs[i]);
            account_with_pk_shard_list.push_back(curr_shard);
        }

        AccountIDWithPKList last_shard;
        last_shard.insert(last_shard.end(), shard_ptrs[num_shards - 2], account_with_pks.end());
        account_with_pk_shard_list.push_back(last_shard);
    }

    auto setup_res = measure_time(setup_timestamp);

    std::cout << "Setup in " << setup_res << std::endl;

    std::cout << "INITIALIZING SHARDS BY SENDING ACCOUNT DATA" << std::endl;

    auto init_timestamp = init_time_measurement();

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, NUM_VIRTUAL_SHARDS),
        [&account_with_pk_shard_list, &NUM_VIRTUAL_SHARDS, &num_shards, &params] (auto r) {
            for (size_t i = r.begin(); i != r.end(); i++) {
                SerializedAccountIDWithPK serialized_account_with_pk = xdr::xdr_to_opaque(account_with_pk_shard_list[i]);
                if (init_shard((i % num_shards) + 2, serialized_account_with_pk, params, i) == 1) {
                    throw std::runtime_error("init shard failed!!!");
                }
            }
        });

    float init_res = measure_time(init_timestamp);

    std::cout << "Initialized shards in " << init_res << std::endl;


    // Send whole block

    std::cout << "SIGNATURE CHECKING " << std::endl;

    auto sig_timestamp = init_time_measurement();

    ExperimentBlock block;

    std::string block_filename = experiment_root + std::string("/") + std::string(argv[2]) + std::string(".txs");

    if (load_xdr_from_file(block, block_filename.c_str())) {
        std::printf("%s\n", block_filename.c_str());
        throw std::runtime_error("failed to load tx block");
    }

    SignedTransactionList tx_list;

    tx_list.insert(tx_list.end(), block.begin(), block.end());
    
    std::vector<SignedTransactionWithPK> tx_with_pks;
    tx_with_pks.resize(tx_list.size());

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, tx_list.size()),
        [&tx_list, &management_structures, &tx_with_pks](auto r) {
            for (size_t i = r.begin(); i < r.end(); i++) {
                SignedTransactionWithPK signed_tx_with_pk;
                signed_tx_with_pk.signedTransaction = tx_list[i];
                signed_tx_with_pk.pk = *management_structures.db.get_pk_nolock(tx_list[i].transaction.metadata.sourceAccount);
                tx_with_pks[i] = signed_tx_with_pk;
            }
        });

    SignedTransactionWithPKList tx_with_pk_list;

    tx_with_pk_list.insert(tx_with_pk_list.end(), tx_with_pks.begin(), tx_with_pks.end());

    SerializedBlockWithPK serialized_block_with_pk = xdr::xdr_to_opaque(tx_with_pk_list);

    size_t num_threads = std::stoul(argv[4]);

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, num_shards),
        [&serialized_block_with_pk, &num_threads](auto r) {
            for (size_t i = r.begin(); i != r.end(); i++) {
                if (poll_node(i + 2, serialized_block_with_pk, num_threads) == 1) {
                    throw std::runtime_error("sig checking failed!!!");
                }
            }
        });

    float res = measure_time(timestamp);
    float sig_res = measure_time(sig_timestamp);

    std::cout << "Checked " << tx_list.size() << " signatures in " << sig_res << std::endl;

    std::cout << "Finished entire process in " << res << 
    " with " << num_shards << " shards, with each worker with max " << num_threads << " threads." << std::endl;

    return 0;

}




















