#include "lmdb-safe.hh"
#include <iostream>

using namespace std;

void countDB(MDBEnv& env, MDBROTransaction& txn, const std::string& dbname)
{
  auto db = txn.openDB(dbname, 0);
  auto cursor = txn.getCursor(db);
  uint32_t count = 0;
  MDBOutVal key, val;
  while(!cursor.get(key, val, count ? MDB_NEXT : MDB_FIRST)) {
    cout << key.get<string>();
    if(key.d_mdbval.mv_size == 4)
      cout << " " << key.get<uint32_t>();
    cout<<": " << val.get<std::string>();
    cout << "\n";
    ++count;
    
  }
  cout <<count<<endl;
}

int main(int argc, char** argv)
{
  MDBEnv env(argv[1], MDB_RDONLY | MDB_NOSUBDIR, 0600);
  auto main = env.openDB("", 0);
  auto txn = env.getROTransaction();

  auto cursor = txn.getCursor(main);

  MDBOutVal key, val;
  if(cursor.get(key, val, MDB_FIRST)) {
    cout << "Database is empty" <<endl;
  }
  do {
    cout << key.get<string>() << endl;
    countDB(env, txn, key.get<string>());
  } while(!cursor.get(key, val, MDB_NEXT));
  
  
}
