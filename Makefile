CXX       = g++
CXX_STD   = -std=c++17
CXX_WARN  = -Wall -Wextra -Wpedantic
CXX_DEBUG = -ggdb3 -DDEBUG -Og
CXX_SAN   = -fsanitize=address,leak,undefined
LIBS      = -pthread -lboost_program_options
CXX_NOSAN = $(CXX_STD) $(CXX_WARN) $(CXX_DEBUG) $(LIBS)
CXX_FLAGS = $(CXX_NOSAN) $(CXX_SAN)
TARGETS   = test_cache_client cache_server test_cache_store # test_evictors
SOURCE    = test_cache_client.cc cache_client.cc fifo_evictor.cc test_cache_store.cc # test_evictors.cc lru_evictor.cc
TEXT      = cache_server.cc cache_client.cc fifo_evictor.cc
OBJ       = $(SRC:.cc=.o)

all:  $(TARGETS)

cache_server: cache_server.o cache_store.o fifo_evictor.o
	$(CXX) $(CXX_FLAGS) -o $@ $^ $(LIBS)

# test_evictors: test_evictors.o
# 	$(CXX) $(CXX_FLAGS) -o $@ $^ $(LIBS)

test_cache_client: test_cache_client.o cache_client.o fifo_evictor.o
	$(CXX) $(CXX_FLAGS) -o $@ $^ $(LIBS)

test_cache_store: test_cache_store.o fifo_evictor.o cache_store.o
	$(CXX) $(CXX_FLAGS) -o $@ $^ $(LIBS)

%.o: %.cc %.hh
	$(CXX) $(CXX_FLAGS) $(OPTFLAGS) -c -o $@ $<

clean:
	rm -fv *.o $(TARGETS)

grind:
	$(CXX) $(CXX_NOSAN) -o test_cache_store $(SOURCE)
	valgrind -s --leak-check=full --show-leak-kinds=all --track-origins=yes ./test_cache_store

warn: $(TEXT)
	clang -Weverything -Wno-c++98-compat $^

tidy: $(TEXT)
	clang-tidy -extra-arg=$(CXX_STD) $^

format: $(TEXT)
	clang-format -style="{BasedOnStyle: google, IndentWidth: 4}" -i --verbose $^

lint: $(TEXT)
	cpplint --filter=-legal/copyright $^
