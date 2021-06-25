
#include <string_view>

#include "lmdb_wrapper.h"
#include "price_utils.h"

#include "simple_debug.h"

#include <chrono>
#include <thread>

std::string
hexdump(const void *data0, size_t len)
{
  auto data = static_cast<const unsigned char *>(data0);
  static constexpr char hexdigits[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(2*len);
  for (size_t i = 0; i < len; i++) {
    ret += hexdigits[data[i]>>4 & 0xf];
    ret += hexdigits[data[i] & 0xf];
  }
  return ret;
}

std::string
dberror::mkWhat(int code, const char *msg)
{
  std::string ret;
  if (msg)
    ret = msg;
  if (code) {
    if (!ret.empty())
      ret += ": ";
    ret += mdb_strerror(code);
  }
  if (ret.empty())
    ret = "dberror";
  return ret;
}

std::string
dbval::hex() const
{
  return hexdump(mv_data, mv_size);
}

dbenv::dbenv(size_t mapsize) {
  MDB_env *env;
  check(mdb_env_create(&env), "mdb_env_create");
  reset(env);
  mdb_env_set_maxdbs(env, 50);
  mdb_env_set_mapsize(env, mapsize);
}

void
dbenv::open(const char *path, unsigned flags, mdb_mode_t mode)
{
  while (check_retry_eagain(mdb_env_open(get(), path, flags, mode), path)) {
    ERROR("EAGAIN when creating %s, retrying (sleeping 1s)", path);
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1000ms);
  }
}

void
dbenv::sync() {
  check(mdb_env_sync(get(), 1));
}

dbenv::txn
dbenv::rbegin() const
{
  MDB_txn *tx;
  check(mdb_txn_begin(get(), nullptr, MDB_RDONLY, &tx), "mdb_txn_begin");
  return txn{tx};
}

dbenv::wtxn
dbenv::wbegin() const
{
  MDB_txn *tx;
  check(mdb_txn_begin(get(), nullptr, 0, &tx), "mdb_txn_begin");
  return wtxn{tx};
}

dbenv::wtxn
dbenv::wtxn::wbegin() const
{
  MDB_txn *tx;
  check(mdb_txn_begin(mdb_txn_env(tx_), tx_, 0, &tx), "mdb_txn_begin");
  return wtxn{tx};
}

dbenv::dbi
dbenv::txn::open(const char *name, unsigned flags) const
{
  dbi ret;
  check(mdb_dbi_open(tx_, name, flags, &ret), name ? name : "mdb_dbi_open");
  return ret;
}

dbval::opt
dbenv::txn::get(dbi db, dbval key) const
{
  dbval ret;
  switch (int code = mdb_get(tx_, db, &key, &ret)) {
  case 0:
    return ret;
  case MDB_NOTFOUND:
    return std::nullopt;
  default:
    throw dberror(code, "mdb_db_get");
  }
}

void
dbenv::wtxn::put(dbi db, dbval key, dbval val, unsigned flags) const
{
  check (mdb_put(tx_, db, &key, &val, flags), "mdb_db_put");
}

bool
dbenv::wtxn::tryput(dbi db, dbval key, dbval *val, unsigned flags) const
{
  switch (int code = mdb_put(tx_, db, &key, val, flags)) {
  case 0:
    return true;
  case MDB_KEYEXIST:
    return false;
  default:
    throw dberror(code, "mdb_db_put");
  }
}

bool
dbenv::wtxn::del(dbi db, dbval key) const
{
  switch (int code = mdb_del(tx_, db, &key, nullptr)) {
  case 0:
    return true;
  case MDB_NOTFOUND:
    return false;
  default:
    throw dberror(code, "mdb_del");
  }
}

bool
dbenv::wtxn::del(dbi db, dbval key, dbval val) const
{
  switch (int code = mdb_del(tx_, db, &key, &val)) {
  case 0:
    return true;
  case MDB_NOTFOUND:
    return false;
  default:
    throw dberror(code, "mdb_del");
  }
}

bool
dbenv::cursor::get(MDB_cursor_op op)
{
  switch (err_ = mdb_cursor_get(c_, &kv_.first, &kv_.second, op)) {
  case 0:
    return true;
  case MDB_NOTFOUND:
    return false;
  default:
    throw dberror(err_, "mdb_cursor_get");
  }
}

bool
dbenv::cursor::get(MDB_cursor_op op, dbval key)
{
  kv_.first = key;
  return get(op);
}



namespace edce {

  void LMDBInstance::open_db(const char* name) {
    if (!env_open) {
      throw std::runtime_error("env not open");
    }

    auto rtx = env.rbegin();

    dbi = rtx.open(name);
    metadata_dbi = rtx.open("metadata");

    auto persisted_round_number_opt = rtx.get(metadata_dbi, dbval("persisted block"));
    if (!persisted_round_number_opt) {
      throw std::runtime_error("missing metadata contents");
    }

    persisted_round_number = persisted_round_number_opt -> uint64();
    rtx.commit();
  }

  void LMDBInstance::create_db(const char* name) {
    if (!env_open) {
      throw std::runtime_error("env not open");
    }
    auto wtx = env.wbegin();
    dbi = wtx.open(name, MDB_CREATE);
    metadata_dbi = wtx.open("metadata", MDB_CREATE);
    wtx.commit();

    persisted_round_number = 0;
    commitment_state = LMDBCommitmentState::BLOCK_END;
  }

  void LMDBInstance::commit_wtxn(dbenv::wtxn& txn, uint64_t persisted_round, bool do_sync, LMDBCommitmentState state) {

    //unsigned char round_buf[sizeof(uint64_t)];

    if (persisted_round < persisted_round_number) {
      throw std::runtime_error("can't overwrite later round with earlier round");
    }

    persisted_round_number = persisted_round;

    if (!env_open) {
      return;
    }

    //std::printf("persisting lmdb to round %lu (prev: %lu)\n", persisted_round, persisted_round_number);

   // PriceUtils::write_unsigned_big_endian(round_buf, persisted_round);
    txn.put(metadata_dbi, dbval("persisted block"), dbval(&persisted_round, sizeof(uint64_t)));
    txn.put(metadata_dbi, dbval("persisted state"), dbval(&state, 1));
    txn.commit();

  //  persisted_round_number = persisted_round;

    if (do_sync) { 
      sync();
    }
  }

} /* edce */
