
#LIBS=lmdb-LMDB_0.9.22/libraries/liblmdb/liblmdb.a
#INCLUDES=-Ilmdb-LMDB_0.9.22/libraries/liblmdb

LIBS=-llmdb
CXXVERSIONFLAG= -std=gnu++11
CXXFLAGS:= $(CXXVERSIONFLAG) -Wall -O2 -MMD -MP -ggdb -pthread $(INCLUDES) #  -fsanitize=address -fno-omit-frame-pointer
CFLAGS:= -Wall -O2 -MMD -MP -ggdb 



PROGRAMS = lmdb-test basic-example scale-example multi-example rel-example \
	resize-example lmdb-typed

all: $(PROGRAMS)

clean:
	rm -f *~ *.o *.d test $(PROGRAMS)

-include *.d


lmdb-test: lmdb-test.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS) #-lasan

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

lmdb-typed: lmdb-typed.o lmdb-safe.o
	g++ $(CXXVERSIONFLAG) $^ -o $@ -pthread $(LIBS) -lboost_serialization
