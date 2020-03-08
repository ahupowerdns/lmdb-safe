#include "lmdb-typed.hh"
using namespace lmdb_safe;
unsigned int MDBGetMaxID(MDBRWTransaction& txn, MDBDbi& dbi)
{
  auto cursor = txn->getRWCursor(dbi);
  MDBOutVal maxidval, maxcontent;
  unsigned int maxid{0};
  if(!cursor.get(maxidval, maxcontent, MDB_LAST)) {
    maxid = maxidval.get<unsigned int>();
  }
  return maxid;
}


