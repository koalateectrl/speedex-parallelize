#include <iostream>
#include "new_memory_database.h"

std::vector<std::bitset<SQRT_OUTPUT_BITS>> NewMemoryDatabase::get_hash() {
    return hash_;
}

void NewMemoryDatabase::calculate_hash() {
    uint64_t n = pow(2, SQRT_OUTPUT_BITS);
    for (auto it = database_.begin(); it != database_.end(); it++) {
        std::string message = std::string{static_cast<char>(it->first)} + it->second.toString();
        std::vector<std::bitset<SQRT_OUTPUT_BITS>> bitset_vec;
        create_str_vec(bitset_vec, message, n);
        sum_two_vecs(hash_, bitset_vec, n);
    }
}

void NewMemoryDatabase::create_str_vec(std::vector<std::bitset<SQRT_OUTPUT_BITS>> &bitset_vec, 
    const std::string& message, uint64_t n) {
    
    const unsigned char* msg = reinterpret_cast<const unsigned char *> (message.c_str());
    unsigned char out[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(out, msg, message.size());

    std::string concat;
    for (size_t i = 0; i < sizeof(out)/sizeof(out[0]); i++) {
        concat.append(std::bitset<8>(out[i]).to_string());
    }

    for (size_t i = 0; i < SQRT_OUTPUT_BITS; i++) {
        bitset_vec.push_back(std::bitset<SQRT_OUTPUT_BITS>(concat.substr(i * SQRT_OUTPUT_BITS, SQRT_OUTPUT_BITS)));
    }
}

void NewMemoryDatabase::sum_two_vecs(std::vector<std::bitset<SQRT_OUTPUT_BITS>> &bitset_sum, 
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> &vec_to_add, uint64_t n) {
    for (size_t i = 0; i < bitset_sum.size(); i++) {
        bitset_sum[i] = std::bitset<SQRT_OUTPUT_BITS>((std::bitset<SQRT_OUTPUT_BITS>(bitset_sum[i]).to_ullong() + 
            std::bitset<SQRT_OUTPUT_BITS>(vec_to_add[i]).to_ullong()) % n);
    }
}

NewUserAccount NewMemoryDatabase::get_account(const AccountID account_id) {
    if (!account_exists(account_id)) {
        return NewUserAccount{};
    }
    return database_.find(account_id)->second;
} 

std::unordered_map<TokenName, int64_t> NewMemoryDatabase::get_all_tokens(const AccountID account_id) {
    if (!account_exists(account_id)) {
        return std::unordered_map<TokenName, int64_t>();
    }

    return get_account(account_id).token_map_;
}

int64_t NewMemoryDatabase::get_token_balance(const AccountID account_id, const std::string& token_name) {
    NewUserAccount& account = find_account(account_id);
    auto token_it = account.token_map_.find(token_name);
    if (token_it == account.token_map_.end()) {
        return 0;
    }

    return token_it->second;
}

void NewMemoryDatabase::account_creation(const AccountID account_id) {
    if (database_.find(account_id) != database_.end()) {
        throw std::runtime_error("Account already exists");
    }
    std::lock_guard<std::shared_mutex> lock(mutex_);
    database_.insert(std::pair<AccountID, NewUserAccount>{account_id, NewUserAccount{}});
}


bool NewMemoryDatabase::account_exists(const AccountID account_id) {
    return database_.find(account_id) != database_.end();
}

void NewMemoryDatabase::update_balance(const AccountID account_id, const std::string& token_name, 
    int64_t amount) {
    if (get_token_balance(account_id, token_name) + amount < 0) {
        throw std::runtime_error("Balance is negative");
    }

    NewUserAccount& account = find_account(account_id);
    auto token_it = account.token_map_.find(token_name);
    if (token_it == account.token_map_.end()) {
        account.token_map_.insert(std::pair<std::string, int64_t>{token_name, amount});
    } else {
        token_it->second += amount;
    }
}

void NewMemoryDatabase::remove_account(const AccountID account_id) {
    if (!account_exists(account_id)) {
        throw std::runtime_error("Invalid db access - no account");
    }

    auto remove_it = database_.find(account_id);
    database_.erase(remove_it);
}

void NewMemoryDatabase::clear_db() {
    database_.clear();
    hash_.clear();
    for (size_t i = 0; i < SQRT_OUTPUT_BITS; i++) {
        hash_.push_back(std::bitset<SQRT_OUTPUT_BITS>{0});
    }
}

size_t NewMemoryDatabase::size() const {
    return database_.size();
}

NewUserAccount& NewMemoryDatabase::find_account(const AccountID account_id) {
    if (!account_exists(account_id)) {
        throw std::runtime_error("Invalid db access - no account");
    }
    return database_.find(account_id)->second;
}

NewMemoryDatabase::NewMemoryDatabase() {
    for (size_t i = 0; i < SQRT_OUTPUT_BITS; i++) {
        hash_.push_back(std::bitset<SQRT_OUTPUT_BITS>{0});
    }
}

int main() {
    if (sodium_init() == -1) {
        std::cout << "FAILED" << std::endl;
        return 1;
    }

    NewMemoryDatabase db;
    db.account_creation(1);
    db.update_balance(1, "ADA", 5);
    db.account_creation(2);
    db.update_balance(2, "ATOM", 10);
    std::cout << db.get_token_balance(1, "ADA") << std::endl;
    std::cout << db.get_account(1).toString() << std::endl;
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> before = db.get_hash();
    db.calculate_hash();
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> after = db.get_hash();

    for (size_t i = 0; i < SQRT_OUTPUT_BITS; i++) {
        std::cout << before[i] << std::endl;
    }

    for (size_t i = 0; i < SQRT_OUTPUT_BITS; i++) {
        std::cout << after[i] << std::endl;
    }

}


/* Questions
Do I need shared mutex when reading? Or just when writing?
Integer underflow/overflow attack?


*/
