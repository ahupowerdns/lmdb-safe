#include <arpa/inet.h>
#include <atomic>
#include <string.h>
#include "lmdb-safe.hh"
#include <unistd.h>

static void closeTest()
{
  MDBEnv env("./database", MDB_RDONLY, 0600);
  int c=  MDB_CREATE;
  MDBDbi dbi = env.openDB("ahu", c);
  MDBDbi main = env.openDB(0, c);
  MDBDbi hyc = env.openDB("hyc2", c);

  auto txn = env.getROTransaction();
  auto cursor = txn.getCursor(dbi);
  
  return;
}


int main(int argc, char** argv)
{
  closeTest();
  return 0;
  MDBEnv env("./database", 0, 0600);
  
  MDB_stat stat;
  mdb_env_stat(env, &stat);
  cout << stat.ms_entries<< " entries in database"<<endl;
  
  MDBDbi dbi = env.openDB("ahu", MDB_CREATE);

  {
    MDBROTransaction rotxn = env.getROTransaction();

    {
      auto rocursor = rotxn.getCursor(dbi);
      MDB_val key{0,0}, data{0,0};
      rocursor.get(key, data, MDB_FIRST);
      int count=0;
      do {
        count++;
      }while(!rocursor.get(key, data, MDB_NEXT));
      
      cout<<"Counted "<<count<<" entries"<<endl;
    }
    
    int found{0}, notfound{0};
    for(unsigned n=0; n < 20000000; ++n) {
      unsigned int store = htonl(n);
      MDB_val data;
      int rc = rotxn.get(dbi, {sizeof(store), (char*)&store},
                         data);
      if(!rc)
        found++;
      else if(rc == MDB_NOTFOUND)
        notfound++;
      else
        throw std::runtime_error("error");
      
      rotxn.reset();
      rotxn.renew();
      if(!(n % 1024000))
        cout << n << " " <<found<< " " << notfound <<endl;
    }
    cout<<"Found "<<found<<", notfound: "<<notfound<<endl;

  }

  auto txn = env.getRWTransaction();

  auto cursor = txn.getCursor(dbi);
  
  time_t start=time(0);
  ofstream delplot("plot");

  
  for(unsigned n=0; n < 20000000*8; ++n) {
    unsigned int store = htonl(n); 
    txn.del(dbi, {sizeof(store), (char*)&store});
    if(!(n % (1024*1024))) {
      cout << time(0)- start << '\t' << n << endl;
      delplot << time(0)- start << '\t' << n << endl;
    }
  }
  cout<<"Done deleting, committing"<<endl;
  txn.commit();
  cout<<"Done with commit"<<endl;
  txn = env.getRWTransaction();
  
  start=time(0);
  ofstream plot("plot");
  for(unsigned n=0; n < 20000000*8; ++n) {
    int res = n*n;
    unsigned int store = htonl(n); 
    txn.put(dbi, {sizeof(store), (char*)&store},
            {sizeof(res), (char*)&res}, MDB_APPEND);

    if(!(n % (1024*1024))) {
      cout << time(0)- start << '\t' << n << endl;
      plot << time(0)- start << '\t' << n << endl;
    }
  }
  txn.commit();
}
