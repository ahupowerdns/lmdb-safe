#include <lmdb.h>
#include <iostream>
#include <fstream>
#include <set>
#include <map>
#include <thread>
#include <memory>
#include <mutex>

using namespace std;

/* open issues:
 *
 * - missing convenience functions (string_view, string)
 */ 

/*
The error strategy. Anything that "should never happen" turns into an exception. But things like 'duplicate entry' or 'no such key' are for you to deal with.
 */

/*
  Thread safety: we are as safe as lmdb. You can talk to MDBEnv from as many threads as you want 
*/

/** MDBDbi is our only 'value type' object, as 1) a dbi is actually an integer
    and 2) per LMDB documentation, we never close it. */
class MDBDbi
{
public:
  explicit MDBDbi(MDB_env* env, MDB_txn* txn, const char* dbname, int flags);  

  operator const MDB_dbi&() const
  {
    return d_dbi;
  }
  
  MDB_dbi d_dbi;
};

class MDBRWTransaction;
class MDBROTransaction;

class MDBEnv
{
public:
  MDBEnv(const char* fname, int mode, int flags);

  ~MDBEnv()
  {
    //    Only a single thread may call this function. All transactions, databases, and cursors must already be closed before calling this function
    mdb_env_close(d_env);
    // but, elsewhere, docs say database handles do not need to be closed?
  }

  MDBDbi openDB(const char* dbname, int flags);
  
  MDBRWTransaction getRWTransaction();
  MDBROTransaction getROTransaction();

  operator MDB_env*& ()
  {
    return d_env;
  }
  MDB_env* d_env;

  int getRWTX();
  void incRWTX();
  void decRWTX();
  int getROTX();
  void incROTX();
  void decROTX();
private:
  std::mutex d_mutex;
  std::map<std::thread::id, int> d_RWtransactionsOut;
  std::map<std::thread::id, int> d_ROtransactionsOut;
};

std::shared_ptr<MDBEnv> getMDBEnv(const char* fname, int mode, int flags);

class MDBROCursor;

class MDBROTransaction
{
public:
  explicit MDBROTransaction(MDBEnv* parent, int flags=0) : d_parent(parent)
  {
    if(d_parent->getRWTX())
      throw std::runtime_error("Duplicate transaction");

    /*
    A transaction and its cursors must only be used by a single thread, and a thread may only have a single transaction at a time. If MDB_NOTLS is in use, this does not apply to read-only transactions. */
    
    if(mdb_txn_begin(d_parent->d_env, 0, MDB_RDONLY | flags, &d_txn))
      throw std::runtime_error("Unable to start RO transaction");
    d_parent->incROTX();
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
    d_parent->decROTX();
  }

  void renew()
  {
    if(d_parent->getROTX())
      throw std::runtime_error("Duplicate transaction");
    if(mdb_txn_renew(d_txn))
      throw std::runtime_error("Renewing transaction");
    d_parent->incROTX();
  }
  

  int get(MDB_dbi dbi, const MDB_val& key, MDB_val& val)
  {
    if(!d_txn)
      throw std::runtime_error("Attempt to use a closed RO transaction for get");

    return mdb_get(d_txn, dbi, (MDB_val*)&key, &val);
  }
  int get(MDB_dbi dbi, string_view key, string_view& val);
  

  // this is something you can do, readonly
  MDBDbi openDB(const char* dbname, int flags)
  {
    return MDBDbi(d_parent->d_env, d_txn, dbname, flags);
  }

  MDBROCursor getCursor(const MDBDbi&);
    
  ~MDBROTransaction()
  {
    if(d_txn) {
      d_parent->decROTX();
      mdb_txn_commit(d_txn); // this appears to work better than abort for r/o database opening
    }
  }

  operator MDB_txn*&()
  {
    return d_txn;
  }
  
  MDBEnv* d_parent;
  MDB_txn* d_txn;
};

/* 
   A cursor in a read-only transaction must be closed explicitly, before or after its transaction ends. It can be reused with mdb_cursor_renew() before finally closing it. 

   "If the parent transaction commits, the cursor must not be used again."
*/

class MDBROCursor
{
public:
  MDBROCursor(MDBROTransaction* parent, const MDB_dbi& dbi) : d_parent(parent)
  {
    int rc= mdb_cursor_open(d_parent->d_txn, dbi, &d_cursor);
    if(rc) {
      throw std::runtime_error("Error creating RO cursor: "+std::string(mdb_strerror(rc)));
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
  MDBROTransaction* d_parent;
};



class MDBRWCursor;

class MDBRWTransaction
{
public:
  explicit MDBRWTransaction(MDBEnv* parent, int flags=0) : d_parent(parent)
  {
    if(d_parent->getROTX() || d_parent->getRWTX())
      throw std::runtime_error("Duplicate transaction");

    if(int rc=mdb_txn_begin(d_parent->d_env, 0, flags, &d_txn))
      throw std::runtime_error("Unable to start RW transaction: "+std::string(mdb_strerror(rc)));
    d_parent->incRWTX();
  }

  MDBRWTransaction(MDBRWTransaction&& rhs)
  {
    d_parent = rhs.d_parent;
    d_txn = rhs.d_txn;
    rhs.d_parent = 0;
    rhs.d_txn = 0;
  }

  MDBRWTransaction& operator=(MDBRWTransaction&& rhs)
  {
    if(d_txn)
      abort();

    d_parent = rhs.d_parent;
    d_txn = rhs.d_txn;
    rhs.d_parent = 0;
    rhs.d_txn = 0;
    
    return *this;
  }

  ~MDBRWTransaction()
  {
    if(d_txn) {
      d_parent->decRWTX();
      closeCursors();
      mdb_txn_abort(d_txn); // XXX check response?
    }
  }
  void closeCursors();
  
  void commit()
  {
    closeCursors();
    if(mdb_txn_commit(d_txn)) {
      throw std::runtime_error("committing");
    }
    d_parent->decRWTX();

    d_txn=0;
  }

  void abort()
  {
    closeCursors();
    mdb_txn_abort(d_txn); // XXX check error?
    d_txn = 0;
    d_parent->decRWTX();
  }

  void put(MDB_dbi dbi, const MDB_val& key, const MDB_val& val, int flags=0)
  {
    if(!d_txn)
      throw std::runtime_error("Attempt to use a closed RW transaction for put");
    int rc;
    if((rc=mdb_put(d_txn, dbi, (MDB_val*)&key, (MDB_val*)&val, flags)))
      throw std::runtime_error("putting data: " + std::string(mdb_strerror(rc)));
  }

  void put(MDB_dbi dbi, string_view key, string_view val, int flags=0);
  

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
    if(!d_txn)
      throw std::runtime_error("Attempt to use a closed transaction for get");

    return mdb_get(d_txn, dbi, (MDB_val*)&key, &val);
  }


  int get(MDB_dbi dbi, string_view key, string_view& val);
  
  MDBDbi openDB(const char* dbname, int flags)
  {
    return MDBDbi(d_parent->d_env, d_txn, dbname, flags);
  }

  MDBRWCursor getCursor(const MDBDbi&);

  void reportCursor(MDBRWCursor* child)
  {
    d_cursors.insert(child);
  }
  void reportCursorMove(MDBRWCursor* from, MDBRWCursor* to)
  {
    d_cursors.erase(from);
    d_cursors.insert(to);
  }
  
  operator MDB_txn*&()
  {
    return d_txn;
  }


  
  std::set<MDBRWCursor*> d_cursors;
  MDBEnv* d_parent;
  MDB_txn* d_txn;
};

/* "A cursor in a write-transaction can be closed before its transaction ends, and will otherwise be closed when its transaction ends" 
   This is a problem for us since it may means we are closing the cursor twice, which is bad
*/
class MDBRWCursor
{
public:
  MDBRWCursor(MDBRWTransaction* parent, const MDB_dbi& dbi) : d_parent(parent)
  {
    int rc= mdb_cursor_open(d_parent->d_txn, dbi, &d_cursor);
    if(rc) {
      throw std::runtime_error("Error creating RW cursor: "+std::string(mdb_strerror(rc)));
    }
    d_parent->reportCursor(this);
  }
  MDBRWCursor(MDBRWCursor&& rhs)
  {
    cout<<"Got move constructed, this was: "<<(void*)&rhs<<", now: "<<(void*)this<<endl;
    d_parent = rhs.d_parent;
    d_cursor = rhs.d_cursor;
    rhs.d_cursor=0;
    d_parent->reportCursorMove(&rhs, this);
  }

  void close()
  {
    if(d_cursor)
      mdb_cursor_close(d_cursor);
    d_cursor=0;
  }
  
  ~MDBRWCursor()
  {
    if(d_cursor)
      mdb_cursor_close(d_cursor);
  }

  int get(MDB_val& key, MDB_val& data, MDB_cursor_op op)
  {
    return mdb_cursor_get(d_cursor, &key, &data, op);
  }

  int put(MDB_val& key, MDB_val& data, int flags=0)
  {
    return mdb_cursor_put(d_cursor, &key, &data, flags);
  }

  int del(MDB_val& key, int flags)
  {
    return mdb_cursor_del(d_cursor, flags);
  }
  
  MDB_cursor* d_cursor;
  MDBRWTransaction* d_parent;
};

