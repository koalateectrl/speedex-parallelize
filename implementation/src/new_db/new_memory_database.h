#include <unordered_map>
#include <shared_mutex>
#include "new_user_account.h"


class NewMemoryDatabase {
public:
    size_t get_hash();
    void calculate_hash();

    NewUserAccount get_account(const AccountID account_id);
    std::unordered_map<TokenName, int64_t> get_all_tokens(const AccountID account_id);
    int64_t get_token_balance(const AccountID account_id, const std::string& token_name);

    void account_creation(const AccountID account_id);
    bool account_exists(const AccountID account_id);

    void update_balance(const AccountID account_id, const std::string& token_name, int64_t amount);

    void remove_account(const AccountID account_id);
    void clear_db();

    size_t size() const;

private:
    size_t hash_;
    std::shared_mutex mutex_;
    std::unordered_map<AccountID, NewUserAccount> database_;

    NewUserAccount& find_account(const AccountID account_id);

    
};
