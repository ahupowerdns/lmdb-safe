#define CATCH_CONFIG_MAIN

#include <iostream>
#include "catch2/catch.hpp"
#include "lmdb-safe.hh"

using namespace std;

TEST_CASE("Most basic tests", "[mostbasic]") {
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  REQUIRE(1);

  MDBDbi main = env.openDB("", MDB_CREATE);
  
  auto txn = env.getRWTransaction();
  MDBOutVal out;

  REQUIRE(txn.get(main, "lmdb", out) == MDB_NOTFOUND);
  
  txn.put(main, "lmdb", "hot");

  REQUIRE(txn.get(main, "lmdb", out) == 0);
  REQUIRE(out.get<std::string>() == "hot");
  txn.abort();

  auto rotxn = env.getROTransaction();
  REQUIRE(rotxn.get(main, "lmdb", out) == MDB_NOTFOUND);
}
