# lmdb-safe
A safe modern & performant C++ wrapper of LMDB.  
Requires C++17, or C++11 + Boost.

[LMDB](http://www.lmdb.tech/doc/index.html) is an outrageously fast
key/value store with semantics that make it highly interesting for many
applications.  Of specific note, besides speed, is the full support for
transactions and good read/write concurrency.  LMDB is also famed for its
robustness..  **when used correctly**.

The design of LMDB is elegant and simple, which aids both the performance
and stability. The downside of this elegant design is a [nontrivial set of
rules](http://www.lmdb.tech/doc/starting.html)
that [need to be followed](http://www.lmdb.tech/doc/group__mdb.html) to not break things. In other words, LMDB delivers
great things but only if you use it exactly right. This is by [conscious
design](https://twitter.com/hyc_symas/status/1056168832606392320). 

Among the things to keep in mind when using LMDB natively:

 * Never open a database file more than once anywhere in your process
 * Never open more than one transaction within a thread
   * .. unless they are all read-only and have MDB_NOTLS set
 * When opening a named database, no other threads may do that at the same time
 * Cursors within RO transactions need freeing, but cursors within RW
 transactions must not be freed. 
 * A new transaction may indicate the database has grown, and you need to
   restart the transaction then.

Breaking these rules may cause no immediate errors, but can lead to silent
data corruption, missing updates, or random crashes. Again, this is not an
actual bug in LMDB, it means that LMDB expects you to use it according to
its exact rules. And who are we to disagree?

The `lmdb-safe` library aims to deliver the full LMDB performance while
programmatically making sure the LMDB semantics are adhered to, with very
limited overhead.

Most common LMDB functionality is wrapped within this library but the native
MDB handles are all available should you want to use functionality we did
not (yet) cater for.

In addition, on top of `lmdb-safe`, a type-safe ["Object Relational
Mapping"](https://en.wikipedia.org/wiki/Object-relational_mapping) interface
is also available. This auto-generates indexes and allows for the insertion,
deletion and iteration of objects. 

# Status
Fresh. If using this tiny library, be aware things might change
rapidly. To use, add `lmdb-safe.cc` and `lmdb-safe.hh` to your project. In
addition, add `lmdb-typed.hh` to use the ORM.

# Philosophy
This library tries to not restrict your use of LMDB, nor make it slower,
except on operations that should be rare. The native LMDB handles
(Environment, DBI, Transactions & Cursors) are all available for your direct
use if need be.

When using `lmdb-safe`, errors "that should never happen" are turned into
exceptions. An error that merely indicates that a key can not be found is
passed on as a regular LMDB error code.

# Example
The following example has no overhead compared to native LMDB, but already
exhibits several ways in which lmdb-safe automates LMDB constraints:
```
  auto env = getMDBEnv("./database", 0, 0600);
  auto dbi = env->openDB("example", MDB_CREATE);
  auto txn = env->getRWTransaction();
```

The first line requests an LMDB environment for a database hosted in
`./database`.  **Within LMDB, it is not allowed to open a database file more
than once**, not even from other threads, not even when using a different
LMDB handle.  `getMDBEnv` keeps a registry of LMDB environments, keyed to
the exact inode & flags.  If another part of your process requests access to
the same inode, it will get the same environment. `MDBEnv` is threadsafe.

On the second line, a database is opened within our environment. The
semantics of opening or creating a database within LMDB are tricky. With
some loss of generality, `MDBEnv::openDB` will create a transaction for you
to open the database, and close it too. Most of the time this is what you
want. It is also possible to open a database within a transaction manually.

The third line opens a read/write transaction using the Resource Acquisition
Is Initialization (RAII) technique. If `txn` goes out of scope, the
transaction is aborted automatically. To commit or abort, use `commit()` or
`abort()`, after which going out of scope has no further effect.

```
  txn.put(dbi, "lmdb", "great");

  string_view data;
  if(!txn.get(dbi, "lmdb", data)) {
    cout<< "Within RW transaction, found that lmdb = " << data <<endl;
  }
  else
    cout<<"Found nothing" << endl;

  txn.commit();
```

LMDB is so fast because it does not copy data unless it really needs to.
Memory bandwidth is a huge determinant of performance on modern CPUs. This
wrapper agrees, and using modern C++ makes it possible to seemlessly use
'views' on data without copying them. Using these techniques, the call to
`txn.put()` sets the "lmdb" string to "great", without making additional
copies. 

We employ the same technique to request the value of "lmdb", which is made
available to us as a read-only view, straight onto the memory mapped data on
disk. 

In the final line, we commit the transaction, after which it also becomes
available for other threads and processes. 

A slightly expanded version of this code can be found in
[basic-example.cc](basic-example.cc).

# Input and output of values
The basic data unit of LMDB is `MDB_val` which describes a slab of memory.
Within LMDB, `MDB_val` is used for both input and output. For safety
purposes, in this library we split this up into `MDBInValue` and
`MDBOutValue`. Once split, we can add some very convenient semantics to these
classes.

For example, to store `double` values for 64 bit IDs:

```
  auto txn = env->getRWTransaction();
  uint64_t id=12345678901;
  double score=3.14159;
  txn.put(dbi, id, score);
  txn.commit();
```

Behind the scenes, the `id` and `score` values are wrapped by `MDBInVal`
which converts these values into byte strings. To retrieve thise values
works similary:

```
  auto txn = env->getRWTransaction();
  uint64_t id=12345678901;
  MDBOutValue val;

  txn.get(dbi, id, val);

  cout << "Score: " << val.get<double>() << "\n";
```

Note that on retrieval, we have to specify the type of the value stored.
This allows the conversion back from a byte string into the native type.
`MDBOutValue` also tests if the length of the data matches the type.

## Details
The automatic conversion to and from the `MDBVal`s is implemented strictly
for:

 * Integer and floating point types
 * std::string
 * std::string_view

However, if you explicitly ask for it, it is also possible to serialize
`struct`s:

```
struct Coordinate
{
	double x,y;
};

C c{12.0, 13.0};

txn.put(dbi, MDBInVal::fromStruct(c), 12.0);

MDBOutVal res;
txn.get(dbi, MDBInVal::fromStruct(c), res);

auto c1 = res.get_struct<Coordinate>();
```

# Cursors, transactions
This example shows how to use cursors and how to mix `lmdb-safe` with direct
calls to mdb.

```
  auto env = getMDBEnv("./database", 0, 0600);
  auto dbi = env->openDB("huge", MDB_CREATE);
  auto txn = env->getRWTransaction();

  unsigned int limit=20000000;
```

This is the usual opening sequence.

```
  auto cursor=txn.getCursor(dbi);
  MDBOutVal key, data;
  int count=0;
  cout<<"Counting records.. "; cout.flush();
  while(!cursor.get(key, data, count ? MDB_NEXT : MDB_FIRST)) {
    count++;
  }
  cout<<"Have "<<count<<"!"<<endl;
```

This describes how we generate a cursor for the `huge` database and iterate
over it to count the number of keys in there. We pass two LMDB native
`MDB_val` structs to the cursor `get` function. These do not get copies of
all the millions of potential keys in the `huge` database - they only
contain pointers to that data. Because of this, we can count 20 million
records in under a second (!).
  
```
  cout<<"Clearing records.. "; cout.flush();
  mdb_drop(txn, dbi, 0); // clear records
  cout<<"Done!"<<endl;
```

Here we drop al keys from the database, which too happens nearly
instantaneously. Note that we pass our `txn` (which is a class) to the
native `mdb_drop` function which we did not wrap. This is possible because
`txn` converts to an `MDB_env*` if needed.

```
  cout << "Adding "<<limit<<" values  .. "; cout.flush();
  for(unsigned int n = 0 ; n < limit; ++n) {
    txn.put(dbi, n, n);
  }
  cout <<"Done!"<<endl;
  cout <<"Calling commit.. "; cout.flush();
  txn.commit();
  cout<<"Done!"<<endl;
```

Here we add 20 million value & then commit the `mdb_drop` and the 20 million
puts.  All this happened in less than 20 seconds.

Had we created our database with the `MDB_INTEGERKEY` option and added the
`MDB_APPEND` flag to `txn.put`, the whole process would have taken around 5
seconds.

# lmdb-typed
The `lmdb-safe` interface may be safe in one sense, but it is still a
key-value store, allowing the user to store any key and any value.
Frequently we have specific needs: to store objects and find them using
different keys. Doing so manually is cumbersome and error-prone, as all
indexes (for rapid retrieval) need to be carefully maintained by hand.

Inspired by Boost MultiIndex, `lmdb-typed` builds on `lmdb-safe` to create,
populate and use indexes for rapidly retrieving objects. As an example,
let's say we want to store the following struct:

```
struct DNSResourceRecord
{
  string qname;          // index
  uint16_t qtype{0};
  uint32_t domain_id{0}; // index
  string content;
  uint32_t ttl{0};
  string ordername;      // index
  bool auth{true};
};
```

And we want to do so based on the `qname`, `domain_id` or `ordername`
fields. First, we have to make sure DNSResourceRecord can serialize itself
to a string:

```
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
```

Next up, we need to define our "Object Relational Mapper":

```
  TypedDBI<DNSResourceRecord, 
           index_on<DNSResourceRecord, string,   &DNSResourceRecord::qname>,
           index_on<DNSResourceRecord, uint32_t, &DNSResourceRecord::domain_id>,
           index_on<DNSResourceRecord, string,   &DNSResourceRecord::ordername>
           > tdbi(getMDBEnv("./typed.lmdb", MDB_NOSUBDIR, 0600), "records");

```
This defines that we create a database called `records` in the file
`./typed.lmdb`. We also state that this database stores `DNSResourceRecord`
objects, and that we want three indexes. Note that this syntax is reasonable
similar to that used by Boost::MultiIndex.

Next up, we can insert some objects:

```
auto txn = tdbi.getRWTransaction();
DNSResourceRecord rr{"www.powerdns.com", 1, domain_id, "1.2.3.4", 0, "www"};
// populate rr
auto id = txn.insert(rr);
txn.commit();
```

Internally, the opening of `tdbi` above created four databases: `records`,
`records_0`, `records_1` and `records_2`. On insert, a serialized form of
`rr` was stored in the `records` table, with the key containing the
(assigned) id value.

In addition, in `records_1`, the qname was added as key, with the `id` field
as value. And similarly for `domain_id` and `ordername`. So the indexes all
point to the id field, which we can find in the `records` database.

To retrieve, we can use any of the indexes:

```
auto txn = tdbi.getROTransaction(); 
DNSResourceRecord rr;
txn.get(id, rr);
txn.get<0>("www.powerdns.com", rr);
txn.get<1>(domain_id, rr);
txn.get<2>("www", rr);
```

As long as we inserted only the one `DNSResourceRecord` from above, all four
`get` calls find the same `rr`.

In the more interesting case where we inserted more DNS records, we could
iterate over all items with `domain_id = 4` as follows:

```
for(auto iter = txn.find<1>(4): iter != txn.end(); ++iter) {
	cout << iter->qname << "\n";
}
```

To delete an item, use `txn.del(12)`, which will remove the record with id
12 from the main database and also from all the indexes.

