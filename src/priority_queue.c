/*
 * SPECInt2006-micro: priority_queue kernel
 * Captures event queue operations from 471.omnetpp
 *
 * Pattern: Binary heap insert/extract operations
 * Memory: Array-based heap with bubble up/down
 * Branch: Data-dependent comparisons
 */

#include "bench.h"


/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef PQ_CAPACITY
#define PQ_CAPACITY         512     /* Maximum queue size */
#endif

#ifndef PQ_OPERATIONS
#define PQ_OPERATIONS       256     /* Number of insert+extract operations */
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Event structure (similar to OMNeT++ cMessage) */
typedef struct {
    uint64_t timestamp;         /* Event time (priority key) */
    uint32_t event_id;          /* Event identifier */
    uint32_t module_id;         /* Target module */
    int32_t  priority;          /* Secondary priority */
    void    *data;              /* Event data pointer */
} event_t;

/* Priority queue (binary min-heap) */
typedef struct {
    event_t *heap;              /* Heap array */
    int     size;               /* Current number of elements */
    int     capacity;           /* Maximum capacity */
} pqueue_t;

/* Static storage */
static event_t heap_storage[PQ_CAPACITY + 1];   /* 1-indexed heap */
static pqueue_t pq;

/* ============================================================================
 * Priority Queue Operations
 * ============================================================================ */

/* Compare events: returns true if a should come before b */
INLINE bool event_less(const event_t *a, const event_t *b)
{
    if (a->timestamp != b->timestamp) {
        return a->timestamp < b->timestamp;
    }
    if (a->priority != b->priority) {
        return a->priority < b->priority;
    }
    return a->event_id < b->event_id;
}

/* Initialize priority queue */
static void pq_init(pqueue_t *q, event_t *storage, int capacity)
{
    q->heap = storage;
    q->size = 0;
    q->capacity = capacity;
}

/* Check if queue is empty */
INLINE bool pq_empty(const pqueue_t *q)
{
    return q->size == 0;
}

/* Check if queue is full */
INLINE bool pq_full(const pqueue_t *q)
{
    return q->size >= q->capacity;
}

/* Bubble up (after insert) */
static void pq_bubble_up(pqueue_t *q, int pos)
{
    event_t temp = q->heap[pos];

    while (pos > 1) {
        int parent = pos / 2;
        if (!event_less(&temp, &q->heap[parent])) {
            break;
        }
        q->heap[pos] = q->heap[parent];
        pos = parent;
    }

    q->heap[pos] = temp;
}

/* Bubble down (after extract) */
static void pq_bubble_down(pqueue_t *q, int pos)
{
    event_t temp = q->heap[pos];
    int size = q->size;

    while (pos * 2 <= size) {
        int child = pos * 2;

        /* Find smaller child */
        if (child < size && event_less(&q->heap[child + 1], &q->heap[child])) {
            child++;
        }

        /* Check if we're done */
        if (!event_less(&q->heap[child], &temp)) {
            break;
        }

        /* Move child up */
        q->heap[pos] = q->heap[child];
        pos = child;
    }

    q->heap[pos] = temp;
}

/* Insert event into queue */
static bool pq_insert(pqueue_t *q, const event_t *event)
{
    if (pq_full(q)) {
        return false;
    }

    /* Insert at end */
    q->size++;
    q->heap[q->size] = *event;

    /* Bubble up */
    pq_bubble_up(q, q->size);

    return true;
}

/* Extract minimum event from queue */
static bool pq_extract(pqueue_t *q, event_t *out)
{
    if (pq_empty(q)) {
        return false;
    }

    /* Get minimum (root) */
    *out = q->heap[1];

    /* Move last element to root */
    q->heap[1] = q->heap[q->size];
    q->size--;

    /* Bubble down */
    if (q->size > 0) {
        pq_bubble_down(q, 1);
    }

    return true;
}

/* Peek at minimum without removing */
__attribute__((unused))
static const event_t *pq_peek(const pqueue_t *q)
{
    if (pq_empty(q)) {
        return NULL;
    }
    return &q->heap[1];
}

/* Remove event at specific position (for cancellation) */
static bool pq_remove_at(pqueue_t *q, int pos)
{
    if (pos < 1 || pos > q->size) {
        return false;
    }

    /* Replace with last element */
    event_t removed = q->heap[pos];
    q->heap[pos] = q->heap[q->size];
    q->size--;

    if (pos <= q->size) {
        /* Check if we need to bubble up or down */
        if (pos > 1 && event_less(&q->heap[pos], &q->heap[pos / 2])) {
            pq_bubble_up(q, pos);
        } else {
            pq_bubble_down(q, pos);
        }
    }

    (void)removed;  /* Avoid unused warning */
    return true;
}

/* ============================================================================
 * Simulation Workload
 * ============================================================================ */

/* Simulate discrete event processing */
static uint32_t simulate_events(pqueue_t *q, uint32_t seed)
{
    uint32_t x = seed;
    uint64_t current_time = 0;
    uint32_t events_processed = 0;
    uint32_t checksum = checksum_init();

    /* Initial events */
    for (int i = 0; i < PQ_OPERATIONS / 2; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;

        event_t e = {
            .timestamp = current_time + (x % 1000),
            .event_id = i,
            .module_id = x % 16,
            .priority = (x >> 16) % 10,
            .data = NULL
        };

        pq_insert(q, &e);
    }

    /* Process events and generate new ones */
    for (int i = 0; i < PQ_OPERATIONS; i++) {
        event_t e;

        if (pq_extract(q, &e)) {
            events_processed++;
            current_time = e.timestamp;

            /* Update checksum with event data */
            checksum = checksum_update(checksum, (uint32_t)e.timestamp);
            checksum = checksum_update(checksum, e.event_id);
            checksum = checksum_update(checksum, e.module_id);

            /* Generate 0-2 new events based on this event */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            int new_events = x % 3;

            for (int j = 0; j < new_events && !pq_full(q); j++) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;

                event_t new_e = {
                    .timestamp = current_time + 1 + (x % 500),
                    .event_id = events_processed * 10 + j,
                    .module_id = (e.module_id + (x % 4)) % 16,
                    .priority = (x >> 8) % 10,
                    .data = NULL
                };

                pq_insert(q, &new_e);
            }

            /* Occasionally cancel a random event */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            if ((x % 10) == 0 && q->size > 5) {
                int cancel_pos = 1 + (x % q->size);
                pq_remove_at(q, cancel_pos);
            }
        }
    }

    /* Drain remaining events */
    while (!pq_empty(q)) {
        event_t e;
        pq_extract(q, &e);
        events_processed++;
        checksum = checksum_update(checksum, (uint32_t)e.timestamp);
    }

    checksum = checksum_update(checksum, events_processed);
    return checksum;
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    /* Initialize priority queue */
    pq_init(&pq, heap_storage, PQ_CAPACITY);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };

    /* Reset queue */
    pq.size = 0;

    /* Start timing */
    BENCH_START();

    /* Run simulation */
    uint32_t csum = simulate_events(&pq, 0xDEADBEEF);

    /* End timing */
    BENCH_END();

    /* Prevent optimization */
    BENCH_VOLATILE(csum);

    result.cycles = BENCH_CYCLES();
    result.checksum = csum;

    return result;
}

static void kernel_cleanup_func(void)
{
    /* Nothing to clean up */
}

/* ============================================================================
 * Kernel Registration
 * ============================================================================ */

KERNEL_DECLARE(
    priority_queue,
    "Priority queue operations",
    "471.omnetpp",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    PQ_OPERATIONS
);

KERNEL_REGISTER(priority_queue)
