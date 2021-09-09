#include <iostream>
#include "new_memory_database.h"

size_t NewMemoryDatabase::get_hash() {
    return hash_;
}

void NewMemoryDatabase::calculate_hash() {
    hash_ = 1;
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
        std::cout << "GOT HERE" << std::endl;
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
    hash_ = 0;
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

int main() {
    NewMemoryDatabase db;
    db.account_creation(1);
    db.update_balance(1, "ADA", 5);
    db.clear_db();
    std::cout << db.get_token_balance(1, "ADA") << std::endl;
}


/* Questions
Do I need shared mutex when reading? Or just when writing?
Integer underflow/overflow attack?


*/
