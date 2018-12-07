
#LIBS=-Llmdb-0.9.21/libraries/liblmdb/
#INCLUDES=-Ilmdb-0.9.21/libraries/liblmdb/

CXXFLAGS:=-std=gnu++11 -Wall -O2 -MMD -MP -ggdb -pthread $(INCLUDES)
CFLAGS:= -Wall -O2 -MMD -MP -ggdb 


PROGRAMS = lmdb-test

all: $(PROGRAMS)

clean:
	rm -f *~ *.o *.d test $(PROGRAMS)

-include *.d


lmdb-test: lmdb-test.o lmdb-safe.o
	g++ -std=gnu++11 $^ -o $@ -pthread $(LIBS)  -llmdb 

