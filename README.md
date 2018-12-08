# lmdb-safe
A safe modern & performant C++ wrapper of LMDB.  For now briefly only
available for C++17, will support C++ 11 again soon.  MIT licensed.

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

Breaking these rules causes no immediate errors, but does lead to silent
data corruption, missing updates, or random crashes. Again, this is not an
actual bug in LMDB, it means that LMDB expects you to use it according to
its exact rules. And who are we to disagree?

The `lmdb-safe` library aims to deliver the full LMDB performance while
programmatically making sure the LMDB semantics are adhered to, with very
limited overhead.

Most common LMDB functionality is wrapped within this library but the native
MDB handles are all available should you want to use functionality we did
not (yet) cater for.

# Status
Very early. If using this tiny library, be aware things might change
rapidly. To use, add `lmdb-safe.cc` and `lmdb-safe.hh` to your project.

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
  MDB_val key, data;
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
    txn.put(dbi, MDBVal(n), MDBVal(n));
  }
  cout <<"Done!"<<endl;
  cout <<"Calling commit.. "; cout.flush();
  txn.commit();
  cout<<"Done!"<<endl;
```

Here we add 20 million values using the `MDBVal` wrapper which converts our
unsigned integer into an `MDB_val`. We then commit the `mdb_drop` and the 20
million puts. All this happened in less than 20 seconds.

Had we created our database with the `MDB_INTEGERKEY` option and added the
`MDB_APPEND` flag to `txn.put`, the whole process would have taken around 5
seconds.