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


/* 
   Open issues:

   Everything should go into a namespace
   What is an error? What is an exception?
   could id=0 be magic? ('no such id')
   Composite keys (powerdns needs them)
     functional keys
   Insert? Put? Naming matters
   Is boost the best serializer?
   rename find to equal_range
   Perhaps use the separate index concept from multi_index
   A dump function would be nice (typed)
*/

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

// this only needs methods that must happen for all indexes at once
// so specifically, not size<t> or get<t>, people ask for those themselves, and
// should no do that on indexes that don't exist

template<class Class,typename Type, typename Parent>
struct LMDBIndexOps
{
  explicit LMDBIndexOps(Parent* parent) : d_parent(parent){}
  void put(MDBRWTransaction& txn, const Class& t, uint32_t id)
  {
    txn.put(d_idx, d_parent->getMember(t), id);
  }

  void del(MDBRWTransaction& txn, const Class& t, uint32_t id)
  {
    txn.del(d_idx, d_parent->getMember(t), id);
  }

  void openDB(std::shared_ptr<MDBEnv>& env, string_view str, int flags)
  {
    d_idx = env->openDB(str, flags);
  }
  MDBDbi d_idx;
  Parent* d_parent;
};

template<class Class,typename Type,Type Class::*PtrToMember>
struct index_on : LMDBIndexOps<Class, Type, index_on<Class, Type, PtrToMember>>
{
  index_on() : LMDBIndexOps<Class, Type, index_on<Class, Type, PtrToMember>>(this)
  {}
  static Type getMember(const Class& c)
  {
    return c.*PtrToMember;
  }
  
  typedef Type type;
};

template<class Class, typename Type, class Func>
struct index_on_function : LMDBIndexOps<Class, Type, index_on_function<Class, Type, Func> >
{
  index_on_function() : LMDBIndexOps<Class, Type, index_on_function<Class, Type, Func> >(this)
  {}
  static Type getMember(const Class& c)
  {
    Func f;
    return f(c);
  }

  typedef Type type;           
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
    
#define openMacro(N) std::get<N>(d_tuple).openDB(d_env, std::string(name)+"_"#N, MDB_CREATE | MDB_DUPFIXED | MDB_DUPSORT);
    openMacro(0);
    openMacro(1);
    openMacro(2);
    openMacro(3);
#undef openMacro
   
  }

  typedef std::tuple<I1, I2, I3, I4> tuple_t; 
  tuple_t d_tuple;

  template<class Parent>
  struct ReadonlyOperations
  {
    ReadonlyOperations(Parent& parent) : d_parent(parent)
    {}

    uint32_t size()
    {
      MDB_stat stat;
      mdb_stat(d_parent.d_txn, d_parent.d_parent->d_main, &stat);
      return stat.ms_entries;
    }

    template<int N>
    uint32_t size()
    {
      MDB_stat stat;
      mdb_stat(d_parent.d_txn, std::get<N>(d_parent.d_parent->d_tuple).d_idx, &stat);
      return stat.ms_entries;
    }
    
    bool get(uint32_t id, T& t)
    {
      MDBOutVal data;
      if(d_parent.d_txn.get(d_parent.d_parent->d_main, id, data))
        return false;
      
      serFromString(data.get<std::string>(), t);
      return true;
    }

    template<int N>
    uint32_t get(const typename std::tuple_element<N, tuple_t>::type::type& key, T& out)
    {
      MDBOutVal id;
      if(!d_parent.d_txn.get(std::get<N>(d_parent.d_parent->d_tuple).d_idx, key, id)) 
        return get(id.get<uint32_t>(), out);
      return 0;
    }


    template<int N>
    uint32_t cardinality()
    {
      auto cursor = d_parent.d_txn.getCursor(std::get<N>(d_parent.d_parent->d_tuple).d_idx);
      bool first = true;
      MDBOutVal key, data;
      uint32_t count = 0;
      while(!cursor.get(key, data, first ? MDB_FIRST : MDB_NEXT_NODUP)) {
        ++count;
        first=false;
      }
      return count;
    }

    struct eiter_t
    {};

    // can be on main, or on an index
    // when on main, return data directly
    // when on index, indirect
    // we can be limited to one key, or iterate over entire table
    // 
    struct iter_t
    {
      explicit iter_t(Parent* parent, typename Parent::cursor_t&& cursor, bool on_index, bool one_key, bool end=false) :
        d_parent(parent),
        d_cursor(std::move(cursor)),
        d_on_index(on_index),
        d_one_key(one_key),
        d_end(end)
      {
        if(d_end)
          return;

        if(d_cursor.get(d_key, d_id,  MDB_GET_CURRENT)) {
          d_end = true;
          return;
        }

        if(d_on_index) {
          if(d_parent->d_txn.get(d_parent->d_parent->d_main, d_id, d_data))
            throw std::runtime_error("Missing id in constructor");
          serFromString(d_data.get<std::string>(), d_t);
        }
        else
          serFromString(d_id.get<std::string>(), d_t);
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
      
      iter_t& genoperator(MDB_cursor_op dupop, MDB_cursor_op op)
      {
        MDBOutVal data;
        int rc = d_cursor.get(d_key, d_id, d_one_key ? dupop : op);
        if(rc == MDB_NOTFOUND) {
          d_end = true;
        }
        else {
          if(d_on_index) {
            if(d_parent->d_txn.get(d_parent->d_parent->d_main, d_id, data))
              throw std::runtime_error("Missing id field");
            
            serFromString(data.get<std::string>(), d_t);
          }
          else
            serFromString(d_id.get<std::string>(), d_t);
        }
        return *this;
      }

      iter_t& operator++()
      {
        return genoperator(MDB_NEXT_DUP, MDB_NEXT);
      }
      iter_t& operator--()
      {
        return genoperator(MDB_PREV_DUP, MDB_PREV);
      }
      
      uint32_t getID()
      {
        if(d_on_index)
          return d_id.get<uint32_t>();
        else
          return d_key.get<uint32_t>();
      }
      
      Parent* d_parent;
      typename Parent::cursor_t d_cursor;
      MDBOutVal d_key{0,0}, d_data{0,0}, d_id{0,0};
      bool d_on_index;
      bool d_one_key;
      bool d_end{false};
      T d_t;
    };

    template<int N>
    iter_t begin()
    {
      typename Parent::cursor_t cursor = d_parent.d_txn.getCursor(std::get<N>(d_parent.d_parent->d_tuple).d_idx);
      
      MDBOutVal out, id;
      
      if(cursor.get(out, id,  MDB_FIRST)) 
{                                              // on_index, one_key, end
        return iter_t{&d_parent, std::move(cursor), true, false, true};
      }

      return iter_t{&d_parent, std::move(cursor), true, false};
    };

    iter_t begin()
    {
      typename Parent::cursor_t cursor = d_parent.d_txn.getCursor(d_parent.d_parent->d_main);
      
      MDBOutVal out, id;
      
      if(cursor.get(out, id,  MDB_FIRST)) {
                                              // on_index, one_key, end        
        return iter_t{&d_parent, std::move(cursor), false, false, true};
      }

      return iter_t{&d_parent, std::move(cursor), false, false};
    };

    
    template<int N>
    iter_t find(const typename std::tuple_element<N, tuple_t>::type::type& key)
    {
      typename Parent::cursor_t cursor = d_parent.d_txn.getCursor(std::get<N>(d_parent.d_parent->d_tuple).d_idx);
      
      MDBInVal in(key);
      MDBOutVal out, id;
      out.d_mdbval = in.d_mdbval;
      
      if(cursor.get(out, id,  MDB_SET)) {
                                              // on_index, one_key, end        
        return iter_t{&d_parent, std::move(cursor), true, false, true};
      }

      return iter_t{&d_parent, std::move(cursor), true, false};
    };

    template<int N>
    std::pair<iter_t,eiter_t> equal_range(const typename std::tuple_element<N, tuple_t>::type::type& key)
    {
      typename Parent::cursor_t cursor = d_parent.d_txn.getCursor(std::get<N>(d_parent.d_parent->d_tuple).d_idx);
      
      MDBInVal in(key);
      MDBOutVal out, id;
      out.d_mdbval = in.d_mdbval;
      
      if(cursor.get(out, id,  MDB_SET)) {
                                              // on_index, one_key, end        
        return {iter_t{&d_parent, std::move(cursor), true, true, true}, eiter_t()};
      }

      return {iter_t{&d_parent, std::move(cursor), true, true}, eiter_t()};
    };

    
    eiter_t end()
    {
      return eiter_t();
    }

    Parent& d_parent;
  };
  
  class ROTransaction : public ReadonlyOperations<ROTransaction>
  {
  public:
    explicit ROTransaction(TypedDBI* parent) : ReadonlyOperations<ROTransaction>(*this), d_parent(parent), d_txn(d_parent->d_env->getROTransaction()) 
    {
    }

    ROTransaction(ROTransaction&& rhs) :
      ReadonlyOperations<ROTransaction>(*this), d_parent(rhs.d_parent),d_txn(std::move(rhs.d_txn))
      
    {
      rhs.d_parent = 0;
    }

    typedef MDBROCursor cursor_t;

    TypedDBI* d_parent;
    MDBROTransaction d_txn;    
  };    

  
  class RWTransaction :  public ReadonlyOperations<RWTransaction>
  {
  public:
    explicit RWTransaction(TypedDBI* parent) : ReadonlyOperations<RWTransaction>(*this), d_parent(parent), d_txn(d_parent->d_env->getRWTransaction())
    {
    }

    RWTransaction(RWTransaction&& rhs) :
      ReadonlyOperations<RWTransaction>(*this),
      d_parent(rhs.d_parent), d_txn(std::move(rhs.d_txn))
    {
      rhs.d_parent = 0;
    }

    
    uint32_t insert(const T& t, uint32_t id=0)
    {
      if(!id)
        id = getMaxID(d_txn, d_parent->d_main) + 1;
      d_txn.put(d_parent->d_main, id, serToString(t));

#define insertMacro(N) std::get<N>(d_parent->d_tuple).put(d_txn, t, id);
      insertMacro(0);
      insertMacro(1);
      insertMacro(2);
      insertMacro(3);
#undef insertMacro

      return id;
    }

    void modify(uint32_t id, std::function<void(T&)> func)
    {
      T t;
      if(!this->get(id, t))
        return; // XXX should be exception
      func(t);
      
      del(id);  // this is the lazy way. We could test for changed index fields
      insert(t, id);
    }

    
    void del(uint32_t id)
    {
      T t;
      if(!this->get(id, t)) 
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
    
    void commit()
    {
      d_txn.commit();
    }

    void abort()
    {
      d_txn.abort();
    }


    typedef MDBRWCursor cursor_t;

    
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

  public:
    TypedDBI* d_parent;
    MDBRWTransaction d_txn;
  };
  
  RWTransaction getRWTransaction()
  {
    return RWTransaction(this);
  }

  ROTransaction getROTransaction()
  {
    return ROTransaction(this);
  }
  
private:
  std::shared_ptr<MDBEnv> d_env;
  MDBDbi d_main;
  std::string d_name;
};





