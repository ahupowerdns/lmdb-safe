#include <arpa/inet.h>
#include <atomic>
#include <string.h>
#include "lmdb-safe.hh"
#include <unistd.h>
#include <thread>
#include <vector>

static void closeTest()
{
  auto env = getMDBEnv("./database", 0, 0600);
  
  int c =  MDB_CREATE;
  MDBDbi dbi = env->openDB("ahu", c);
  MDBDbi main = env->openDB(0, c);
  MDBDbi hyc = env->openDB("hyc", c);

  auto txn = env->getROTransaction();
  for(auto& d : {&main, &dbi, &hyc}) {
    auto rocursor = txn.getCursor(*d);
    MDB_val key{0,0}, data{0,0};
    if(rocursor.get(key, data, MDB_FIRST))
      continue;
    int count=0;
    do {
      count++;
    }while(!rocursor.get(key, data, MDB_NEXT));
    cout<<"Have "<<count<<" entries"<<endl;
  }
  
  return;
}


void doPuts(int tid)
try
{
  auto env = getMDBEnv("./database", 0, 0600);
  MDBDbi dbi = env->openDB("ahu", MDB_CREATE);
  for(int n=0; n < 15; ++n) {
    auto txn = env->getRWTransaction();
    int val = n + 1000*tid;
    txn.put(dbi, {sizeof(val), (char*)&val},
            {sizeof(val), (char*)&val});
    txn.commit();
    cout << "Done with transaction "<<n<<" in thread " << tid<<endl;
  }
  cout<<"Done with thread "<<tid<<endl;
}
catch(std::exception& e)
{
  cout<<"in thread "<<tid<<": "<<e.what()<<endl;
  throw;
}

void doGets(int tid)
try
{
  auto env = getMDBEnv("./database", 0, 0600);
  MDBDbi dbi = env->openDB("ahu", MDB_CREATE);
  for(int n=0; n < 15; ++n) {
    auto txn = env->getROTransaction();
    int val = n + 1000*tid;
    MDB_val res;
    if(txn.get(dbi, {sizeof(val), (char*)&val},
               res)) {
      throw std::runtime_error("no record");
    }
    
    cout << "Done with readtransaction "<<n<<" in thread " << tid<<endl;
  }
  cout<<"Done with read thread "<<tid<<endl;
}
catch(std::exception& e)
{
  cout<<"in thread "<<tid<<": "<<e.what()<<endl;
  throw;
}

struct MDBVal
{
  MDBVal(unsigned int v) : d_v(v)
  {
    d_mdbval.mv_size=sizeof(v);
    d_mdbval.mv_data = &d_v;
  }
  operator const MDB_val&()
  {
    return d_mdbval;
  }
  unsigned int d_v;
  MDB_val d_mdbval;
};


void doFill()
{
  auto env = getMDBEnv("./database", 0, 0600);
  MDBDbi dbi = env->openDB("ahu", MDB_CREATE);

  for(int n = 0; n < 20; ++n) {
    auto txn = env->getRWTransaction();
    for(int j=0; j < 1000000; ++j) {
      MDBVal mv(n*1000000+j);
      txn.put(dbi, mv, mv, 0);
    }
    txn.commit();
  }
  cout<<"Done filling"<<endl;
}

void doMeasure()
{
  auto env = getMDBEnv("./database", 0, 0600);
  MDBDbi dbi = env->openDB("ahu", MDB_CREATE);

  for(;;) {
    for(int n = 0; n < 20; ++n) {
      auto txn = env->getROTransaction();
      unsigned int count=0;
      for(int j=0; j < 1000000; ++j) {
        MDBVal mv(n*1000000+j);
        MDB_val res;
        if(!txn.get(dbi, mv, res))
          ++count;
      }
      cout<<count<<" ";
      cout.flush();
      if(!count)
        break;
    }
    cout<<endl;
  }
}

int main(int argc, char** argv)
{
  std::thread t1(doMeasure);
  std::thread t2(doFill);

  t1.join();
  t2.join();

}
/*
  auto env = getMDBEnv("./database", 0, 0600);
  MDBDbi dbi = env->openDB("ahu", MDB_CREATE);
  vector<std::thread> threads;
  for(int n=0; n < 100; ++n) {
    std::thread t(doPuts, n);
    threads.emplace_back(std::move(t));
  }

  for(auto& t: threads) {
    t.join();
  }

  threads.clear();

  for(int n=0; n < 100; ++n) {
    std::thread t(doGets, n);
    threads.emplace_back(std::move(t));
  }

  for(auto& t: threads) {
    t.join();
  }

  
  return 0;
}

  
  closeTest();
  auto env = getMDBEnv("./database", 0, 0600);
  
  MDB_stat stat;
  mdb_env_stat(*env.get(), &stat);
  cout << stat.ms_entries<< " entries in database"<<endl;
  
  MDBDbi dbi = env->openDB("ahu", MDB_CREATE);

  {
    MDBROTransaction rotxn = env->getROTransaction();

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

  auto txn = env->getRWTransaction();

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
  txn = env->getRWTransaction();
  
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
*/
