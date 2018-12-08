#include "lmdb-safe.hh"
#include <fcntl.h>
#include <mutex>
#include <memory>
#include <sys/stat.h>
#include <string.h>
#include <map>

static string MDBError(int rc)
{
  return mdb_strerror(rc);
}

MDBDbi::MDBDbi(MDB_env* env, MDB_txn* txn, const char* dbname, int flags)
{
  // A transaction that uses this function must finish (either commit or abort) before any other transaction in the process may use this function.
  
  int rc = mdb_dbi_open(txn, dbname, flags, &d_dbi);
  if(rc)
    throw std::runtime_error("Unable to open named database: " + MDBError(rc));
  
  // Database names are keys in the unnamed database, and may be read but not written.
}

MDBEnv::MDBEnv(const char* fname, int mode, int flags)
{
  mdb_env_create(&d_env);   
  if(mdb_env_set_mapsize(d_env, 4ULL*4096*244140ULL)) // 4GB
    throw std::runtime_error("setting map size");

    /*
Various other options may also need to be set before opening the handle, e.g. mdb_env_set_mapsize(), mdb_env_set_maxreaders(), mdb_env_set_maxdbs(),
    */

  mdb_env_set_maxdbs(d_env, 128);

  // we need MDB_NOTLS since we rely on its semantics
  if(int rc=mdb_env_open(d_env, fname, mode, flags | MDB_NOTLS)) {
    // If this function fails, mdb_env_close() must be called to discard the MDB_env handle.
    mdb_env_close(d_env);
    throw std::runtime_error("Unable to open database file "+std::string(fname)+": " + MDBError(rc));
  }
}

void MDBEnv::incROTX()
{
  std::lock_guard<std::mutex> l(d_countmutex);
  ++d_ROtransactionsOut[std::this_thread::get_id()];
}

void MDBEnv::decROTX()
{
  std::lock_guard<std::mutex> l(d_countmutex);
  --d_ROtransactionsOut[std::this_thread::get_id()];
}

void MDBEnv::incRWTX()
{
  std::lock_guard<std::mutex> l(d_countmutex);
  ++d_RWtransactionsOut[std::this_thread::get_id()];
}

void MDBEnv::decRWTX()
{
  std::lock_guard<std::mutex> l(d_countmutex);
  --d_RWtransactionsOut[std::this_thread::get_id()];
}

int MDBEnv::getRWTX()
{
  std::lock_guard<std::mutex> l(d_countmutex);
  return d_RWtransactionsOut[std::this_thread::get_id()];
}
int MDBEnv::getROTX()
{
  std::lock_guard<std::mutex> l(d_countmutex);
  return d_ROtransactionsOut[std::this_thread::get_id()];
}


std::shared_ptr<MDBEnv> getMDBEnv(const char* fname, int mode, int flags)
{
  struct Value
  {
    weak_ptr<MDBEnv> wp;
    int flags;
  };
  
  static std::map<tuple<dev_t, ino_t>, Value> s_envs;
  static std::mutex mut;
  
  struct stat statbuf;
  if(stat(fname, &statbuf)) {
    if(errno != ENOENT)
      throw std::runtime_error("Unable to stat prospective mdb database: "+string(strerror(errno)));
    else {
      std::lock_guard<std::mutex> l(mut);
      auto fresh = std::make_shared<MDBEnv>(fname, mode, flags);
      if(stat(fname, &statbuf))
        throw std::runtime_error("Unable to stat prospective mdb database: "+string(strerror(errno)));
      auto key = std::tie(statbuf.st_dev, statbuf.st_ino);
      s_envs[key] = {fresh, flags};
      return fresh;
    }
  }

  std::lock_guard<std::mutex> l(mut);
  auto key = std::tie(statbuf.st_dev, statbuf.st_ino);
  auto iter = s_envs.find(key);
  if(iter != s_envs.end()) {
    auto sp = iter->second.wp.lock();
    if(sp) {
      if(iter->second.flags != flags)
        throw std::runtime_error("Can't open mdb with differing flags");

      return sp;
    }
    else {
      s_envs.erase(iter); // useful if make_shared fails
    }
  }

  auto fresh = std::make_shared<MDBEnv>(fname, mode, flags);
  s_envs[key] = {fresh, flags};
  
  return fresh;
}


MDBDbi MDBEnv::openDB(const char* dbname, int flags)
{
  unsigned int envflags;
  mdb_env_get_flags(d_env, &envflags);
  /*
    This function must not be called from multiple concurrent transactions in the same process. A transaction that uses this function must finish (either commit or abort) before any other transaction in the process may use this function.
  */
  std::lock_guard<std::mutex> l(d_openmut);
  
  if(!(envflags & MDB_RDONLY)) {
    auto rwt = getRWTransaction();
    MDBDbi ret  = rwt.openDB(dbname, flags);
    rwt.commit();
    return ret;
  }
  
  auto rwt = getROTransaction();
  return rwt.openDB(dbname, flags);
}

MDBRWCursor MDBRWTransaction::getCursor(const MDBDbi& dbi)
{
  return MDBRWCursor(this, dbi);
}

MDBROTransaction MDBEnv::getROTransaction()
{
  return MDBROTransaction(this);
}
MDBRWTransaction MDBEnv::getRWTransaction()
{
  return MDBRWTransaction(this);
}


void MDBRWTransaction::closeCursors()
{
  for(auto& c : d_cursors)
    c->close();
  d_cursors.clear();
}

MDBROCursor MDBROTransaction::getCursor(const MDBDbi& dbi)
{
  return MDBROCursor(this, dbi);
}

void MDBRWTransaction::put(MDB_dbi dbi, string_view key, string_view val, int flags)
{
  put(dbi, MDB_val{key.size(), (void*)&key[0]}, MDB_val{val.size(), (void*)&val[0]}, flags);
}

int MDBRWTransaction::get(MDB_dbi dbi, string_view key, string_view& val)
{
  MDB_val res;
  int rc = get(dbi, MDB_val{key.size(), (void*)&key[0]}, res);
  val=string_view((char*)res.mv_data, res.mv_size);
  return rc;
}

int MDBROTransaction::get(MDB_dbi dbi, string_view key, string_view& val)
{
  MDB_val res;
  int rc = get(dbi, MDB_val{key.size(), (void*)&key[0]}, res);
  val=string_view((char*)res.mv_data, res.mv_size);
  return rc;
}
