#include <unordered_map>

using TokenName = std::string;
using AccountID = size_t;
using PublicKey = size_t;

struct NewUserAccount {
    std::unordered_map<TokenName, int64_t> token_map_;
    size_t virt_shard_;
    PublicKey public_key_;
    AccountID account_id_;

    std::string toString() {
        std::string ret_str = std::string{static_cast<char>(virt_shard_)} + 
            std::string{static_cast<char>(public_key_)} + std::string{static_cast<char>(account_id_)};
        for (auto it = token_map_.begin(); it != token_map_.end(); it++) {
            ret_str += it->first;
            ret_str += std::string{static_cast<char>(it->second)};
        }
        return ret_str;
    }
};