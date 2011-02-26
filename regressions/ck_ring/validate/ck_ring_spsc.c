#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <ck_ring.h>
#include "../../common.h"

#ifndef ITERATIONS
#define ITERATIONS 128 
#endif

struct context {
	unsigned int tid;
	unsigned int previous;
	unsigned int next;
};

struct entry {
	int tid;
	int value;
};

static int nthr;
static ck_ring_t *ring;
static struct affinity a;
static int size;
static volatile int barrier;

static void *
test(void *c)
{
	struct context *context = c;
	struct entry *entry;
	int i, j;
	bool r;

        if (aff_iterate(&a)) {
                perror("ERROR: Could not affine thread");
                exit(EXIT_FAILURE);
        }

	if (context->tid == 0) {
		struct entry *entries;

		entries = malloc(sizeof(struct entry) * size);
		assert(entries != NULL);

		for (i = 0; i < size; i++) {
			entries[i].value = i;
			entries[i].tid = 0;

			r = ck_ring_enqueue_spsc(ring + context->tid, entries + i);
		}

		barrier = 1;
	}

	while (barrier == 0);

	for (i = 0; i < ITERATIONS; i++) {
		for (j = 0; j < size; j++) {
			while (ck_ring_dequeue_spsc(ring + context->previous, &entry) == false);

			if (context->previous != (unsigned int)entry->tid) {
				fprintf(stderr, "[%u:%p] %u != %u\n",
					context->tid, (void *)entry, entry->tid, context->previous);
				exit(EXIT_FAILURE);
			}

			if (entry->value != j) {
				fprintf(stderr, "[%u:%p] %u != %u\n",
					context->tid, (void *)entry, entry->tid, context->previous);
				exit(EXIT_FAILURE);
			}

			entry->tid = context->tid;
			r = ck_ring_enqueue_spsc(ring + context->tid, entry);
			assert(r == true);
		}
	}

	return NULL;
}

int
main(int argc, char *argv[])
{
	int i, r;
	void *buffer;
	struct context *context;
	pthread_t *thread;

	if (argc != 4) {
		fprintf(stderr, "Usage: validate <threads> <affinity delta> <size>\n");
		exit(EXIT_FAILURE);
	}

	a.request = 0;
	a.delta = atoi(argv[2]);

	nthr = atoi(argv[1]);
	assert(nthr >= 1);

	size = atoi(argv[3]);
	assert(size > 0);

	ring = malloc(sizeof(ck_ring_t) * nthr);
	assert(ring);

	context = malloc(sizeof(*context) * nthr);
	assert(context);

	thread = malloc(sizeof(pthread_t) * nthr);
	assert(thread);

	for (i = 0; i < nthr; i++) {
		context[i].tid = i;
		if (i == 0) {
			context[i].previous = nthr - 1;
			context[i].next = i + 1;
		} else if (i == nthr - 1) {
			context[i].next = 0;
			context[i].previous = i - 1;
		} else {
			context[i].next = i + 1;
			context[i].previous = i - 1;
		}

		buffer = malloc(sizeof(void *) * (size + 1));
		assert(buffer);
		ck_ring_init(ring + i, buffer, size + 1);
		r = pthread_create(thread + i, NULL, test, context + i);
		assert(r == 0);
	}

	for (i = 0; i < nthr; i++)
		pthread_join(thread[i], NULL);

	return (0);
}