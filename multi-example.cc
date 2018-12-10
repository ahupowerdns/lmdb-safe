#include "lmdb-safe.hh"

int main()
{
  auto env = getMDBEnv("multi",  0, 0600);
  auto dbi = env->openDB("qnames", MDB_DUPSORT | MDB_CREATE);

  auto txn = env->getRWTransaction();
  txn.clear(dbi);

  txn.put(dbi, "bdb", "old");
  txn.put(dbi, "lmdb", "hot");
  txn.put(dbi, "lmdb", "fast");
  txn.put(dbi, "lmdb", "zooms");
  txn.put(dbi, "lmdb", "c");
  txn.put(dbi, "mdb", "old name");

  std::string_view v1;
  if(!txn.get(dbi, "mdb", v1)) {
    cout<<v1<<endl;
  }
  else {
    cout << "found nothing" << endl;
  }
  txn.commit();
  
  txn = env->getRWTransaction();
  auto cursor = txn.getCursor(dbi);
  int count=0;

  MDBOutVal key, data;
  MDBInVal start("lmdb");
  key.d_mdbval = start.d_mdbval;

  int rc=0;
  while(!(rc=cursor.get(key, data, count ? MDB_NEXT_DUP : MDB_SET))) {
    cout << key.get<string_view>() << " = " << data.get<string_view>() <<endl;
    ++count;
  }
}
