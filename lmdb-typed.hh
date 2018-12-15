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

  uint32_t size(MDBRWTransaction& txn)
  {
    MDB_stat stat;
    mdb_stat(txn, d_idx, &stat);
    return stat.ms_entries;
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

  uint32_t size()
  {
    return 0;
  }
  
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
    
#define openMacro(N) std::get<N>(d_tuple).openDB(d_env, std::string(name)+"_"#N, MDB_CREATE | MDB_DUPFIXED | MDB_DUPSORT);
    openMacro(0);
    openMacro(1);
    openMacro(2);
    openMacro(3);
#undef openMacro
   
  }

  typedef std::tuple<I1, I2, I3, I4> tuple_t; 
  tuple_t d_tuple;
  
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

    uint32_t size()
    {
      MDB_stat stat;
      mdb_stat(d_txn, d_parent->d_main, &stat);
      return stat.ms_entries;
    }
    
    uint32_t insert(const T& t)
    {
      uint32_t id = getMaxID(d_txn, d_parent->d_main) + 1;
      d_txn.put(d_parent->d_main, id, serToString(t));

#define insertMacro(N) std::get<N>(d_parent->d_tuple).put(d_txn, t, id);
      insertMacro(0);
      insertMacro(1);
      insertMacro(2);
      insertMacro(3);
#undef insertMacro

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
      clearIndex(id, t);
    }

    void clear()
    {
      auto cursor = d_txn.getCursor(d_parent->d_main);
      bool first = true;
      MDBOutVal key, data;
      while(!cursor.get(key, data, first ? MDB_FIRST : MDB_NEXT)) {
        first = false;
        T t;
        serFromString(data.get<std::string>(), t);
        clearIndex(key.get<uint32_t>(), t);
        cursor.del();
      }
    }

    template<int N>
    uint32_t get(const typename std::tuple_element<N, tuple_t>::type::type& key, T& out)
    {
      MDBOutVal id;
      if(!d_txn.get(std::get<N>(d_parent->d_tuple).d_idx, key, id)) 
        return get(id.get<uint32_t>(), out);
      return 0;
    }

    template<int N>
    uint32_t size()
    {
      return std::get<N>(d_parent->d_tuple).size(d_txn);
    }

    template<int N>
    uint32_t cardinality()
    {
      auto cursor = d_txn.getCursor(std::get<N>(d_parent->d_tuple).d_idx);
      bool first = true;
      MDBOutVal key, data;
      uint32_t count = 0;
      while(!cursor.get(key, data, first ? MDB_FIRST : MDB_NEXT_NODUP)) {
        ++count;
        first=false;
      }
      return count;
    }
    
    void commit()
    {
      d_txn.commit();
    }

    void abort()
    {
      d_txn.abort();
    }

  struct eiter_t
  {};

  template<int N>
  struct iter_t
  {
    explicit iter_t(RWTransaction* parent, const typename std::tuple_element<N, tuple_t>::type::type& key) :
      d_parent(parent),
      d_cursor(d_parent->d_txn.getCursor(std::get<N>(d_parent->d_parent->d_tuple).d_idx)),
      d_in(key)
    {
      d_key.d_mdbval = d_in.d_mdbval;

      MDBOutVal id, data;
      if(d_cursor.get(d_key, id,  MDB_SET)) {
        d_end = true;
        return;
      }
      if(d_parent->d_txn.get(d_parent->d_parent->d_main, id, data))
        throw std::runtime_error("Missing id in constructor");

      serFromString(data.get<std::string>(), d_t);
    }


    bool operator!=(const eiter_t& rhs)
    {
      return !d_end;
    }

    bool operator==(const eiter_t& rhs)
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

    iter_t& operator++()
    {
      MDBOutVal id, data;
      int rc = d_cursor.get(d_key, id, MDB_NEXT_DUP);
      if(rc == MDB_NOTFOUND) {
        d_end = true;
      }
      else {
        if(d_parent->d_txn.get(d_parent->d_parent->d_main, id, data))
          throw std::runtime_error("Missing id field");
        
        serFromString(data.get<std::string>(), d_t);
      }
      return *this;
    }
    
    RWTransaction* d_parent;
    MDBRWCursor d_cursor;
    MDBOutVal d_key, d_data;
    MDBInVal d_in;
    bool d_end{false};
    T d_t;
  };

  template<int N>
  iter_t<N> find(const typename std::tuple_element<N, tuple_t>::type::type& key)
  {
    iter_t<N> ret{this, key};
    return ret;
  };
  
  eiter_t end()
  {
    return eiter_t();
  }

    
  private:
    void clearIndex(uint32_t id, const T& t)
    {
#define clearMacro(N) std::get<N>(d_parent->d_tuple).del(d_txn, t, id);
      clearMacro(0);
      clearMacro(1);
      clearMacro(2);
      clearMacro(3);
#undef clearMacro      
    }
    
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





