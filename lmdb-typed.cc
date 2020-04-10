#include "lmdb-typed.hh"

unsigned int lmdb_safe::MDBGetMaxID(lmdb_safe::MDBRWTransaction& txn, lmdb_safe::MDBDbi& dbi)
{
  auto cursor = txn->getRWCursor(dbi);
  MDBOutVal maxidval, maxcontent;
  unsigned int maxid{0};
  if(!cursor.get(maxidval, maxcontent, MDB_LAST)) {
    maxid = maxidval.get<unsigned int>();
  }
  return maxid;
}


