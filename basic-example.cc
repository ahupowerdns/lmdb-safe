#include "lmdb-safe.hh"

void checkLMDB(MDBEnv* env, MDBDbi dbi)
{
  auto rotxn = env->getROTransaction();
  string_view data;
  if(!rotxn.get(dbi, "lmdb", data)) {
    cout<< "Outside RW transaction, found that lmdb = " << data <<endl;
  }
  else
    cout<<"Outside RW transaction, found nothing" << endl;
}

int main()
{
  auto env = getMDBEnv("./database", 0, 0600);
  auto dbi = env->openDB("example", MDB_CREATE);
  
  auto txn = env->getRWTransaction();
  txn.put(dbi, "lmdb", "great");

  string_view data;
  if(!txn.get(dbi, "lmdb", data)) {
    cout<< "Within RW transaction, found that lmdb = " << data <<endl;
  }
  else
    cout<<"Found nothing" << endl;

  std::thread elsewhere(checkLMDB, env.get(), dbi);
  elsewhere.join();
  
  txn.commit();
  cout<<"Committed data"<<endl;
  
  checkLMDB(env.get(), dbi);
  
}
