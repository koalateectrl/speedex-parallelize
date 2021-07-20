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
auto split_accounts(ForwardIt first, ForwardIt last, Condition condition, int num_splits) {
    std::vector<ForwardIt> split_vec;

    if (num_splits < 2) {
        split_vec.push_back(last);
        return split_vec;
    }

    split_vec.push_back(std::partition(first, last, [&](const auto &v) {return condition(v) == 0;}));
    for (size_t i = 0; i < num_splits - 2; i++) {
        split_vec[i + 1] = std::partition(split_vec[i], last, [&](const auto &v) {return condition(v) == (i + 1);});
    }

    return split_vec;
}

uint32_t
init_shard(int idx, const SerializedAccountIDWithPK& account_with_pk, 
    const ExperimentParameters& params, uint16_t checker_begin_idx, 
    uint16_t checker_end_idx, uint16_t num_assets = 20,
    uint8_t tax_rate = 10, uint8_t smooth_mult = 10) {

    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_SHARD_PORT);
    auto client = xdr::srpc_client<SignatureShardV1>(fd.get());

    uint32_t return_value = *client.init_shard(account_with_pk, params, idx, 
        checker_begin_idx, checker_end_idx, num_assets, tax_rate, smooth_mult);
    std::cout << return_value << std::endl;
    return return_value;
}


uint32_t
poll_node(int idx, const SerializedBlockWithPK& block_with_pk) {
    
    auto fd = xdr::tcp_connect(hostname_from_idx(idx).c_str(), SIGNATURE_SHARD_PORT);
    auto client = xdr::srpc_client<SignatureShardV1>(fd.get());

    // if works return 0 else if failed return 1
    uint32_t return_value = *client.check_block(block_with_pk, 4);
    std::cout << return_value << std::endl;
    return return_value;
}

int main(int argc, char const *argv[]) {

    if (argc != 5) {
        std::printf("usage: ./signature_shard_controller experiment_name block_number num_shards total_machines \n");
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
        management_structures.db.add_account_to_db(account_with_pks[i].account, account_with_pks[i].pk);
    }

    

    management_structures.db.commit(0);

    AccountIDWithPKList account_with_pk_list;

    account_with_pk_list.insert(account_with_pk_list.end(), account_with_pks.begin(), account_with_pks.end());

    int num_shards = std::stoi(argv[3]);
    auto split_ptrs = split_accounts(account_with_pks.begin(), account_with_pks.end(), 
        [&num_shards] (auto x) {return x.account % num_shards;}, num_shards);

    std::vector<AccountIDWithPKList> account_with_pk_split_list;

    AccountIDWithPKList first_split;
    first_split.insert(first_split.end(), account_with_pks.begin(), split_ptrs[0]);
    account_with_pk_split_list.push_back(first_split);

    if (num_shards > 1) {
        for (size_t i = 1; i < num_shards - 1; i++) {
            AccountIDWithPKList curr_split;
            curr_split.insert(curr_split.end(), split_ptrs[i - 1], split_ptrs[i]);
            account_with_pk_split_list.push_back(curr_split);
        }

        AccountIDWithPKList last_split;
        last_split.insert(last_split.end(), split_ptrs[num_shards - 2], account_with_pks.end());
        account_with_pk_split_list.push_back(last_split);
    }

    int total_machines = std::stoi(argv[4]);
    size_t checker_node_begin_idx = 2 + num_shards;
    size_t num_checkers_per_shard = (total_machines - num_shards - 1) / num_shards;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, num_shards),
        [&account_with_pk_split_list, &num_shards, &params](auto r) {
            for (size_t i = r.begin(); i != r.end(); i++) {
                SerializedAccountIDWithPK serialized_account_with_pk = xdr::xdr_to_opaque(account_with_pk_split_list[i]);
                if (init_shard(i + 2, serialized_account_with_pk, params, 
                    checker_node_begin_idx + i * num_checkers_per_shard,
                    checker_node_begin_idx + (i + 1) * num_checkers_per_shard) == 1) {
                    throw std::runtime_error("init shard failed!!!");
                }
            }
        });


    // Send whole block


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

    if (poll_node(2, serialized_block_with_pk) == 1) {
        throw std::runtime_error("sig checking failed!!!");
    }

    std::cout << "HELLO WORLD" << std::endl;

    return 0;

}




















