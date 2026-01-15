/*
 * SPECInt2006-micro: bwt_sort kernel
 * Captures Burrows-Wheeler Transform from 401.bzip2
 *
 * Pattern: Suffix sorting with radix + quicksort fallback
 * Memory: Large array with stride access
 * Branch: Mix of predictable (radix) and data-dependent (quicksort)
 */

#include "bench.h"


/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef BWT_BLOCK_SIZE
#define BWT_BLOCK_SIZE      512     /* Block size in bytes */
#endif

#ifndef BWT_ALPHABET_SIZE
#define BWT_ALPHABET_SIZE   256     /* Byte alphabet */
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Static storage */
static uint8_t block[BWT_BLOCK_SIZE + 4];     /* Input block + sentinel */
static uint32_t ptr[BWT_BLOCK_SIZE];          /* Suffix pointers */
static uint32_t ftab[BWT_ALPHABET_SIZE + 1];  /* Frequency table */
static uint8_t output[BWT_BLOCK_SIZE];        /* BWT output */

/* ============================================================================
 * Sorting Functions (simplified from bzip2)
 * ============================================================================ */

/* Compare suffixes at positions p1 and p2 */
static int suffix_compare(const uint8_t *block, uint32_t n, uint32_t p1, uint32_t p2)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx1 = (p1 + i) % n;
        uint32_t idx2 = (p2 + i) % n;

        if (block[idx1] < block[idx2]) return -1;
        if (block[idx1] > block[idx2]) return 1;
    }
    return 0;
}

/* 3-way quicksort partition */
static void qsort3_partition(uint32_t *ptr, const uint8_t *block, uint32_t n,
                            int lo, int hi, int depth,
                            int *lt_out, int *gt_out)
{
    if (hi <= lo) {
        *lt_out = lo;
        *gt_out = hi;
        return;
    }

    /* Pivot is middle element */
    int mid = lo + (hi - lo) / 2;
    uint32_t pivot_pos = ptr[mid];
    uint8_t pivot = block[(pivot_pos + depth) % n];

    int lt = lo;
    int gt = hi;
    int i = lo;

    while (i <= gt) {
        uint8_t c = block[(ptr[i] + depth) % n];
        if (c < pivot) {
            /* Swap ptr[lt] and ptr[i] */
            uint32_t tmp = ptr[lt];
            ptr[lt] = ptr[i];
            ptr[i] = tmp;
            lt++;
            i++;
        } else if (c > pivot) {
            /* Swap ptr[i] and ptr[gt] */
            uint32_t tmp = ptr[i];
            ptr[i] = ptr[gt];
            ptr[gt] = tmp;
            gt--;
        } else {
            i++;
        }
    }

    *lt_out = lt;
    *gt_out = gt;
}

/* 3-way quicksort for suffixes */
static void qsort3_suffixes(uint32_t *ptr, const uint8_t *block, uint32_t n,
                           int lo, int hi, int depth)
{
    /* Limit recursion depth */
    if (hi <= lo || depth > 32) {
        return;
    }

    /* Use insertion sort for small subarrays */
    if (hi - lo < 10) {
        for (int i = lo + 1; i <= hi; i++) {
            uint32_t v = ptr[i];
            int j = i;
            while (j > lo && suffix_compare(block, n, ptr[j-1], v) > 0) {
                ptr[j] = ptr[j-1];
                j--;
            }
            ptr[j] = v;
        }
        return;
    }

    int lt, gt;
    qsort3_partition(ptr, block, n, lo, hi, depth, &lt, &gt);

    /* Recurse on three partitions */
    qsort3_suffixes(ptr, block, n, lo, lt - 1, depth);
    qsort3_suffixes(ptr, block, n, lt, gt, depth + 1);
    qsort3_suffixes(ptr, block, n, gt + 1, hi, depth);
}

/* Radix bucket sort (first character) */
static void radix_bucket(const uint8_t *block, uint32_t n, uint32_t *ptr, uint32_t *ftab)
{
    /* Count character frequencies */
    memset(ftab, 0, (BWT_ALPHABET_SIZE + 1) * sizeof(uint32_t));

    for (uint32_t i = 0; i < n; i++) {
        ftab[block[i]]++;
    }

    /* Compute cumulative frequencies */
    uint32_t cum = 0;
    for (int i = 0; i < BWT_ALPHABET_SIZE; i++) {
        uint32_t tmp = ftab[i];
        ftab[i] = cum;
        cum += tmp;
    }
    ftab[BWT_ALPHABET_SIZE] = n;

    /* Distribute suffixes into buckets */
    for (uint32_t i = 0; i < n; i++) {
        ptr[ftab[block[i]]++] = i;
    }

    /* Restore ftab for bucket boundaries */
    cum = 0;
    for (int i = 0; i < BWT_ALPHABET_SIZE; i++) {
        uint32_t tmp = ftab[i] - cum;
        ftab[i] = cum;
        cum = ftab[i] + tmp;
    }
}

/* Main BWT function */
static uint32_t bwt_transform(const uint8_t *input, uint32_t n,
                             uint32_t *ptr, uint32_t *ftab, uint8_t *output)
{
    /* Radix sort on first character */
    radix_bucket(input, n, ptr, ftab);

    /* Quicksort each bucket */
    for (int c = 0; c < BWT_ALPHABET_SIZE; c++) {
        uint32_t lo = ftab[c];
        uint32_t hi = (c < 255) ? ftab[c + 1] - 1 : n - 1;

        if (hi > lo) {
            qsort3_suffixes(ptr, input, n, lo, hi, 1);
        }
    }

    /* Generate BWT output and find original position */
    uint32_t orig_pos = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (ptr[i] == 0) {
            output[i] = input[n - 1];
            orig_pos = i;
        } else {
            output[i] = input[ptr[i] - 1];
        }
    }

    return orig_pos;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_block(uint8_t *block, uint32_t size, uint32_t seed)
{
    /* Generate text-like data with some repetition */
    uint32_t x = seed;

    for (uint32_t i = 0; i < size; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;

        /* Bias towards lowercase letters (text-like) */
        uint32_t r = x % 100;
        if (r < 60) {
            block[i] = 'a' + (x % 26);
        } else if (r < 80) {
            block[i] = ' ';  /* Space */
        } else if (r < 90) {
            block[i] = 'A' + (x % 26);
        } else {
            block[i] = '0' + (x % 10);
        }
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    /* Generate test block */
    generate_block(block, BWT_BLOCK_SIZE, 0xCAFEBABE);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };

    /* Start timing */
    BENCH_START();

    /* Perform BWT */
    uint32_t orig_pos = bwt_transform(block, BWT_BLOCK_SIZE, ptr, ftab, output);

    /* End timing */
    BENCH_END();

    /* Prevent optimization */
    BENCH_VOLATILE(orig_pos);

    /* Compute checksum of output */
    uint32_t csum = checksum_buffer(output, BWT_BLOCK_SIZE);
    csum = checksum_update(csum, orig_pos);

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
    bwt_sort,
    "Burrows-Wheeler Transform",
    "401.bzip2",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,  /* Checksum varies */
    1
);

KERNEL_REGISTER(bwt_sort)
