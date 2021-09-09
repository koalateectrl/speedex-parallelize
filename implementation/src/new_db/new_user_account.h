#include <unordered_map>

using TokenName = std::string;
using AccountID = size_t;
using PublicKey = size_t;

struct NewUserAccount {
    std::unordered_map<TokenName, int64_t> token_map_;
    size_t virt_shard_;
    PublicKey public_key_;
    AccountID account_id_;
};