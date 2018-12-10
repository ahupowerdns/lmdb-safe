#include "lmdb-safe.hh"
#include <sstream>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
using namespace std;

struct Record
{

  friend class boost::serialization::access;
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version)
  {
    ar & id & domain_id & name & type & ttl & content & enabled & auth;
  }
  
  unsigned int id;
  unsigned int domain_id; // needs index
  std::string name;  // needs index
  std::string type;
  unsigned int ttl{0};
  std::string content;
  bool enabled{true};
  bool auth{true};

};

static unsigned int getMaxID(MDBRWTransaction& txn, MDBDbi& dbi)
{
  auto cursor = txn.getCursor(dbi);
  MDBOutVal maxidval, maxcontent;
  unsigned int maxid{0};
  if(!cursor.get(maxidval, maxcontent, MDB_LAST)) {
    maxid = maxidval.get<unsigned int>();
  }
  return maxid;
}

static void store(MDBRWTransaction& txn, MDBDbi& records, MDBDbi& domainidx, MDBDbi&nameidx, const Record& r)
{
  ostringstream oss;
  boost::archive::binary_oarchive oa(oss,boost::archive::no_header );
  oa << r;
  
  txn.put(records, r.id, oss.str(), MDB_APPEND);
  txn.put(domainidx, r.domain_id, r.id);
  txn.put(nameidx, r.name, r.id);
}


int main(int argc, char** argv)
{
  auto env = getMDBEnv("pdns",  0, 0600);
  auto records = env->openDB("records", MDB_INTEGERKEY | MDB_CREATE );
  auto domainidx = env->openDB("domainidx", MDB_INTEGERKEY | MDB_DUPFIXED | MDB_DUPSORT | MDB_CREATE);
  auto nameidx = env->openDB("nameidx", MDB_DUPFIXED | MDB_DUPSORT | MDB_CREATE);

  auto txn = env->getRWTransaction();

  /*
  txn.clear(records);
  txn.clear(domainidx);
  txn.clear(domainidx);
  txn.clear(nameidx);
  */

  unsigned int maxid=getMaxID(txn, records);
  unsigned int maxdomainid=getMaxID(txn, domainidx);
  
  cout<<"Maxid = "<<maxid<<", Max domain ID = "<<maxdomainid<<endl;

  string prefix(argv[1]);
  auto lim=atoi(argv[2]);
  for(int n=0; n < lim; ++n) {
    string domain;
    if(n)
      domain.assign(prefix+std::to_string(n)+".com");
    else
      domain="powerdns.com";
    Record r;
    r.id=++maxid;
    r.domain_id = ++maxdomainid;
    r.name = domain;
    r.ttl = 3600;
    r.type = "SOA";
    r.content = "ns1.powerdns.com ahu.powerdns.com 1";

    store(txn, records, domainidx, nameidx, r);

    r.id=++maxid;
    r.type="NS";
    r.content="ns1.powerdns.com";
    store(txn, records, domainidx, nameidx, r);


    r.id=++maxid;
    r.type="A";
    r.content="1.2.3.4";
    store(txn, records, domainidx, nameidx, r);

    r.id=++maxid;
    r.type="AAAA";
    r.content="::1";
    store(txn, records, domainidx, nameidx, r);

    r.id=++maxid;
    r.type="CAA";
    r.content="letsencrypt.org";
    store(txn, records, domainidx, nameidx, r);
    

    r.id=++maxid;
    r.type="AAAA";
    r.name="www."+domain;
    r.content="::1";
    store(txn, records, domainidx, nameidx, r);

    r.id=++maxid;
    r.type="A";
    r.name="www."+domain;
    r.content="127.0.0.1";
    store(txn, records, domainidx, nameidx, r);
  }
  
  txn.commit();

  auto rotxn = env->getROTransaction();
  auto rotxn2 = env->getROTransaction();
  
  auto rocursor = rotxn.getCursor(nameidx);

  MDBOutVal data;
  int count = 0;
  MDBOutVal key;

  MDBInVal tmp("www.powerdns.com");
  key.d_mdbval = tmp.d_mdbval;

  // ugh
  
  while(!rocursor.get(key, data, count ? MDB_NEXT_DUP : MDB_SET)) {
    unsigned int id = data.get<unsigned int>();
    cout<<"Got something: id="<<id<<endl;
    MDBOutVal record;

    if(!rotxn.get(records, data, record)) {
      Record test;
      stringstream istr{record.get<string>()};
      boost::archive::binary_iarchive oi(istr,boost::archive::no_header );
      oi >> test;
      cout <<"Record: "<<test.name<<" "<<test.type <<" " <<test.ttl<<" "<<test.content<<endl;
    }
    else {
      cout<<"Did not find anything for id "<<id<<endl;
    }
    ++count;
  }
  
}
