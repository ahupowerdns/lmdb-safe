
#LIBS=lmdb-LMDB_0.9.23/libraries/liblmdb/liblmdb.a
#INCLUDES=-Ilmdb-LMDB_0.9.23/libraries/liblmdb

LIBS=-llmdb
CXXVERSIONFLAG= -std=gnu++11
CXXFLAGS:= $(CXXVERSIONFLAG) -Wall -O2 -MMD -MP -ggdb -pthread $(INCLUDES) -Iext #  -fsanitize=address -fno-omit-frame-pointer
CFLAGS:= -Wall -O2 -MMD -MP -ggdb 

PROGRAMS = lmdb-various basic-example scale-example multi-example rel-example \
	resize-example typed-example testrunner lmdb-view

all: $(PROGRAMS)

clean:
	rm -f *~ *.o *.d test $(PROGRAMS)

-include *.d

testrunner: test-basic.o typed-test.o lmdb-safe.o lmdb-typed.o -lboost_serialization
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS) 	

lmdb-various: lmdb-various.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS) 

lmdb-view: lmdb-view.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ $(LIBS) 

basic-example: basic-example.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS) 

scale-example: scale-example.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS) 

multi-example: multi-example.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS) 

rel-example: rel-example.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS)  -lboost_serialization

resize-example: resize-example.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS)

typed-example: typed-example.o lmdb-typed.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS) -lboost_serialization
