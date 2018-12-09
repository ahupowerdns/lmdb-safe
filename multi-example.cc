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
  MDB_val key{4, (char*)"lmdb"};
  MDB_val data{0,0};
  int rc=0;
  while(!(rc=cursor.get(key, data, count ? MDB_NEXT_DUP : MDB_SET))) {
    std::string_view k((char*)key.mv_data, key.mv_size);
    std::string_view v((char*)data.mv_data, data.mv_size);
    cout << k << " = " << v <<endl;
    ++count;
  }
}
