#pragma once
#include <iostream>
#include "lmdb-safe.hh"
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <sstream>
using std::cout;
using std::endl;

unsigned int getMaxID(MDBRWTransaction& txn, MDBDbi& dbi);


template<typename T>
std::string serToString(const T& t)
{

  std::string serial_str;
  boost::iostreams::back_insert_device<std::string> inserter(serial_str);
  boost::iostreams::stream<boost::iostreams::back_insert_device<std::string> > s(inserter);
  boost::archive::binary_oarchive oa(s, boost::archive::no_header | boost::archive::no_codecvt);
  
  oa << t;
  return serial_str;
}

template<typename T>
void serFromString(const std::string& str, T& ret)
{
  ret = T();
  std::istringstream istr{str};
  boost::archive::binary_iarchive oi(istr,boost::archive::no_header|boost::archive::no_codecvt );
  oi >> ret;
}


/* This is for storing a struct that has to be found using several
   of its fields.

   We want a typed lmdb database that we can only:
   * insert such structs
   * remove them
   * mutate them

   All while maintaining indexes on insert, removal and mutation.

   struct DNSResourceRecord
   {
      string qname; // index
      string qtype; 
      uint32_t domain_id; // index
      string content;
      string ordername;   // index
      bool auth;
   }

   TypedDBI<DNSResourceRecord, 
     DNSResourceRecord::qname, 
     DNSResourceRecord::domain_id,
     DNSResourceRecord::ordername> tdbi;

   DNSResourceRecord rr;
   uint32_t id = tdbi.insert(rr); // inserts, creates three index items
   
   tdbi.modify(id, [](auto& rr) { rr.auth=false; });

   DNSResourceRecord retrr;
   uint32_t id = tdbi.get<1>(qname, retrr);

   // this checks for changes and updates indexes if need be
   tdbi.modify(id, [](auto& rr) { rr.ordername="blah"; });
*/


template<class Class,typename Type,Type Class::*PtrToMember>
struct index_on
{
  static Type getMember(const Class& c)
  {
    return c.*PtrToMember;
  }

  void put(MDBRWTransaction& txn, const Class& t, uint32_t id)
  {
    txn.put(d_idx, getMember(t), id);
  }

  void del(MDBRWTransaction& txn, const Class& t, uint32_t id)
  {
    txn.del(d_idx, getMember(t), id);
  }

  void openDB(std::shared_ptr<MDBEnv>& env, string_view str, int flags)
  {
    d_idx = env->openDB(str, flags);
  }

  
  typedef Type type;
  MDBDbi d_idx;
};

struct nullindex_t
{
  template<typename Class>
  void put(MDBRWTransaction& txn, const Class& t, uint32_t id)
  {}
  template<typename Class>
  void del(MDBRWTransaction& txn, const Class& t, uint32_t id)
  {}

  void openDB(std::shared_ptr<MDBEnv>& env, string_view str, int flags)
  {
    
  }
  typedef uint32_t type; // dummy
};

template<typename T, class I1=nullindex_t, class I2=nullindex_t, class I3 = nullindex_t, class I4 = nullindex_t>
class TypedDBI
{
public:
  TypedDBI(std::shared_ptr<MDBEnv> env, string_view name)
    : d_env(env), d_name(name)
  {
    d_main = d_env->openDB(name, MDB_CREATE | MDB_INTEGERKEY);
    d_i1.openDB(d_env, std::string(name)+"_1", MDB_CREATE | MDB_DUPFIXED | MDB_DUPSORT);
    d_i2.openDB(d_env, std::string(name)+"_2", MDB_CREATE | MDB_DUPFIXED | MDB_DUPSORT);
    d_i3.openDB(d_env, std::string(name)+"_3", MDB_CREATE | MDB_DUPFIXED | MDB_DUPSORT);
    d_i4.openDB(d_env, std::string(name)+"_4", MDB_CREATE | MDB_DUPFIXED | MDB_DUPSORT);
  }

  I1 d_i1;
  I2 d_i2;
  I3 d_i3;
  I4 d_i4;
  
  class RWTransaction
  {
  public:
    explicit RWTransaction(TypedDBI* parent) : d_parent(parent), d_txn(d_parent->d_env->getRWTransaction())
    {
    }

    RWTransaction(RWTransaction&& rhs) :
      d_parent(rhs.d_parent), d_txn(std::move(rhs.d_txn))
    {
      rhs.d_parent = 0;
    }

    uint32_t insert(const T& t)
    {
      uint32_t id = getMaxID(d_txn, d_parent->d_main) + 1;
      d_txn.put(d_parent->d_main, id, serToString(t));

      d_parent->d_i1.put(d_txn, t, id);
      d_parent->d_i2.put(d_txn, t, id);
      d_parent->d_i3.put(d_txn, t, id);
      d_parent->d_i4.put(d_txn, t, id);
      return id;
    }

    bool get(uint32_t id, T& t) 
    {
      MDBOutVal data;
      if(d_txn.get(d_parent->d_main, id, data))
        return false;
      
      serFromString(data.get<std::string>(), t);
      return true;
    }
    
    void del(uint32_t id)
    {
      T t;
      if(!get(id, t)) 
        return;
      
      d_txn.del(d_parent->d_main, id);
      
      d_parent->d_i1.del(d_txn, t, id);
      d_parent->d_i2.del(d_txn, t, id);
      d_parent->d_i3.del(d_txn, t, id);
      d_parent->d_i4.del(d_txn, t, id);
    }
    
    uint32_t get1(const typename I1::type& key, T& out)
    {
      MDBOutVal id;
      if(!d_txn.get(d_parent->d_i1.d_idx, key, id)) 
        return get(id.get<uint32_t>(), out);
      return 0;
    }

    uint32_t get2(const typename I2::type& key, T& out)
    {
      MDBOutVal id;
      if(!d_txn.get(d_parent->d_i2.d_idx, key, id)) 
        return get(id.get<uint32_t>(), out);
      return 0;
    }

    uint32_t get3(const typename I3::type& key, T& out)
    {
      MDBOutVal id;
      if(!d_txn.get(d_parent->d_i3.d_idx, key, id)) 
        return get(id.get<uint32_t>(), out);
      return 0;
    }

    uint32_t get4(const typename I4::type& key, T& out)
    {
      MDBOutVal id;
      if(!d_txn.get(d_parent->d_i4.d_idx, key, id)) 
        return get(id.get<uint32_t>(), out);
      return 0;
    }
    
    void commit()
    {
      d_txn.commit();
    }

    void abort()
    {
      d_txn.abort();
    }

    
  private:
    TypedDBI* d_parent;
    MDBRWTransaction d_txn;
  };
  
  RWTransaction getRWTransaction()
  {
    return RWTransaction(this);
  }
  
private:

  std::shared_ptr<MDBEnv> d_env;
  MDBDbi d_main;
  std::string d_name;
};


#if 0
  struct eiter1_t
  {};
  struct iter1_t
  {
    explicit iter1_t(MDBROTransaction && txn, const MDBDbi& dbi, const MDBDbi& main, const typename I1::type& key) : d_txn(std::move(txn)), d_cursor(d_txn.getCursor(dbi)), d_in(key), d_main(main)
    {
      d_key.d_mdbval = d_in.d_mdbval;

      MDBOutVal id, data;
      if(d_cursor.get(d_key, id,  MDB_SET)) {
        d_end = true;
        return;
      }
      if(d_txn.get(d_main, id, data))
        throw std::runtime_error("Missing id in constructor");

      serFromString(data.get<std::string>(), d_t);
    }


    bool operator!=(const eiter1_t& rhs)
    {
      return !d_end;
    }

    bool operator==(const eiter1_t& rhs)
    {
      return d_end;
    }

    const T& operator*()
    {
      return d_t;
    }

    const T* operator->()
    {
      return &d_t;
    }

    iter1_t& operator++()
    {
      MDBOutVal id, data;
      int rc = d_cursor.get(d_key, id, MDB_NEXT_DUP);
      if(rc == MDB_NOTFOUND) {
        d_end = true;
      }
      else {
        if(d_txn.get(d_main, id, data))
          throw std::runtime_error("Missing id field");
        
        serFromString(data.get<std::string>(), d_t);
      }
      return *this;
    }
    
    MDBROTransaction d_txn;
    MDBROCursor d_cursor;
    MDBOutVal d_key, d_data;
    MDBInVal d_in;
    bool d_end{false};
    T d_t;
    MDBDbi d_main;
  };
  
  iter1_t find1(const typename I1::type& key)
  {
    iter1_t ret{std::move(d_env->getROTransaction()), d_idx1.d_ix1 d_main, key};
    return ret;
  };
  
  eiter1_t end()
  {
    return eiter1_t();
  }

#endif
