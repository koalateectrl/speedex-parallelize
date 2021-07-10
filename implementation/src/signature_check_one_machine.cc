#include "crypto_utils.h"

#include "utils.h"
#include <cstdint>
#include <cstddef>

#include "xdr/experiments.h"

#include "edce_management_structures.h"
#include "tbb/global_control.h"

using namespace edce;

int main(int argc, char const *argv[])
{

    if (argc != 4) {
        std::printf("usage: ./signature_check_one_machine experiment_name block_number num_threads\n");
        return -1;
    }

    auto timestamp = init_time_measurement();

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

    std::vector<PublicKey> pks;
    pks.resize(account_id_list.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, account_id_list.size()),
        [&key_gen, &account_id_list, &pks](auto r) {
            for (size_t i = r.begin(); i < r.end(); i++) {
                auto [_, pk] = key_gen.deterministic_key_gen(account_id_list[i]);
                pks[i] = pk;
            }
        });

    for (int32_t i = 0; i < params.num_accounts; i++) {

        //std::printf("%lu %s\n", account_id_list[i], DebugUtils::__array_to_str(pks.at(i).data(), pks[i].size()).c_str());
        management_structures.db.add_account_to_db(account_id_list[i], pks[i]);
    }

    management_structures.db.commit(0);

    BlockSignatureChecker checker(management_structures);

    ExperimentBlock block;

    std::string block_filename = experiment_root + std::string("/") + std::string(argv[2]) + std::string(".txs");

    if (load_xdr_from_file(block, block_filename.c_str())) {
        std::printf("%s\n", block_filename.c_str());
        throw std::runtime_error("failed to load tx block");
    }

    SignedTransactionList tx_list;

    tx_list.insert(tx_list.end(), block.begin(), block.end());

    SerializedBlock serialized_block = xdr::xdr_to_opaque(tx_list);
    
    size_t num_threads = std::stoi(argv[3]);

    tbb::global_control control(
        tbb::global_control::max_allowed_parallelism, num_threads);

    if (!checker.check_all_sigs(serialized_block)) {
        throw std::runtime_error("sig checking failed!!!");
    }

    float res = measure_time(timestamp);

    std::printf("checked %lu sigs in %lf with max %lu threads\n", tx_list.size(), res, num_threads);
    return 0;
}
