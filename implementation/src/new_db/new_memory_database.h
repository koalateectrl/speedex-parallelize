#include <iostream>
#include <sodium.h>
#include <math.h>
#include <vector>
#include <cstdlib>
#include <unordered_map>
#include <shared_mutex>
#include "new_user_account.h"


#define OUTPUT_BITS 256
#define SQRT_OUTPUT_BITS 16

class NewMemoryDatabase {
public:
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> get_hash();
    void calculate_hash_from_scratch();
    NewUserAccount get_account(const AccountID account_id);
    std::unordered_map<TokenName, int64_t> get_all_tokens(const AccountID account_id);
    int64_t get_token_balance(const AccountID account_id, const std::string& token_name);

    void account_creation(const AccountID account_id);
    bool account_exists(const AccountID account_id);

    void update_balance(const AccountID account_id, const std::string& token_name, int64_t amount);

    void remove_account(const AccountID account_id);
    void clear_db();

    size_t size() const;

    NewMemoryDatabase();

private:
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> hash_;
    uint64_t hash_n_;
    std::shared_mutex mutex_;
    std::unordered_map<AccountID, NewUserAccount> database_;

    void clear_hash();
    NewUserAccount& find_account(const AccountID account_id);
    
    void create_bitvec(std::vector<std::bitset<SQRT_OUTPUT_BITS>> &bitset_vec, 
        const std::string& message);
    void add_bitvec(std::vector<std::bitset<SQRT_OUTPUT_BITS>> &bitset_sum, 
        std::vector<std::bitset<SQRT_OUTPUT_BITS>> &vec_to_add);
    void subtract_bitvec(std::vector<std::bitset<SQRT_OUTPUT_BITS>> &bitset_sum, 
        std::vector<std::bitset<SQRT_OUTPUT_BITS>> &vec_to_subtract);

    
};
