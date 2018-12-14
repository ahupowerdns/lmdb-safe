#include "lmdb-typed.hh"
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


int main()
{
  TypedDBI<DNSResourceRecord, 
           index_on<DNSResourceRecord, string, &DNSResourceRecord::qname>,
           index_on<DNSResourceRecord, uint32_t, &DNSResourceRecord::domain_id>,
           index_on<DNSResourceRecord, string, &DNSResourceRecord::ordername>
           > tdbi(getMDBEnv("./typed.lmdb", MDB_NOSUBDIR, 0600), "records");

  auto txn = tdbi.getRWTransaction();
  cout<<"Currently have "<< txn.size()<< " entries"<<endl;
  cout<<" " << txn.size<0>() << " " << txn.size<1>() << " " << txn.size<2>() << endl;
  cout<<" " << txn.cardinality<0>() << endl;

  cout<<" " << txn.cardinality<1>() << endl;

  cout<<" " << txn.cardinality<2>() << endl;
  
  
  txn.clear();

  cout<<"Currently have "<< txn.size()<< " entries after clear"<<endl;
  cout<<" " << txn.size<0>() << " " << txn.size<1>() << " " << txn.size<2>() << endl;

  
  DNSResourceRecord rr;
  rr.domain_id=0;  rr.qtype = 5;  rr.ttl = 3600;  rr.qname = "www.powerdns.com";  rr.ordername = "www";
  rr.content = "powerdns.com";
  
  auto id = txn.insert(rr);
  cout<<"Inserted as id "<<id<<endl;
  
  rr.qname = "powerdns.com";  rr.qtype = 1;  rr.ordername=" ";  rr.content = "1.2.3.4";

  id = txn.insert(rr);
  cout<<"Inserted as id "<<id<<endl;

  rr.qtype = 2;  rr.content = "ns1.powerdns.com";  rr.ordername = "ns1";
  id = txn.insert(rr);
  cout<<"Inserted as id "<<id<<endl;

  rr.content = "ns2.powerdns.com";  rr.ordername = "ns2";  id = txn.insert(rr);
  cout<<"Inserted as id "<<id<<endl;

  rr.qname = "www.ds9a.nl";  rr.domain_id = 1;  rr.content = "1.2.3.4";  rr.qtype = 1;
  txn.insert(rr);

  rr.qname = "ds9a.nl"; rr.content = "ns1.ds9a.nl bert.ds9a.bl 1"; rr.qtype = 6;
  txn.insert(rr);
  
  DNSResourceRecord rr2;
  id = txn.get<0>("www.powerdns.com", rr2);

  cout<<"Retrieved id "<< id <<", content: "<<rr2.content<<endl;

  id = txn.get<0>("powerdns.com", rr2);

  cout<<"Retrieved id "<< id <<", content: "<<rr2.content<<endl;

  DNSResourceRecord rr3;
  id = txn.get<0>("powerdns.com", rr3);
  cout<< id << endl;

  cout<<"Going to iterate over powerdns.com!"<<endl;

  for(auto iter = txn.find<0>("powerdns.com"); iter != txn.end(); ++iter) {
    cout << iter->qname << " " << iter->qtype << " " << iter->content <<endl;
  }
  cout<<"Done iterating"<<endl;                        

  cout<<"Going to iterate over ds9a.nl!"<<endl;

  for(auto iter = txn.find<1>(1); iter != txn.end(); ++iter) {
    cout << iter->qname << " " << iter->qtype << " " << iter->content <<endl;
  }
  cout<<"Done iterating"<<endl;                        
  
  txn.del(1);

  //  DNSResourceRecord rr4;
  //  id = txn.get3("ns1", rr4);
  //  cout<<"Found "<<id<<": " << rr4.content <<endl;

  txn.commit();
}
