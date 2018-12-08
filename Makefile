
LIBS=lmdb-LMDB_0.9.22/libraries/liblmdb/liblmdb.a
INCLUDES=-Ilmdb-LMDB_0.9.22/libraries/liblmdb

#LIBS=-llmdb
CXXFLAGS:=-std=gnu++17 -Wall -O2 -MMD -MP -ggdb -pthread $(INCLUDES) #  -fsanitize=address -fno-omit-frame-pointer
CFLAGS:= -Wall -O2 -MMD -MP -ggdb 


PROGRAMS = lmdb-test basic-example

all: $(PROGRAMS)

clean:
	rm -f *~ *.o *.d test $(PROGRAMS)

-include *.d


lmdb-test: lmdb-test.o lmdb-safe.o
	g++ -std=gnu++17 $^ -o $@ -pthread $(LIBS) #-lasan

basic-example: basic-example.o lmdb-safe.o
	g++ -std=gnu++17 $^ -o $@ -pthread $(LIBS) 
