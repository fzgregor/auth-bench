COMMON_FLAGS = -g

CXXFLAGS := -std=c++11 -O3 -mavx2 -Wall -Ihighwayhash/ $(COMMON_FLAGS)

HASH_OBJS := $(addprefix highwayhash/highwayhash/, \
        os_specific.o \
        sip_hash.o \
        sip_tree_hash.o \
        scalar_sip_tree_hash.o \
        highway_tree_hash.o \
        scalar_highway_tree_hash.o \
        sse41_highway_tree_hash.o)


all: bench

bench.o: bench.c
	$(CC) $(COMMON_FLAGS) $(CFLAGS) -Irdtsc/ -Ihighwayhash/ -Ilibaesgcm/include/ -O3 -c -o $@ $^

libaesgcm/libaesgcm.a:
	cd libaesgcm && make

bench: $(HASH_OBJS) bench.o libaesgcm/libaesgcm.a
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread

clean:
	rm -f $(HASH_OBJS) bench bench.o libaesgcm/libaesgcm.a

.PHONY: all clean
