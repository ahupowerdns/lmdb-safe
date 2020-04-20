#include "lmdb-safe.hh"
using namespace std;
using namespace lmdb_safe;

void checkLMDB(MDBEnv* env, MDBDbi dbi)
{
  auto rotxn = env->getROTransaction();
  MDBOutVal data;
  if(!rotxn->get(dbi, "lmdb", data)) {
    cout<< "Outside RW transaction, found that lmdb = " << data.get<string_view>() <<endl;
  }
  else
    cout<<"Outside RW transaction, found nothing" << endl;
}

int main()
{
  auto env = getMDBEnv("./database", 0, 0600);
  auto dbi = env->openDB("example", MDB_CREATE);
  
  auto txn = env->getRWTransaction();
  mdb_drop(*txn, dbi, 0);
  txn->put(dbi, "lmdb", "great");

  MDBOutVal data;
  if(!txn->get(dbi, "lmdb", data)) {
    cout<< "Within RW transaction, found that lmdb = " << data.get<string_view>() <<endl;
  }
  else
    cout<<"Found nothing" << endl;

  std::thread elsewhere(checkLMDB, env.get(), dbi);
  elsewhere.join();
  
  txn->commit();
  
  cout<<"Committed data"<<endl;
  
  checkLMDB(env.get(), dbi);
  txn = env->getRWTransaction();
  mdb_drop(*txn, dbi, 0);
  txn->commit();
}
