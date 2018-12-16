#include "lmdb-typed.hh"
#include <arpa/inet.h>

#include <string>

unsigned int getMaxID(MDBRWTransaction& txn, MDBDbi& dbi)
{
  auto cursor = txn.getCursor(dbi);
  MDBOutVal maxidval, maxcontent;
  unsigned int maxid{0};
  if(!cursor.get(maxidval, maxcontent, MDB_LAST)) {
    maxid = maxidval.get<unsigned int>();
  }
  return maxid;
}


using namespace std;

struct DNSResourceRecord
{
  string qname; // index
  uint16_t qtype{0};
  uint32_t domain_id{0}; // index
  string content;
  uint32_t ttl{0};
  string ordername;   // index
  bool auth{true};
};

template<class Archive>
void serialize(Archive & ar, DNSResourceRecord& g, const unsigned int version)
{
  ar & g.qtype;
  ar & g.qname;
  ar & g.content;
  ar & g.ttl;
  ar & g.domain_id;
  ar & g.ordername;
  ar & g.auth;
}


struct compound
{
  std::string operator()(const DNSResourceRecord& rr)
  {
    std::string ret;
    uint32_t id = htonl(rr.domain_id);
    ret.assign((char*)&id, 4);
    ret.append(rr.ordername);
    return ret;
  }
};

int main()
{
  TypedDBI<DNSResourceRecord, 
           index_on<DNSResourceRecord, string,   &DNSResourceRecord::qname>,
           index_on<DNSResourceRecord, uint32_t, &DNSResourceRecord::domain_id>,
           index_on_function<DNSResourceRecord, string, compound>
           > tdbi(getMDBEnv("./typed.lmdb", MDB_NOSUBDIR, 0600), "records");

  {
    auto rotxn = tdbi.getROTransaction();
    DNSResourceRecord rr0;
    if(rotxn.get(2, rr0)) {
      cout << "id 2, found "<<rr0.qname<<endl;
    }
    else {
      cout <<"Did not find id 2" << endl;
    }
    cout<<"Iterating over name 'powerdns.com': "<<endl;
    auto range = rotxn.equal_range<0>("powerdns.com");
    for(auto iter = std::move(range.first); iter != range.second; ++iter)
    {
      cout << iter->qname << " " << iter->qtype << " " <<iter->content <<endl;
    }
    cout<<"Currently have "<< rotxn.size()<< " entries"<<endl;
    cout<<" " << rotxn.size<0>() << " " << rotxn.size<1>() << " " << rotxn.size<2>() << endl;
    cout<<" " << rotxn.cardinality<0>() << endl;
    cout<<" " << rotxn.cardinality<1>() << endl;
    cout<<" " << rotxn.cardinality<2>() << endl;
  }
  
  auto txn = tdbi.getRWTransaction();
  txn.clear();

  cout<<"Currently have "<< txn.size()<< " entries after clear"<<endl;
  cout<<" " << txn.size<0>() << " " << txn.size<1>() << " " << txn.size<2>() << endl;
  cout<<" " << txn.cardinality<0>() << endl;
  cout<<" " << txn.cardinality<1>() << endl;
  cout<<" " << txn.cardinality<2>() << endl;

  DNSResourceRecord rr;
  rr.domain_id=11;  rr.qtype = 5;  rr.ttl = 3600;  rr.qname = "www.powerdns.com";  rr.ordername = "www";
  rr.content = "powerdns.com";
  
  auto id = txn.put(rr);
  cout<<"Puted as id "<<id<<endl;
  
  rr.qname = "powerdns.com";  rr.qtype = 1;  rr.ordername="";  rr.content = "1.2.3.4";

  id = txn.put(rr);
  cout<<"Puted as id "<<id<<endl;

  rr.qtype = 2;  rr.content = "ns1.powerdns.com";  rr.ordername = "ns1";
  id = txn.put(rr);
  cout<<"Puted as id "<<id<<endl;

  rr.content = "ns2.powerdns.com";  rr.ordername = "ns2";  id = txn.put(rr);
  cout<<"Puted as id "<<id<<endl;

  rr.qname = "www.ds9a.nl";  rr.domain_id = 10;  rr.content = "1.2.3.4";  rr.qtype = 1;
  rr.ordername="www";
  txn.put(rr);

  rr.qname = "ds9a.nl"; rr.content = "ns1.ds9a.nl bert.ds9a.nl 1"; rr.qtype = 6;
  rr.ordername="";
  txn.put(rr);

  rr.qname = "ds9a.nl"; rr.content = "25 ns1.ds9a.nl"; rr.qtype = 15;
  txn.put(rr);

  rr.qname = "ns1.ds9a.nl"; rr.content = "1.2.3.4"; rr.qtype = 1;
  rr.ordername="ns1";
  txn.put(rr);
  rr.qname = "ns1.ds9a.nl"; rr.content = "::1"; rr.qtype = 26;
  txn.put(rr);

  rr.qname = "ns2.ds9a.nl"; rr.content = "1.2.3.4"; rr.qtype = 1;
  rr.ordername="ns2";
  txn.put(rr);
  rr.qname = "ns2.ds9a.nl"; rr.content = "::1"; rr.qtype = 26;
  txn.put(rr);

  
  
  DNSResourceRecord rr2;
  id = txn.get<0>("www.powerdns.com", rr2);

  cout<<"Retrieved id "<< id <<", content: "<<rr2.content<<endl;

  id = txn.get<0>("powerdns.com", rr2);

  cout<<"Retrieved id "<< id <<", content: "<<rr2.content<<endl;

  DNSResourceRecord rr3;
  id = txn.get<0>("powerdns.com", rr3);
  cout<< id << endl;


  cout<<"Going to iterate over everything, ordered by name!"<<endl;
  for(auto iter = txn.begin<0>(); iter != txn.end(); ++iter) {
    cout << iter.getID()<<": "<<iter->qname << " " << iter->qtype << " " << iter->content <<endl;
  }

  cout<<"Going to iterate over everything, ordered by domain_id!"<<endl;
  for(auto iter = txn.begin<1>(); iter != txn.end(); ++iter) {
    cout << iter.getID()<<": "<<iter->qname << " " << iter->qtype << " " << iter->content <<endl;
  }

  cout<<"Going to iterate over everything, ordered by id!"<<endl;
  for(auto iter = txn.begin(); iter != txn.end(); ++iter) {
    cout << iter.getID()<<": "<<iter->qname << " " << iter->qtype << " " << iter->content <<endl;
  }

  cout<<"Going to iterate over everything, ordered by compound index!"<<endl;
  for(auto iter = txn.begin<2>(); iter != txn.end(); ++iter) {
    cout << iter.getID()<<": "<<iter->qname << " " << iter->qtype << " " << iter->content <<" # "<<iter->ordername << endl;
  }

  compound c;
  rr3.ordername = "vvv";
  rr3.domain_id = 10;
  auto iter = txn.lower_bound<2>(c(rr3));
  cout <<"Found for '"<<rr3.ordername<<"' using compound index: "<<iter->qname<< " # '" <<iter->ordername<< "'"<<endl;
  for(int n =0 ; n < 4; ++n) {
    --iter;
    cout <<"Found PREV using compound index: "<<iter->qname<< " # '" <<iter->ordername<<"'"<<endl;
  }
  
  cout<<"Going to iterate over the name powerdns.com!"<<endl;

  for(auto iter = txn.equal_range<0>("powerdns.com"); iter.first != iter.second; ++iter.first) {
    cout << iter.first.getID()<<": "<<iter.first->qname << " " << iter.first->qtype << " " << iter.first->content <<endl;
  }
  cout<<"Done iterating"<<endl;                        

  cout<<"Going to iterate over the zone ds9a.nl!"<<endl;

  for(auto iter = txn.find<1>(10); iter != txn.end(); ++iter) {
    cout << iter.getID()<<": "<<iter->qname << " " << iter->qtype << " " << iter->content <<endl;
  }
  cout<<"Done iterating"<<endl;                        

  DNSResourceRecord change;
  txn.get(1, change);
  cout<<"1.auth: "<<change.auth << endl;
  txn.modify(1, [](DNSResourceRecord& c) {
      c.auth = false;
    });
  txn.get(1, change);
  cout<<"1.auth: "<<change.auth << endl;
  txn.del(1);

  //  DNSResourceRecord rr4;
  //  id = txn.get3("ns1", rr4);
  //  cout<<"Found "<<id<<": " << rr4.content <<endl;

  txn.commit();
}
