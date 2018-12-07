#include <lmdb.h>
#include <iostream>
#include <fstream>
#include <arpa/inet.h>
#include <atomic>
#include <string.h>

using namespace std;

/* open issues:
 *
 * - opening a DBI is still exceptionally painful to get right, especially in a 
 *   multi-threaded world
 * - we're not yet protecting you against opening a file twice
 * - we are not yet protecting you correctly against opening multiple transactions in 1 thread
 * - error reporting is bad
 * - missing convenience functions (string_view, string)
 */ 

class MDBDbi
{
public:
  
  explicit MDBDbi(MDB_env* env, MDB_txn* txn, const char* dbname, int flags)
    : d_env(env), d_txn(txn)
  {

    // A transaction that uses this function must finish (either commit or abort) before any other transaction in the process may use this function.
    
    if(mdb_dbi_open(txn, dbname, flags, &d_dbi))
      throw std::runtime_error("Unable to open database");

    // Database names are keys in the unnamed database, and may be read but not written.
    
  }

  MDBDbi(MDBDbi&& rhs)
  {
    d_dbi = rhs.d_dbi;
    d_env = rhs.d_env;
    d_txn = rhs.d_txn;
    rhs.d_env = 0;
    rhs.d_txn = 0;
  }
  
  ~MDBDbi()
  {
    if(d_env)
      mdb_dbi_close(d_env, d_dbi);
  }

  operator const MDB_dbi&() const
  {
    return d_dbi;
  }
  
  MDB_dbi d_dbi;
  MDB_env* d_env;
  MDB_txn* d_txn;
};

class MDBTransaction;
class MDBROTransaction;

class MDBEnv
{
public:
  MDBEnv(const char* fname, int mode, int flags)
  {
    mdb_env_create(&d_env);   // there is no close
    if(mdb_env_set_mapsize(d_env, 4096*2000000ULL))
      throw std::runtime_error("setting map size");

    /*
Various other options may also need to be set before opening the handle, e.g. mdb_env_set_mapsize(), mdb_env_set_maxreaders(), mdb_env_set_maxdbs(),
    */

    mdb_env_set_maxdbs(d_env, 128);

    // TODO: check if fname is open somewhere already (under lock)
    
    if(mdb_env_open(d_env, fname, mode, flags)) {
      // If this function fails, mdb_env_close() must be called to discard the MDB_env handle.
      mdb_env_close(d_env);
      throw std::runtime_error("Unable to open database");
    }
  }

  ~MDBEnv()
  {
    //    Only a single thread may call this function. All transactions, databases, and cursors must already be closed before calling this function
    mdb_env_close(d_env);
    // but, elsewhere, docs say database handles do not need to be closed?
  }
         
  
  MDBTransaction getTransaction();
  MDBROTransaction getROTransaction();
  operator MDB_env*& ()
  {
    return d_env;
  }
  MDB_env* d_env;
  bool d_transactionOut{false};
};

class MDBROCursor;

  

class MDBROTransaction
{
public:
  explicit MDBROTransaction(MDBEnv* parent, int flags=0) : d_parent(parent)
  {
    if(d_parent->d_transactionOut)
      throw std::runtime_error("Duplicate transaction");

    /*
    A transaction and its cursors must only be used by a single thread, and a thread may only have a single transaction at a time. If MDB_NOTLS is in use, this does not apply to read-only transactions. */
    
    if(mdb_txn_begin(d_parent->d_env, 0, MDB_RDONLY | flags, &d_txn))
      throw std::runtime_error("Unable to start RO transaction");
  }

  MDBROTransaction(MDBROTransaction&& rhs)
  {
    d_parent = rhs.d_parent;
    d_txn = rhs.d_txn;
    rhs.d_parent = 0;
    rhs.d_txn = 0;
  }

  void reset()
  {
    // this does not free cursors
    mdb_txn_reset(d_txn);
    d_parent->d_transactionOut=false;
  }

  void renew()
  {
    if(d_parent->d_transactionOut)
      throw std::runtime_error("Duplicate transaction");
    d_parent->d_transactionOut=1;
    if(mdb_txn_renew(d_txn))
      throw std::runtime_error("Renewing transaction");
  }
  

  int get(MDB_dbi dbi, const MDB_val& key, MDB_val& val)
  {
    return mdb_get(d_txn, dbi, (MDB_val*)&key, &val);
  }

  
  MDBDbi openDB(const char* dbname, int flags)
  {
    return MDBDbi(d_parent->d_env, d_txn, dbname, flags);
  }

  MDBROCursor getCursor(const MDBDbi&);
  
  
  ~MDBROTransaction()
  {
    if(d_txn) {
      d_parent->d_transactionOut=false;
      mdb_txn_abort(d_txn);
    }
  }

  operator MDB_txn*&()
  {
    return d_txn;
  }
  
  MDBEnv* d_parent;
  MDB_txn* d_txn;
};

class MDBROCursor
{
public:
  MDBROCursor(MDB_txn* txn, const MDB_dbi& dbi)
  {
    int rc= mdb_cursor_open(txn, dbi, &d_cursor);
    if(rc) {
      throw std::runtime_error("Error creating cursor: "+std::string(mdb_strerror(rc)));
    }
  }
  MDBROCursor(MDBROCursor&& rhs)
  {
    d_cursor = rhs.d_cursor;
    rhs.d_cursor=0;
  }

  void close()
  {
    mdb_cursor_close(d_cursor);
    d_cursor=0;
  }
  
  ~MDBROCursor()
  {
    if(d_cursor)
      mdb_cursor_close(d_cursor);
  }

  int get(MDB_val& key, MDB_val& data, MDB_cursor_op op)
  {
    return mdb_cursor_get(d_cursor, &key, &data, op);
  }
  
  MDB_cursor* d_cursor;
};


MDBROCursor MDBROTransaction::getCursor(const MDBDbi& dbi)
{
  return MDBROCursor(d_txn, dbi);
}

class MDBTransaction
{
public:
  explicit MDBTransaction(MDBEnv* parent, int flags=0) : d_parent(parent)
  {
    if(d_parent->d_transactionOut)
      throw std::runtime_error("Duplicate transaction");
    d_parent->d_transactionOut = true;
    if(mdb_txn_begin(d_parent->d_env, 0, flags, &d_txn))
      throw std::runtime_error("Unable to start transaction");
  }

  MDBTransaction(MDBTransaction&& rhs)
  {
    d_parent = rhs.d_parent;
    d_txn = rhs.d_txn;
    rhs.d_parent = 0;
    rhs.d_txn = 0;
  }

  MDBTransaction& operator=(MDBTransaction&& rhs)
  {
    if(d_txn)
      abort();

    d_parent = rhs.d_parent;
    d_txn = rhs.d_txn;
    rhs.d_parent = 0;
    rhs.d_txn = 0;
    
    return *this;
  }
  
  void commit()
  {
    if(mdb_txn_commit(d_txn)) {
      throw std::runtime_error("committing");
    }
    d_parent->d_transactionOut=false;

    d_txn=0;
  }

  void abort()
  {
    mdb_txn_abort(d_txn);
    d_txn = 0;
    d_parent->d_transactionOut=false;
  }

  void put(MDB_dbi dbi, const MDB_val& key, const MDB_val& val, int flags)
  {
    int rc;
    if((rc=mdb_put(d_txn, dbi, (MDB_val*)&key, (MDB_val*)&val, flags)))
      throw std::runtime_error("putting data: " + std::string(mdb_strerror(rc)));
  }


  int del(MDB_dbi dbi, const MDB_val& key)
  {
    int rc;
    rc=mdb_del(d_txn, dbi, (MDB_val*)&key, 0);
    if(rc && rc != MDB_NOTFOUND)
      throw std::runtime_error("deleting data: " + std::string(mdb_strerror(rc)));
    return rc;
  }

  
  int get(MDB_dbi dbi, const MDB_val& key, MDB_val& val)
  {
    return mdb_get(d_txn, dbi, (MDB_val*)&key, &val);
  }

  
  MDBDbi openDB(const char* dbname, int flags)
  {
    return MDBDbi(d_parent->d_env, d_txn, dbname, flags);
  }

  
  ~MDBTransaction()
  {
    if(d_txn) {
      d_parent->d_transactionOut=false;
      mdb_txn_abort(d_txn);
    }
  }

  operator MDB_txn*&()
  {
    return d_txn;
  }
  
  MDBEnv* d_parent;
  MDB_txn* d_txn;
};


MDBROTransaction MDBEnv::getROTransaction()
{
  cout << d_transactionOut << endl;
  return MDBROTransaction(this);
}
MDBTransaction MDBEnv::getTransaction()
{
  cout << d_transactionOut << endl;
  return MDBTransaction(this);
}



int main(int argc, char** argv)
{
  MDBEnv env("./database", 0, 0600);
  
  MDB_stat stat;
  mdb_env_stat(env, &stat);
  cout << stat.ms_entries<< " entries in database"<<endl;

  MDBTransaction txn = env.getTransaction(); 
  MDBDbi dbi = txn.openDB("ahu", MDB_CREATE);
  txn.commit();

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

  txn = env.getTransaction();

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
  txn = env.getTransaction();
  
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
