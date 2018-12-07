CXXFLAGS:=-std=gnu++11 -Wall -O2 -MMD -MP -ggdb -pthread 
CFLAGS:= -Wall -O2 -MMD -MP -ggdb 

PROGRAMS = lmdb-test

all: $(PROGRAMS)

clean:
	rm -f *~ *.o *.d test $(PROGRAMS)

-include *.d


lmdb-test: lmdb-test.o 
	g++ -std=gnu++11 $^ -o $@ -pthread -llmdb

h2o-simple: h2o-simple.o h2o-pp.o ext/simplesocket/comboaddress.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lh2o-evloop -lssl -lcrypto -lz

h2o-real: h2o-real.o h2o-pp.o ext/simplesocket/comboaddress.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lh2o-evloop -lssl -lcrypto -lz

h2o-stream: h2o-stream.o h2o-pp.o ext/simplesocket/comboaddress.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lh2o-evloop -lsqlite3 -lssl -lcrypto -lz
