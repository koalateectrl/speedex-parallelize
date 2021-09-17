#include <iostream>
#include "new_memory_database.h"

std::vector<std::bitset<SQRT_OUTPUT_BITS>> NewMemoryDatabase::get_hash() {
    return hash_;
}

void NewMemoryDatabase::calculate_hash_from_scratch() {
    clear_hash();
    for (auto it = database_.begin(); it != database_.end(); it++) {
        std::string message = std::string{static_cast<char>(it->first)} + it->second.toString();
        std::vector<std::bitset<SQRT_OUTPUT_BITS>> bitset_vec;
        create_bitvec(bitset_vec, message);
        add_bitvec(hash_, bitset_vec);
    }
}

void NewMemoryDatabase::create_bitvec(std::vector<std::bitset<SQRT_OUTPUT_BITS>> &bitset_vec, 
    const std::string& message) {
    
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

void NewMemoryDatabase::add_bitvec(std::vector<std::bitset<SQRT_OUTPUT_BITS>> &bitset_sum, 
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> &vec_to_add) {
    for (size_t i = 0; i < bitset_sum.size(); i++) {
        bitset_sum[i] = std::bitset<SQRT_OUTPUT_BITS>((std::bitset<SQRT_OUTPUT_BITS>(bitset_sum[i]).to_ullong() + 
            std::bitset<SQRT_OUTPUT_BITS>(vec_to_add[i]).to_ullong()) % hash_n_);
    }
}

void NewMemoryDatabase::subtract_bitvec(std::vector<std::bitset<SQRT_OUTPUT_BITS>> &bitset_sum, 
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> &vec_to_subtract) {
    for (size_t i = 0; i < bitset_sum.size(); i++) {
        bitset_sum[i] = std::bitset<SQRT_OUTPUT_BITS>((std::bitset<SQRT_OUTPUT_BITS>(bitset_sum[i]).to_ullong() - 
            std::bitset<SQRT_OUTPUT_BITS>(vec_to_subtract[i]).to_ullong()) % hash_n_);
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

    std::string message = std::string{static_cast<char>(account_id)} + get_account(account_id).toString();
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> bitset_vec;
    create_bitvec(bitset_vec, message);
    add_bitvec(hash_, bitset_vec);
}


bool NewMemoryDatabase::account_exists(const AccountID account_id) {
    return database_.find(account_id) != database_.end();
}

void NewMemoryDatabase::update_balance(const AccountID account_id, const std::string& token_name, 
    int64_t amount) {
    if (get_token_balance(account_id, token_name) + amount < 0) {
        throw std::runtime_error("Balance is negative");
    }

    std::lock_guard<std::shared_mutex> lock(mutex_);
    NewUserAccount& account = find_account(account_id);
    std::string message = std::string{static_cast<char>(account_id)} + account.toString();
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> bitset_vec;
    create_bitvec(bitset_vec, message);
    subtract_bitvec(hash_, bitset_vec);

    auto token_it = account.token_map_.find(token_name);
    if (token_it == account.token_map_.end()) {
        account.token_map_.insert(std::pair<std::string, int64_t>{token_name, amount});
    } else {
        token_it->second += amount;
    }

    std::string message_new = std::string{static_cast<char>(account_id)} + get_account(account_id).toString();
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> bitset_vec_new;
    create_bitvec(bitset_vec_new, message_new);
    add_bitvec(hash_, bitset_vec_new);
}

void NewMemoryDatabase::remove_account(const AccountID account_id) {
    if (!account_exists(account_id)) {
        throw std::runtime_error("Invalid db access - no account");
    }

    std::lock_guard<std::shared_mutex> lock(mutex_);
    std::string message = std::string{static_cast<char>(account_id)} + get_account(account_id).toString();
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> bitset_vec;
    create_bitvec(bitset_vec, message);
    subtract_bitvec(hash_, bitset_vec);

    auto remove_it = database_.find(account_id);
    database_.erase(remove_it);
}

void NewMemoryDatabase::clear_db() {
    std::lock_guard<std::shared_mutex> lock(mutex_);
    database_.clear();
    clear_hash();
}

size_t NewMemoryDatabase::size() const {
    return database_.size();
}

void NewMemoryDatabase::clear_hash() {
    hash_.clear();
    for (size_t i = 0; i < SQRT_OUTPUT_BITS; i++) {
        hash_.push_back(std::bitset<SQRT_OUTPUT_BITS>{0});
    }
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

    std::vector<std::bitset<SQRT_OUTPUT_BITS>> before = db.get_hash();
    
    db.account_creation(2);
    db.update_balance(2, "ATOM", 10);

    std::vector<std::bitset<SQRT_OUTPUT_BITS>> after = db.get_hash();
    
    db.remove_account(2);
    std::vector<std::bitset<SQRT_OUTPUT_BITS>> after_remove = db.get_hash();

    std::cout << before[0] << std::endl;
    std::cout << "---------------------------" << std::endl;
    
    std::cout << after[0] << std::endl;

    std::cout << "---------------------------" << std::endl;

    std::cout << after_remove[0] << std::endl;

    db.calculate_hash_from_scratch();
    std::cout << db.get_hash()[0] << std::endl;
    

}


/* Questions
Do I need shared mutex when reading? Or just when writing?
Integer underflow/overflow attack?


*/
