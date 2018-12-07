#include "lmdb-safe.hh"
#include <fcntl.h>
#include <mutex>
#include <memory>
#include <sys/stat.h>
#include <string.h>
#include <map>

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
      cout<<"Making a fresh one, file did not exist yet"<<endl;
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
    cout<<"Found something!"<<endl;
    
    auto sp = iter->second.wp.lock();
    if(sp) {
      if(iter->second.flags != flags)
        throw std::runtime_error("Can't open mdb with differing flags");

      cout<<"It was live!"<<endl;
      return sp;
    }
    else {
      cout<<"It was dead already"<<endl;
      s_envs.erase(iter); // useful if make_shared fails
    }
  }
  else
    cout<<"Found nothing"<<endl;
  cout<<"Making a fresh one"<<endl;
  auto fresh = std::make_shared<MDBEnv>(fname, mode, flags);
  s_envs[key] = {fresh, flags};
  
  return fresh;
}


MDBDbi MDBEnv::openDB(const char* dbname, int flags)
{
  unsigned int envflags;
  mdb_env_get_flags(d_env, &envflags);
  
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
