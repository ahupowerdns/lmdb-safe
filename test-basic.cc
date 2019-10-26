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

  REQUIRE(txn->get(main, "lmdb", out) == MDB_NOTFOUND);

  txn->put(main, "lmdb", "hot");

  REQUIRE(txn->get(main, "lmdb", out) == 0);
  REQUIRE(out.get<std::string>() == "hot");
  txn->abort();

  auto rotxn = env.getROTransaction();
  REQUIRE(rotxn->get(main, "lmdb", out) == MDB_NOTFOUND);
}

TEST_CASE("Range tests", "[range]") {
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  REQUIRE(1);

  MDBDbi main = env.openDB("", MDB_CREATE);

  auto txn = env.getRWTransaction();
  MDBOutVal out;

  REQUIRE(txn->get(main, "lmdb", out) == MDB_NOTFOUND);

  txn->put(main, "bert", "hubert");
  txn->put(main, "bertt", "1975");
  txn->put(main, "berthubert", "lmdb");
  txn->put(main, "bert1", "one");
  txn->put(main, "beru", "not");

  {
    auto cursor = txn->getCursor(main);
    MDBInVal bert("bert");
    MDBOutVal key, val;
    REQUIRE(cursor.lower_bound(bert, key, val) == 0);
    REQUIRE(key.get<string>() == "bert");
    REQUIRE(val.get<string>() == "hubert");

    REQUIRE(cursor.next(key, val) == 0);
    REQUIRE(key.get<string>() == "bert1");
    REQUIRE(val.get<string>() == "one");

    REQUIRE(cursor.next(key, val) == 0);
    REQUIRE(key.get<string>() == "berthubert");
    REQUIRE(val.get<string>() == "lmdb");

    REQUIRE(cursor.lower_bound("kees", key, val) == MDB_NOTFOUND);

    txn->commit();
  }

  auto rotxn = env.getROTransaction();
  {
    auto cursor = rotxn->getCursor(main);
    MDBInVal bert("bert");
    MDBOutVal key, val;
    REQUIRE(cursor.lower_bound(bert, key, val) == 0);
    REQUIRE(key.get<string>() == "bert");
    REQUIRE(val.get<string>() == "hubert");

    REQUIRE(cursor.next(key, val) == 0);
    REQUIRE(key.get<string>() == "bert1");
    REQUIRE(val.get<string>() == "one");

    REQUIRE(cursor.next(key, val) == 0);
    REQUIRE(key.get<string>() == "berthubert");
    REQUIRE(val.get<string>() == "lmdb");

    REQUIRE(cursor.lower_bound("kees", key, val) == MDB_NOTFOUND);
  }

}

TEST_CASE("moving transactions")
{
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  REQUIRE(1);

  MDBDbi main = env.openDB("", MDB_CREATE);

  auto txn = env.getRWTransaction();
  MDBOutVal out;

  REQUIRE(txn->get(main, "lmdb", out) == MDB_NOTFOUND);

  txn->put(main, "bert", "hubert");
  txn->put(main, "bertt", "1975");
  txn->put(main, "berthubert", "lmdb");
  txn->put(main, "bert1", "one");
  txn->put(main, "beru", "not");

  auto cursor = txn->getCursor(main);
  auto txn2 = std::move(txn);
  {
    auto cursor2 = std::move(cursor);
  }
}

TEST_CASE("transaction inheritance and moving")
{
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  MDBDbi main = env.openDB("", MDB_CREATE);

  MDBRWCursor cursor;
  {
    MDBRWTransaction txn = env.getRWTransaction();
    MDBOutVal out;

    REQUIRE(txn->get(main, "lmdb", out) == MDB_NOTFOUND);

    // lets just keep this cursor to ensure that it invalidates
    cursor = txn->getRWCursor(main);
    txn->put(main, "bert", "hubert");
    txn->put(main, "bertt", "1975");
    txn->put(main, "berthubert", "lmdb");
    txn->put(main, "bert1", "one");
    txn->put(main, "beru", "not");

    MDBROTransaction ro_txn(std::move(txn));
    // despite being moved to an ro_txn (which normally commits instead of
    // aborting by default)
  }

  CHECK(!cursor);
}
