#define CATCH_CONFIG_MAIN

#include <iostream>
#include "catch2/catch.hpp"
#include "lmdb-safe.hh"

using namespace std;
using namespace lmdb_safe;

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

TEST_CASE("nested RW transactions", "[transactions]")
{
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  REQUIRE(1);

  MDBDbi main = env.openDB("", MDB_CREATE);

  /* bootstrap some data */
  {
    auto txn = env.getRWTransaction();
    txn->put(main, "bert", "hubert");
    txn->put(main, "bertt", "1975");
    txn->put(main, "berthubert", "lmdb");
    txn->put(main, "bert1", "one");
    txn->put(main, "beru", "not");
    txn->commit();
  }

  auto main_txn = env.getRWTransaction();
  main_txn->del(main, "bertt", "1975");

  MDBOutVal dummy{};
  CHECK(main_txn->get(main, "bertt", dummy) == MDB_NOTFOUND);

  {
    auto sub_txn = main_txn->getRWTransaction();
    CHECK(sub_txn->get(main, "berthubert", dummy) == 0);
    sub_txn->del(main, "berthubert", "lmdb");
    CHECK(sub_txn->get(main, "berthubert", dummy) == MDB_NOTFOUND);
  }

  /* check that subtransaction got rolled back */
  CHECK(main_txn->get(main, "berthubert", dummy) == 0);
  /* and that the main changes are still there */
  CHECK(main_txn->get(main, "bertt", dummy) == MDB_NOTFOUND);

  {
    auto sub_txn = main_txn->getRWTransaction();
    CHECK(sub_txn->get(main, "berthubert", dummy) == 0);
    sub_txn->del(main, "berthubert", "lmdb");
    CHECK(sub_txn->get(main, "berthubert", dummy) == MDB_NOTFOUND);
    /* this time for real! */
    sub_txn->commit();
  }

  CHECK(main_txn->get(main, "berthubert", dummy) == MDB_NOTFOUND);
  CHECK(main_txn->get(main, "bertt", dummy) == MDB_NOTFOUND);
}


TEST_CASE("nesting RW -> RO", "[transactions]")
{
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  REQUIRE(1);

  MDBDbi main = env.openDB("", MDB_CREATE);

  /* bootstrap some data */
  {
    auto txn = env.getRWTransaction();
    txn->put(main, "bert", "hubert");
    txn->put(main, "bertt", "1975");
    txn->put(main, "berthubert", "lmdb");
    txn->put(main, "bert1", "one");
    txn->put(main, "beru", "not");
    txn->commit();
  }

  auto main_txn = env.getRWTransaction();
  main_txn->del(main, "bertt", "1975");

  MDBOutVal dummy{};
  CHECK(main_txn->get(main, "bertt", dummy) == MDB_NOTFOUND);

  {
    MDBROTransaction sub_txn = main_txn->getROTransaction();
    CHECK(sub_txn->get(main, "berthubert", dummy) == 0);
  }

  /* check that subtransaction got rolled back */
  CHECK(main_txn->get(main, "berthubert", dummy) == 0);
  /* and that the main changes are still there */
  CHECK(main_txn->get(main, "bertt", dummy) == MDB_NOTFOUND);

  {
    auto sub_txn = main_txn->getRWTransaction();
    CHECK(sub_txn->get(main, "berthubert", dummy) == 0);
    sub_txn->del(main, "berthubert", "lmdb");
    CHECK(sub_txn->get(main, "berthubert", dummy) == MDB_NOTFOUND);
    {
      MDBROTransaction sub_sub_txn = sub_txn->getROTransaction();
      CHECK(sub_sub_txn->get(main, "berthubert", dummy) == MDB_NOTFOUND);
    }
    /* this time for real! */
    sub_txn->commit();
  }

  CHECK(main_txn->get(main, "berthubert", dummy) == MDB_NOTFOUND);
  CHECK(main_txn->get(main, "bertt", dummy) == MDB_NOTFOUND);
}

TEST_CASE("try to nest twice", "[transactions]")
{
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  REQUIRE(1);

  MDBDbi main = env.openDB("", MDB_CREATE);

  /* bootstrap some data */
  {
    auto txn = env.getRWTransaction();
    txn->put(main, "bert", "hubert");
    txn->put(main, "bertt", "1975");
    txn->put(main, "berthubert", "lmdb");
    txn->put(main, "bert1", "one");
    txn->put(main, "beru", "not");
    txn->commit();
  }

  auto main_txn = env.getRWTransaction();
  main_txn->del(main, "bertt", "1975");

  MDBOutVal dummy{};
  CHECK(main_txn->get(main, "bertt", dummy) == MDB_NOTFOUND);

  {
    auto sub_txn = main_txn->getRWTransaction();
    CHECK(sub_txn->get(main, "berthubert", dummy) == 0);
    sub_txn->del(main, "berthubert", "lmdb");
    CHECK(sub_txn->get(main, "berthubert", dummy) == MDB_NOTFOUND);

    CHECK_THROWS_AS(
          main_txn->getRWTransaction(),
          std::runtime_error
          );
  }
}

TEST_CASE("transaction counter correctness for RW->RW nesting")
{
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  REQUIRE(1);

  MDBDbi main = env.openDB("", MDB_CREATE);

  {
    auto txn = env.getRWTransaction();
    auto sub_txn = txn->getRWTransaction();
  }

  CHECK_NOTHROW(env.getRWTransaction());
  CHECK_NOTHROW(env.getROTransaction());
}

TEST_CASE("transaction counter correctness for RW->RO nesting")
{
  unlink("./tests");

  MDBEnv env("./tests", MDB_NOSUBDIR, 0600);
  REQUIRE(1);

  MDBDbi main = env.openDB("", MDB_CREATE);

  {
    auto txn = env.getRWTransaction();
    auto sub_txn = txn->getROTransaction();
  }

  CHECK_NOTHROW(env.getRWTransaction());
  CHECK_NOTHROW(env.getROTransaction());
}
