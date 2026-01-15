/*
 * SPECInt2006-micro: graph_simplex kernel
 * Captures network simplex from 429.mcf
 *
 * Pattern: Graph traversal with pointer-based data structures
 * Memory: Random pointer chasing through node/arc structures
 * Branch: Data-dependent path selection
 */

#include "bench.h"


/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef GRAPH_NUM_NODES
#define GRAPH_NUM_NODES     64
#endif

#ifndef GRAPH_NUM_ARCS
#define GRAPH_NUM_ARCS      256
#endif

#ifndef SIMPLEX_ITERATIONS
#define SIMPLEX_ITERATIONS  50
#endif

/* Arc states */
#define ARC_AT_LOWER    0
#define ARC_AT_UPPER    1
#define ARC_BASIC       2

/* ============================================================================
 * Data Structures (similar to MCF)
 * ============================================================================ */

typedef struct arc arc_t;
typedef struct node node_t;

struct arc {
    node_t  *tail;          /* Source node */
    node_t  *head;          /* Target node */
    int32_t cost;           /* Arc cost */
    int32_t capacity;       /* Arc capacity */
    int32_t flow;           /* Current flow */
    uint8_t ident;          /* Arc state */
};

struct node {
    arc_t   *basic_arc;     /* Tree arc to parent */
    node_t  *pred;          /* Predecessor in tree */
    int32_t potential;      /* Node potential */
    int32_t balance;        /* Supply/demand */
    int32_t depth;          /* Tree depth */
    uint8_t orientation;    /* Arc direction in tree */
};

/* Static storage */
static node_t nodes[GRAPH_NUM_NODES + 1];   /* 1-indexed */
static arc_t arcs[GRAPH_NUM_ARCS];

/* ============================================================================
 * Network Simplex Operations
 * ============================================================================ */

/* Compute reduced cost */
INLINE int32_t reduced_cost(const arc_t *arc)
{
    return arc->cost - arc->tail->potential + arc->head->potential;
}

/* Find entering arc (pricing) */
static arc_t *primal_bea(void)
{
    arc_t *best_arc = NULL;
    int32_t best_rc = 0;

    for (int i = 0; i < GRAPH_NUM_ARCS; i++) {
        arc_t *arc = &arcs[i];

        if (arc->ident == ARC_BASIC) continue;

        int32_t rc = reduced_cost(arc);

        if (arc->ident == ARC_AT_LOWER && rc < best_rc) {
            best_rc = rc;
            best_arc = arc;
        } else if (arc->ident == ARC_AT_UPPER && rc > -best_rc) {
            best_rc = -rc;
            best_arc = arc;
        }
    }

    return best_arc;
}

/* Find leaving arc and compute flow change */
static arc_t *ratio_test(arc_t *entering, int32_t *delta)
{
    node_t *i = entering->tail;
    node_t *j = entering->head;
    arc_t *leaving = entering;

    /* Compute maximum flow change */
    int32_t max_delta = entering->capacity - entering->flow;
    if (entering->ident == ARC_AT_UPPER) {
        max_delta = entering->flow;
    }

    /* Trace path from tail to root */
    node_t *node = i;
    while (node->pred) {
        arc_t *arc = node->basic_arc;
        int32_t cap;

        if (node->orientation) {
            cap = arc->flow;
        } else {
            cap = arc->capacity - arc->flow;
        }

        if (cap < max_delta) {
            max_delta = cap;
            leaving = arc;
        }

        node = node->pred;
    }

    /* Trace path from head to root */
    node = j;
    while (node->pred) {
        arc_t *arc = node->basic_arc;
        int32_t cap;

        if (node->orientation) {
            cap = arc->capacity - arc->flow;
        } else {
            cap = arc->flow;
        }

        if (cap < max_delta) {
            max_delta = cap;
            leaving = arc;
        }

        node = node->pred;
    }

    *delta = max_delta;
    return leaving;
}

/* Update tree structure */
static void update_tree(arc_t *entering, arc_t *leaving, int32_t delta)
{
    /* Update flows on path */
    node_t *node = entering->tail;
    while (node->pred) {
        arc_t *arc = node->basic_arc;
        if (node->orientation) {
            arc->flow -= delta;
        } else {
            arc->flow += delta;
        }
        node = node->pred;
    }

    node = entering->head;
    while (node->pred) {
        arc_t *arc = node->basic_arc;
        if (node->orientation) {
            arc->flow += delta;
        } else {
            arc->flow -= delta;
        }
        node = node->pred;
    }

    /* Update entering arc flow */
    if (entering->ident == ARC_AT_LOWER) {
        entering->flow += delta;
    } else {
        entering->flow -= delta;
    }

    /* Update tree if entering != leaving */
    if (entering != leaving) {
        /* Mark leaving arc as non-basic */
        if (leaving->flow == 0) {
            leaving->ident = ARC_AT_LOWER;
        } else {
            leaving->ident = ARC_AT_UPPER;
        }

        /* Mark entering arc as basic */
        entering->ident = ARC_BASIC;

        /* Update tree structure (simplified) */
        entering->head->basic_arc = entering;
        entering->head->pred = entering->tail;
        entering->head->orientation = 0;
    }
}

/* Update potentials */
static void update_potentials(void)
{
    /* BFS to update potentials from root */
    node_t *queue[GRAPH_NUM_NODES];
    int front = 0, back = 0;

    /* Root has potential 0 */
    nodes[1].potential = 0;
    queue[back++] = &nodes[1];

    while (front < back) {
        node_t *node = queue[front++];

        /* Update children */
        for (int i = 0; i < GRAPH_NUM_ARCS; i++) {
            arc_t *arc = &arcs[i];
            if (arc->ident != ARC_BASIC) continue;

            node_t *child = NULL;
            int32_t pot;

            if (arc->tail == node && arc->head->pred == node) {
                child = arc->head;
                pot = node->potential + arc->cost;
            } else if (arc->head == node && arc->tail->pred == node) {
                child = arc->tail;
                pot = node->potential - arc->cost;
            }

            if (child && child != node) {
                child->potential = pot;
                queue[back++] = child;
            }
        }
    }
}

/* Compute total cost */
static int64_t compute_cost(void)
{
    int64_t total = 0;
    for (int i = 0; i < GRAPH_NUM_ARCS; i++) {
        total += (int64_t)arcs[i].cost * arcs[i].flow;
    }
    return total;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void init_network(uint32_t seed)
{
    uint32_t x = seed;

    /* Initialize nodes */
    for (int i = 0; i <= GRAPH_NUM_NODES; i++) {
        nodes[i].basic_arc = NULL;
        nodes[i].pred = NULL;
        nodes[i].potential = 0;
        nodes[i].balance = 0;
        nodes[i].depth = 0;
        nodes[i].orientation = 0;
    }

    /* Set supply/demand (balanced) */
    int32_t total_supply = 0;
    for (int i = 1; i <= GRAPH_NUM_NODES / 2; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        nodes[i].balance = 10 + (x % 90);
        total_supply += nodes[i].balance;
    }
    for (int i = GRAPH_NUM_NODES / 2 + 1; i <= GRAPH_NUM_NODES; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int32_t demand = 10 + (x % 90);
        nodes[i].balance = -demand;
        total_supply -= demand;
    }
    /* Adjust last node for balance */
    nodes[GRAPH_NUM_NODES].balance -= total_supply;

    /* Initialize arcs */
    for (int i = 0; i < GRAPH_NUM_ARCS; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int tail = 1 + (x % GRAPH_NUM_NODES);
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int head = 1 + (x % GRAPH_NUM_NODES);

        if (tail == head) head = (head % GRAPH_NUM_NODES) + 1;

        arcs[i].tail = &nodes[tail];
        arcs[i].head = &nodes[head];
        arcs[i].cost = 1 + (x % 100);
        arcs[i].capacity = 50 + (x % 200);
        arcs[i].flow = 0;
        arcs[i].ident = ARC_AT_LOWER;
    }

    /* Build initial tree (simple star from node 1) */
    for (int i = 2; i <= GRAPH_NUM_NODES; i++) {
        nodes[i].pred = &nodes[1];
        nodes[i].basic_arc = &arcs[i - 2];
        arcs[i - 2].ident = ARC_BASIC;
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    init_network(0xCAFEBABE);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    int iterations = 0;

    /* Start timing */
    BENCH_START();

    /* Run simplex iterations */
    for (int i = 0; i < SIMPLEX_ITERATIONS; i++) {
        /* Find entering arc */
        arc_t *entering = primal_bea();
        if (!entering) break;  /* Optimal */

        /* Find leaving arc */
        int32_t delta;
        arc_t *leaving = ratio_test(entering, &delta);

        /* Update tree and flows */
        update_tree(entering, leaving, delta);

        /* Update potentials periodically */
        if (i % 10 == 0) {
            update_potentials();
        }

        iterations++;
    }

    int64_t final_cost = compute_cost();

    /* End timing */
    BENCH_END();

    /* Checksum */
    uint32_t csum = checksum_init();
    csum = checksum_update(csum, (uint32_t)final_cost);
    csum = checksum_update(csum, (uint32_t)(final_cost >> 32));
    csum = checksum_update(csum, (uint32_t)iterations);

    /* Prevent optimization */
    BENCH_VOLATILE(final_cost);

    result.cycles = BENCH_CYCLES();
    result.checksum = csum;

    return result;
}

static void kernel_cleanup_func(void)
{
}

/* ============================================================================
 * Kernel Registration
 * ============================================================================ */

KERNEL_DECLARE(
    graph_simplex,
    "Network simplex algorithm",
    "429.mcf",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    SIMPLEX_ITERATIONS
);

KERNEL_REGISTER(graph_simplex)
