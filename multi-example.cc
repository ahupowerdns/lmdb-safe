#include "lmdb-safe.hh"
using namespace std;

#include <unistd.h>

int main()
{
  unlink("./multi");
  auto env = getMDBEnv("multi",  MDB_NOSUBDIR, 0600);
  auto dbi = env->openDB("qnames", MDB_DUPSORT | MDB_CREATE);

  auto txn = env->getRWTransaction();
  txn->clear(dbi);

  txn->put(dbi, "bdb", "old");
  txn->put(dbi, "lmdb", "hot");
  txn->put(dbi, "lmdb", "fast");
  txn->put(dbi, "lmdb", "zooms");
  txn->put(dbi, "lmdb", "c");
  txn->put(dbi, "mdb", "old name");

  string_view v1;
  if(!txn->get(dbi, "mdb", v1)) {
    cout<<v1<<endl;
  }
  else {
    cout << "found nothing" << endl;
  }
  txn->commit();
  
  txn = env->getRWTransaction();
  auto cursor = txn->getRWCursor(dbi);

  MDBOutVal key, data;

  for(int rc = cursor.find("lmdb", key, data); !rc; rc = cursor.get(key, data, MDB_NEXT_DUP)) {
    cout << key.get<string_view>() << " = " << data.get<string_view>() <<endl;    
  }

  cout<<"Dump of complete database: "<<endl;
  for(int rc = cursor.first(key, data); !rc; rc = cursor.next(key, data)) {
    cout << key.get<string_view>() << " = " << data.get<string_view>() <<endl;    
  }
  cout << "Done!" <<endl;

  cout << "Now going to delete 'lmdb' keys" << endl;

  for(int rc = cursor.first(key, data); !rc; rc = cursor.next(key, data)) {
    if(key.get<string_view>() == "lmdb")
      cursor.del();
  }

  cout <<"Complete database after deleting 'lmdb' keys: " << endl;
  
  for(int rc = cursor.first(key, data); !rc; rc = cursor.next(key, data)) {
    cout << key.get<string_view>() << " = " << data.get<string_view>() <<endl;    
  }
  
  
}
