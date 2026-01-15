/*
 * SPECInt2006-micro: huffman_tree kernel
 * Captures Huffman tree construction from 401.bzip2
 *
 * Pattern: Min-heap based tree construction
 * Memory: Array-based heap with moderate locality
 * Branch: Heap operations with data-dependent swaps
 */

#include "bench.h"


/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef HUFFMAN_SYMBOLS
#define HUFFMAN_SYMBOLS     256     /* Number of symbols */
#endif

#ifndef HUFFMAN_MAX_LEN
#define HUFFMAN_MAX_LEN     20      /* Maximum code length */
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Huffman node */
typedef struct {
    int32_t weight;         /* Symbol frequency or combined weight */
    int32_t parent;         /* Parent node index (-1 if root) */
    int32_t left;           /* Left child index (-1 if leaf) */
    int32_t right;          /* Right child index (-1 if leaf) */
    int32_t symbol;         /* Symbol value (for leaves) */
} huffman_node_t;

/* Static storage */
static int32_t frequencies[HUFFMAN_SYMBOLS];
static huffman_node_t nodes[2 * HUFFMAN_SYMBOLS];
static uint8_t code_lengths[HUFFMAN_SYMBOLS];
static int32_t heap[HUFFMAN_SYMBOLS + 1];
static int heap_size;

/* ============================================================================
 * Min-Heap Operations
 * ============================================================================ */

static void huffman_heap_init(void)
{
    heap_size = 0;
}

static void heap_push(int32_t node_idx, int32_t weight)
{
    /* Insert at end */
    int pos = ++heap_size;

    /* Bubble up */
    while (pos > 1) {
        int parent = pos / 2;
        if (nodes[heap[parent]].weight <= weight) {
            break;
        }
        heap[pos] = heap[parent];
        pos = parent;
    }

    heap[pos] = node_idx;
}

static int32_t heap_pop(void)
{
    if (heap_size == 0) {
        return -1;
    }

    int32_t min_node = heap[1];
    int32_t last = heap[heap_size--];

    /* Bubble down */
    int pos = 1;
    while (pos * 2 <= heap_size) {
        int child = pos * 2;

        /* Find smaller child */
        if (child < heap_size &&
            nodes[heap[child + 1]].weight < nodes[heap[child]].weight) {
            child++;
        }

        if (nodes[last].weight <= nodes[heap[child]].weight) {
            break;
        }

        heap[pos] = heap[child];
        pos = child;
    }

    heap[pos] = last;
    return min_node;
}

/* ============================================================================
 * Huffman Tree Construction
 * ============================================================================ */

/* Build Huffman tree and return root index */
static int32_t huffman_build_tree(const int32_t *freq, int num_symbols)
{
    int32_t num_nodes = 0;

    /* Initialize heap */
    huffman_heap_init();

    /* Create leaf nodes for non-zero frequencies */
    for (int i = 0; i < num_symbols; i++) {
        if (freq[i] > 0) {
            nodes[num_nodes].weight = freq[i];
            nodes[num_nodes].parent = -1;
            nodes[num_nodes].left = -1;
            nodes[num_nodes].right = -1;
            nodes[num_nodes].symbol = i;
            heap_push(num_nodes, freq[i]);
            num_nodes++;
        }
    }

    /* Handle edge case: only one symbol */
    if (heap_size == 1) {
        return heap[1];
    }

    /* Build tree by combining nodes */
    while (heap_size > 1) {
        /* Extract two minimum nodes */
        int32_t left = heap_pop();
        int32_t right = heap_pop();

        /* Create combined node */
        int32_t combined_weight = nodes[left].weight + nodes[right].weight;
        nodes[num_nodes].weight = combined_weight;
        nodes[num_nodes].parent = -1;
        nodes[num_nodes].left = left;
        nodes[num_nodes].right = right;
        nodes[num_nodes].symbol = -1;

        /* Set parent pointers */
        nodes[left].parent = num_nodes;
        nodes[right].parent = num_nodes;

        /* Insert combined node */
        heap_push(num_nodes, combined_weight);
        num_nodes++;
    }

    return heap[1];  /* Root */
}

/* Compute code lengths from tree */
static void compute_code_lengths(int32_t root, uint8_t *lengths, int num_symbols)
{
    /* Clear lengths */
    memset(lengths, 0, num_symbols);

    /* DFS traversal using stack */
    struct {
        int32_t node;
        uint8_t depth;
    } stack[HUFFMAN_SYMBOLS * 2];
    int stack_top = 0;

    stack[stack_top].node = root;
    stack[stack_top].depth = 0;
    stack_top++;

    while (stack_top > 0) {
        stack_top--;
        int32_t node = stack[stack_top].node;
        uint8_t depth = stack[stack_top].depth;

        if (nodes[node].left == -1) {
            /* Leaf node */
            int32_t symbol = nodes[node].symbol;
            if (symbol >= 0 && symbol < num_symbols) {
                lengths[symbol] = depth > 0 ? depth : 1;
            }
        } else {
            /* Internal node - push children */
            stack[stack_top].node = nodes[node].left;
            stack[stack_top].depth = depth + 1;
            stack_top++;

            stack[stack_top].node = nodes[node].right;
            stack[stack_top].depth = depth + 1;
            stack_top++;
        }
    }
}

/* Limit code lengths (bzip2 requires max 20 bits) */
static void limit_code_lengths(uint8_t *lengths, int num_symbols, int max_len)
{
    bool changed = true;

    while (changed) {
        changed = false;

        for (int i = 0; i < num_symbols; i++) {
            if (lengths[i] > max_len) {
                lengths[i] = max_len;
                changed = true;
            }
        }

        /* Verify Kraft inequality and adjust if needed */
        int64_t kraft_sum = 0;
        for (int i = 0; i < num_symbols; i++) {
            if (lengths[i] > 0) {
                kraft_sum += 1LL << (max_len - lengths[i]);
            }
        }

        int64_t max_kraft = 1LL << max_len;

        /* If sum exceeds limit, increment shortest codes */
        if (kraft_sum > max_kraft) {
            for (int i = 0; i < num_symbols && kraft_sum > max_kraft; i++) {
                if (lengths[i] > 0 && lengths[i] < max_len) {
                    kraft_sum -= 1LL << (max_len - lengths[i]);
                    lengths[i]++;
                    kraft_sum += 1LL << (max_len - lengths[i]);
                    changed = true;
                }
            }
        }
    }
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_frequencies(int32_t *freq, int num_symbols, uint32_t seed)
{
    /* Generate Zipf-like distribution (common in text) */
    uint32_t x = seed;

    for (int i = 0; i < num_symbols; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;

        /* Most symbols rare, few common */
        if (i < 26) {
            /* Letters: high frequency */
            freq[i] = 1000 + (x % 5000);
        } else if (i < 52) {
            /* More letters: medium frequency */
            freq[i] = 100 + (x % 1000);
        } else if (i < 100) {
            /* Digits and punctuation: low frequency */
            freq[i] = 10 + (x % 100);
        } else {
            /* Others: very low or zero */
            freq[i] = (x % 10 < 3) ? (x % 50) : 0;
        }
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    /* Generate test frequencies */
    generate_frequencies(frequencies, HUFFMAN_SYMBOLS, 0x12345678);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };

    /* Start timing */
    BENCH_START();

    /* Build Huffman tree */
    int32_t root = huffman_build_tree(frequencies, HUFFMAN_SYMBOLS);

    /* Compute code lengths */
    compute_code_lengths(root, code_lengths, HUFFMAN_SYMBOLS);

    /* Limit code lengths */
    limit_code_lengths(code_lengths, HUFFMAN_SYMBOLS, HUFFMAN_MAX_LEN);

    /* End timing */
    BENCH_END();

    /* Prevent optimization */
    BENCH_VOLATILE(root);

    /* Compute checksum of code lengths */
    uint32_t csum = checksum_buffer(code_lengths, HUFFMAN_SYMBOLS);
    csum = checksum_update(csum, (uint32_t)root);

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
    huffman_tree,
    "Huffman tree construction",
    "401.bzip2",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    1
);

KERNEL_REGISTER(huffman_tree)
