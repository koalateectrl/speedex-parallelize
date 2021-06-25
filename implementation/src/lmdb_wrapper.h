
#pragma once

#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <cstdint>

#include <lmdb.h>

#include "cleanup.h"

#include "xdr/transaction.h"

#define DEFAULT_LMDB_FLAGS MDB_WRITEMAP | MDB_NOSYNC //MDB_NOTLS|MDB_WRITEMAP|MDB_NOSYNC


class dberror : public std::runtime_error {
  static std::string mkWhat(int code, const char *msg);
public:
  const int code_;
  dberror(int code, const char *msg)  
    : std::runtime_error(mkWhat(code, msg)), code_(code) {}
};

struct dbval : MDB_val {
  static constexpr const void *_tovoidp(const void *p) { return p; }

  using opt = std::optional<dbval>;
  dbval() : MDB_val{0, nullptr} {}
  dbval(std::string_view sv)
    : MDB_val{sv.size(), const_cast<char *>(sv.data())} {}
  dbval(const void *data, std::size_t size)
    : MDB_val{size, const_cast<void *>(data)} {}
  template<typename T,
	   typename = std::enable_if_t<sizeof(typename T::value_type) == 1>>
  dbval(const T&t)
    : MDB_val{t.size(), const_cast<void *>(_tovoidp(t.data()))} {}
  explicit dbval(const size_t &i) : dbval(&i, sizeof(i)) {}
  std::string str() const {
    return {static_cast<char *>(mv_data), mv_size};
  }
  std::string hex() const;
  std::vector<std::uint8_t> bytes() const {
    return {static_cast<uint8_t *>(mv_data),
	    static_cast<uint8_t *>(mv_data) + mv_size};
  }

  size_t uint() const {
    if (mv_size != sizeof(size_t))
      throw dberror(0, "dbval::uint wrong size");
    return *static_cast<size_t *>(mv_data);
  }

  //assumes we don't copy db contents to different endian system
  uint64_t uint64() const {
    if (mv_size != sizeof(uint64_t)) {
      throw dberror(0, "dbval::uint64 wrong size");
    }
    return *static_cast<uint64_t *>(mv_data);
  }

  friend bool operator==(const dbval &a, const dbval &b) {
    return a.mv_size == b.mv_size &&
      !std::memcmp(a.mv_data, b.mv_data, a.mv_size);
  }
  friend bool operator!=(const dbval &a, const dbval &b) {
    return !(a == b);
  }
};

struct dbuint {
  size_t val_;
  explicit dbuint(size_t v) : val_(v) {}
  operator dbval() const { return dbval{val_}; }
};

struct dbenv : unique_destructor_t<mdb_env_close> {
  static void check(int err, const char *msg = nullptr) {
    if (err)
      throw dberror(err, msg);
  }

  static bool check_retry_eagain(int err, const char* msg = nullptr) {
    if (err) {
      if (err == EAGAIN) {
        return true;
      }
      throw dberror(err, msg);
    }
    return false;
  }

  dbenv(size_t mapsize);// = 0x10000000000);
  dbenv(dbenv &&) = default;
  dbenv &operator=(dbenv &&) = default;
  void open(const char *path, unsigned flags = 0, mdb_mode_t mode = 0666);

  void sync();

  using dbi = MDB_dbi;

  struct cursor {
    using kv_t = std::pair<dbval, dbval>;

    MDB_cursor *c_ = nullptr;
    kv_t kv_;
    int err_ = MDB_NOTFOUND;

    explicit cursor(MDB_cursor *c) : c_(c) {}
    cursor(cursor &&other) {
      c_ = other.c_;
      kv_ = other.kv_;
      err_ = other.err_;
      other.c_ = nullptr;
    }
    ~cursor() { close(); }
    cursor &operator=(cursor &&other) {
      close();
      c_ = other.c_;
      kv_ = other.kv_;
      err_ = other.err_;
      other.c_ = nullptr;
      return *this;
    }
    void close() {
      if (c_) {
        mdb_cursor_close(c_);
        c_ = nullptr;
      }
    }
    const kv_t &operator*() const {
      check (err_, "mdb cursor dereference");
      return kv_;
    }
    bool get(MDB_cursor_op op);
    bool get(MDB_cursor_op op, dbval key);
    cursor &operator++() { get(MDB_NEXT); return *this; }
    cursor &operator--() { get(MDB_PREV); return *this; }
    explicit operator bool() const { return !err_; }

    void del() {
      check(err_ = mdb_cursor_del(c_, 0), "mdb_cursor_del");
    }

    struct iterator {
      cursor *const c_;
      constexpr explicit iterator (std::nullptr_t) : c_(nullptr) {}
      constexpr iterator (cursor &c) : c_(&c) {}
      iterator &operator++() { ++*c_; return *this; }
      const kv_t &operator*() { return **c_; }
      friend bool operator==(const iterator &a, const iterator &b) {
        return a.c_->err_ && !b.c_;  // Seems to only work for forward iteration.
      }
      friend bool operator!=(const iterator &a, const iterator &b) {
        return !(a == b);
      }
    };

    // This allows range-for loops, but note that they actually change
    // the underlying cursor.
    iterator begin() { get(MDB_FIRST); return {*this}; }
    static constexpr iterator end() { return iterator{nullptr}; }
  };

  struct txn {
    MDB_txn *tx_;
    explicit txn(MDB_txn *tx = nullptr) : tx_(tx) {}
    txn(txn &&t) : tx_(t.tx_) { t.tx_ = nullptr; }
    txn &operator=(txn &&t) {
      abort();
      tx_ = t.tx_;
      t.tx_ = nullptr;
      return *this;
    }
    ~txn() { abort(); }
    void abort() {
      if (tx_) {
        mdb_txn_abort(tx_);
        tx_ = nullptr;
      }
    }
    void commit() {
      int err = mdb_txn_commit(tx_);
      tx_ = nullptr;
      check(err, "mdb_txn_commit");
    }

    MDB_stat stat(MDB_dbi dbi) {
      MDB_stat out;
      check(mdb_stat(tx_, dbi, &out), "mdb_stat");
      return out;
    }

    // Open a named database.  Note that as long as this transaction
    // commits, dbi continues to be valid and also does not need to be
    // garbage-collected (since it's just an integer).
    dbi open(const char *name = nullptr, unsigned flags = 0) const;

    // Look up a key in the database.  Returns std::nullopt if the key
    // is not found in the database.  Throws dberror on any other
    // failure condition.
    dbval::opt get(dbi db, dbval key) const;

    cursor cursor_open(dbi db) const {
      MDB_cursor *c;
      check(mdb_cursor_open(tx_, db, &c));
      return cursor{c};
    }
  };

  struct wtxn : txn {
    explicit wtxn(MDB_txn *tx = nullptr) : txn(tx) {}
    wtxn(wtxn &&t) : txn(std::move(t)) {}
    wtxn &operator=(wtxn &&t) { txn::operator=(std::move(t)); return *this; }

    // Start a nested transaction.
    wtxn wbegin() const;

    // Throws an exceptin on any failure.
    void put(dbi, dbval key, dbval val, unsigned flags = 0) const;
    // Throws an exception on failures except MDB_NOOVERWRITE, in
    // which case returns false and val is updated to existing value.
    bool tryput(dbi, dbval key, dbval *val, unsigned flags = 0) const;

    // Remove a key from the database.
    bool del(dbi db, dbval key) const;
    bool del(dbi db, dbval key, dbval val) const;
  };

  // Begin read-only transaction.
  txn rbegin() const;
  // Begin read-write transaction.
  wtxn wbegin() const;
};



namespace edce {

enum LMDBCommitmentState {
  BLOCK_END = 0,
  WORKUNIT_POST_INITIAL_COMMIT = 1
};

struct LMDBInstance {
  dbenv env;
  MDB_dbi dbi;
  bool env_open;

  MDB_dbi metadata_dbi;

  uint64_t persisted_round_number; // state up to and including persisted_round_number has been persisted.
  //Starts at 0 - means that we can't have a block number 0, except as a base/bot instance

  LMDBCommitmentState commitment_state;

  const uint64_t& get_persisted_round_number() const {
    return persisted_round_number;
  }

  LMDBInstance(std::size_t mapsize = 0x1000000000) : env{mapsize}, dbi{0}, env_open{false}, persisted_round_number(0) {};

  void open_env(const std::string path, unsigned flags = DEFAULT_LMDB_FLAGS, mdb_mode_t mode = 0666) {
    env.open(path.c_str(), flags, mode);
    env_open = true;
  }

  void create_db(const char* name);
  void open_db(const char* name);

  MDB_stat stat() {
    auto rtx = env.rbegin();
    auto stat = rtx.stat(dbi);
    rtx.abort();
    return stat;
  }

  operator bool() {
    return env_open;
  }

  dbenv::wtxn wbegin() {
    return env.wbegin();
  }

  dbenv::txn rbegin() {
    return env.rbegin();
  }

  const MDB_dbi& get_data_dbi() {
    return dbi;
  }

  void sync() {
    env.sync();
  }

  void commit_wtxn(dbenv::wtxn& txn, uint64_t persisted_round, bool do_sync = true, LMDBCommitmentState = LMDBCommitmentState::BLOCK_END);
};

template<typename ret_type>
struct generic_success {
};

template<>
struct generic_success<TransactionProcessingStatus> {
  static constexpr TransactionProcessingStatus success() {
    return TransactionProcessingStatus::SUCCESS;
  }
};

template<>
struct generic_success<bool> {
  static constexpr bool success() {
    return true;
  }
};

template<>
struct generic_success<void> {
  static constexpr void success() {}
};


template<typename wrapped_type>
struct LMDBLoadingWrapper {
  wrapped_type wrapped;
  const bool do_operations;

  template<auto func, typename... Args>
  auto
  generic_do(Args... args) {
    using ret_type = decltype((wrapped.*func)(args...));
    if (do_operations) {
      return (wrapped.*func)(args...);
    }
    return generic_success<ret_type>::success();
  }

  template<typename... Args>
  LMDBLoadingWrapper(const uint64_t current_block_number, Args&... args)
    : wrapped(args...)
    , do_operations(wrapped.get_persisted_round_number() < current_block_number) {
      //std::printf("%lu %lu\n", wrapped.get_persisted_round_number(), current_block_number);
     // std::printf("do_operations: %d\n", do_operations);
    }
};

} /* edce */
