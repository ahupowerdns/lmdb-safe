# lmdb-safe
A safe modern & performant C++ wrapper of LMDB.  For now briefly only
available for C++17, will support C++ 11 again soon.  MIT licensed.

[LMDB](http://www.lmdb.tech/doc/index.html) is an outrageously fast
key/value store with semantics that make it highly interesting for many
applications. Of specific note, besides speed, is the full support for
transactions and read/write concurrency. LMDB is also famed for its
robustness.. **when used correctly**.

The design of LMDB is elegant and simple, which aids both the performance
and stability. The downside of this elegant design is a plethora of rules
that need to be followed to not break things. In other words, LMDB delivers
great things but only if you use it exactly right.

Among the things to keep in mind when using LMDB natively:

 * Never open a database file more than once anywhere in your process
 * Never open more than one transaction within a thread
   * .. unless they are all Read Only and have MDB_NOTLS set
 * When opening a named database, no other threads may do that at the same time
 * Cursors within RO transactions need freeing, but cursors within RW
 transactions must not be freed. 

Breaking these rules causes no errors, but does lead to silent data
corruption, missing updates, or random crashes.

This LMDB library aims to deliver the full LMDB performance while
programmatically making sure the LMDB semantics are adhered to, with very
limited overhead.

Most common LMDB functionality is wrapped within this library but the native
MDB handles are all available should you want to use functionality we did
not (yet) cater for.

# Example
The following example has no overhead compared to native LMDB, but already
exhibits several ways in which lmdb-safe is easier and safer to use:
```
  auto env = getMDBEnv("./database", 0, 0600);
  auto dbi = env->openDB("example", MDB_CREATE);
  auto txn = env->getRWTransaction();
```

The first line requests an LMDB environment for a database hosted in
`./database`. **Within LMDB, it is not allowed to open a database file more
than once**, not even from other threads, not even when using a different LMDB
handle. `getMDBEnv` keeps a registry of LMDB environments, keyed to the
exact inode. If another part of your process requests access to the same
inode, it will get the same environment. 

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
wrapper agrees and using modern C++, it is possible to seemlessly use
'views' on data without copying them. Using these techniques, the call to
`txn.put()` sets the "lmdb" string to "great", without making additional
copies. 

We employ the same technique to request the value of "lmdb", which is made
available to us as a read-only view, straight onto the memory mapped data on
disk. 

In the final line, we commit the transaction, after which it also becomes
available for other threads and processes. 

