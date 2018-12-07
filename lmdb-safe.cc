#include "lmdb-safe.hh"

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
