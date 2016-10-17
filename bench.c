#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/mman.h>
#include "rdtsc.h"

#include "highwayhash/c_bindings.h"
#include "aes_gcm.h"

#ifdef USE_CACHE
#define MEM_INDEX 0
#else
#define MEM_INDEX i
#endif


const int SIZE = 512*1024*1024;
// AES-GCM: 128-bit or 256-bit, SipHash: 128-bit, HighwayHash: 256-bit
char* key = "DEADBEEFCAFEBABEDEADBABECAFEBEEFDEADBEEFCAFEBABEDEADBABECAFEBEEF";
// AES-GCM IV
char* iv = "DEADBEEFDEADBEEFDEADBEEFDEADBEEF";

uint32_t record_lens[] = {1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 64, 128, 256, 512, 1024, 2048};

pthread_barrier_t barrier;

typedef struct thread_state_s {
    uint8_t *memory;
    uint32_t record_len;
} thread_state_t;

void *thread_run_highwayhash(void *tstate_void) {
    thread_state_t *tstate = (thread_state_t*)tstate_void;

    pthread_barrier_wait(&barrier);
    uint64_t i = 0;
    // create tags
    for (; i+tstate->record_len+16 < SIZE; i+=tstate->record_len+16) {
        *((uint64_t*)&tstate->memory[MEM_INDEX+tstate->record_len]) = HighwayTreeHashC((uint64_t*)key, (char*)&tstate->memory[MEM_INDEX], tstate->record_len);
    }

    // check tags
    for (i=0; i+tstate->record_len+16 < SIZE; i+=tstate->record_len+16) {
        uint64_t tag = HighwayTreeHashC((uint64_t*)key, (char*)&tstate->memory[MEM_INDEX], tstate->record_len);
        if (*((uint64_t*)&tstate->memory[MEM_INDEX+tstate->record_len]) != tag) {
            exit(128);
        }
    }

    return NULL;
}

void *thread_run_siphash(void *tstate_void) {
    thread_state_t *tstate = (thread_state_t*)tstate_void;

    pthread_barrier_wait(&barrier);
    uint64_t i = 0;
    // create tags
    for (; i+tstate->record_len+16 < SIZE; i+=tstate->record_len+16) {
        *((uint64_t*)&tstate->memory[MEM_INDEX+tstate->record_len]) = SipTreeHashC((uint64_t*)key, (char*)&tstate->memory[MEM_INDEX], tstate->record_len);
    }

    // check tags
    for (i=0; i+tstate->record_len+16 < SIZE; i+=tstate->record_len+16) {
        uint64_t tag = SipTreeHashC((uint64_t*)key, (char*)&tstate->memory[MEM_INDEX], tstate->record_len);
        if (*((uint64_t*)&tstate->memory[MEM_INDEX+tstate->record_len]) != tag) {
            exit(128);
        }
    }

    return NULL;
}

void *thread_run_aesgcm(void *tstate_void) {
    thread_state_t *tstate = (thread_state_t*)tstate_void;
    struct gcm_data gdata;
    aesni_gcm128_pre(key, &gdata);

    pthread_barrier_wait(&barrier);
    uint64_t i = 0;
    // create tags
    for (; i+tstate->record_len+16 < SIZE; i+=tstate->record_len+16) {
        aesni_gcm128_dec(&gdata, NULL, NULL, 0, iv, &tstate->memory[MEM_INDEX], tstate->record_len, &tstate->memory[MEM_INDEX+tstate->record_len], 16);
    }

    // check tags
    for (i=0; i+tstate->record_len+16 < SIZE; i+=tstate->record_len+16) {
        uint8_t tag[16];
        aesni_gcm128_dec(&gdata, NULL, NULL, 0, iv, &tstate->memory[MEM_INDEX], tstate->record_len, &tag, 16);
        if (memcmp(&tag, &tstate->memory[MEM_INDEX+tstate->record_len], 16) != 0) {
            exit(128);
        }
    }

    return NULL;
}

void do_bench(uint32_t tcnt, uint32_t record_len, bool csv_out, pthread_t* tids, thread_state_t* tstates, char *name, void*(*c)(void*)) {
    pthread_barrier_init(&barrier, NULL, tcnt+1);

    uint32_t i;
    for (i=0; i < tcnt; i++) {
        tstates[i].record_len = record_len;
        pthread_create(&tids[i], NULL, c, (void*)&tstates[i]);
    }

    uint64_t begin = rdtsc();
    pthread_barrier_wait(&barrier);

    for (i=0; i < tcnt; i++) {
        pthread_join(tids[i], NULL);
    }
    double time = elapsed_secs(begin);
    // only account the record length
    uint64_t size = SIZE/(record_len+16)*record_len/1024/1024;
    // data is touched twice, first written, then checked
    size *= 2;

    if (csv_out) {
        printf("%s,%d,%d,%lu,%f,%f\n", name, tcnt, record_len, tcnt*size, time, tcnt*size/time);
    } else {
        printf("%s: %d Threads Record: %d %lu MB in %f secs %f MB/s\n", name, tcnt, record_len, tcnt*size, time, tcnt*size/time);
    }

    pthread_barrier_destroy(&barrier);
}

int main(int argc, char* argv[]) {
    // flush the results immediatly
    setbuf(stdout, NULL);
    int opt;

    uint32_t max_tcnt = 1;
    bool csv_out = false;

    while ((opt = getopt(argc, argv, "t:ch")) != -1) {
        switch(opt) {
        case 't':
            max_tcnt = strtol(optarg, NULL, 10);
            // TODO: error checking
            break;
        case 'c':
            csv_out = true;
            break;
        case 'h':
            fprintf(stderr,"Benchmark rdrand instruction\n-t n\tmaximal number of threads\n-c\toutput in CSV format\n");
            exit(1);
            break;
        default:
            break;
        }
    }

    uint64_t i, j;
    thread_state_t* tstates = (thread_state_t*)calloc(sizeof(thread_state_t), max_tcnt);
    pthread_t* tids = (pthread_t*)calloc(sizeof(pthread_t), max_tcnt);

    for (i=0; i < max_tcnt; i++) {
        tstates[i].memory = mmap(NULL, SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON|MAP_POPULATE, -1, 0);
        if (tstates[i].memory == NULL) {
            perror("mmap");
            exit(1);
        }
        tstates[i].record_len = 16;
    }

    if (csv_out) {
        printf("name,threads,record_len,size,seconds,throughput\n");
    }

    for (i=1; i <= max_tcnt; i++) {
        for (j=0; j < sizeof(record_lens)/sizeof(uint32_t); j++) {
            do_bench(i, record_lens[j], csv_out, tids, tstates, "highwayhash", thread_run_highwayhash);
        }
    }

    for (i=1; i <= max_tcnt; i++) {
        for (j=0; j < sizeof(record_lens)/sizeof(uint32_t); j++) {
            do_bench(i, record_lens[j], csv_out, tids, tstates, "siphash", thread_run_siphash);
        }
    }

    for (i=1; i <= max_tcnt; i++) {
        for (j=0; j < sizeof(record_lens)/sizeof(uint32_t); j++) {
            do_bench(i, record_lens[j], csv_out, tids, tstates, "aesgcm", thread_run_aesgcm);
        }
    }
}

