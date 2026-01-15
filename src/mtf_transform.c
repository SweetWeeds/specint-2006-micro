/*
 * SPECInt2006-micro: mtf_transform kernel
 * Captures Move-To-Front transform from 401.bzip2
 *
 * Pattern: Symbol reordering for entropy reduction
 * Memory: Sequential access with list updates
 * Branch: Data-dependent list position
 */

#include "bench.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef MTF_BLOCK_SIZE
#define MTF_BLOCK_SIZE      1024
#endif

#ifndef MTF_ALPHABET_SIZE
#define MTF_ALPHABET_SIZE   256
#endif

#ifndef MTF_NUM_BLOCKS
#define MTF_NUM_BLOCKS      10
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

static uint8_t input_block[MTF_BLOCK_SIZE];
static uint8_t output_block[MTF_BLOCK_SIZE];
static uint8_t mtf_list[MTF_ALPHABET_SIZE];
static uint8_t inverse_list[MTF_ALPHABET_SIZE];

/* ============================================================================
 * Move-To-Front Transform
 * ============================================================================ */

/* Initialize MTF list to identity */
static void mtf_init(void)
{
    for (int i = 0; i < MTF_ALPHABET_SIZE; i++) {
        mtf_list[i] = (uint8_t)i;
        inverse_list[i] = (uint8_t)i;
    }
}

/* MTF encode: output the position of symbol in list, then move to front */
static void mtf_encode(const uint8_t *input, uint8_t *output, int len)
{
    mtf_init();

    for (int i = 0; i < len; i++) {
        uint8_t symbol = input[i];

        /* Find position of symbol in list */
        int pos = 0;
        while (pos < MTF_ALPHABET_SIZE && mtf_list[pos] != symbol) {
            pos++;
        }

        output[i] = (uint8_t)pos;

        /* Move symbol to front */
        if (pos > 0) {
            for (int j = pos; j > 0; j--) {
                mtf_list[j] = mtf_list[j - 1];
            }
            mtf_list[0] = symbol;
        }
    }
}

/* MTF decode: inverse transform */
static void mtf_decode(const uint8_t *input, uint8_t *output, int len)
{
    mtf_init();

    for (int i = 0; i < len; i++) {
        int pos = input[i];

        /* Get symbol at position */
        uint8_t symbol = mtf_list[pos];
        output[i] = symbol;

        /* Move symbol to front */
        if (pos > 0) {
            for (int j = pos; j > 0; j--) {
                mtf_list[j] = mtf_list[j - 1];
            }
            mtf_list[0] = symbol;
        }
    }
}

/* ============================================================================
 * Run-Length Encoding (RUNA/RUNB from bzip2)
 * ============================================================================ */

/* Count runs of zeros in MTF output */
static int count_zero_runs(const uint8_t *data, int len, int *run_counts)
{
    int num_runs = 0;
    int i = 0;

    while (i < len) {
        if (data[i] == 0) {
            /* Count consecutive zeros */
            int run_len = 0;
            while (i < len && data[i] == 0) {
                run_len++;
                i++;
            }
            if (num_runs < 256) {
                run_counts[num_runs++] = run_len;
            }
        } else {
            i++;
        }
    }

    return num_runs;
}

/* Encode run lengths using RUNA/RUNB (power of 2 encoding) */
static int encode_runs(int run_len, uint8_t *output)
{
    int out_len = 0;

    /* bzip2 uses bijective base-2 encoding */
    while (run_len > 0) {
        if (run_len & 1) {
            output[out_len++] = 0;  /* RUNA */
        } else {
            output[out_len++] = 1;  /* RUNB */
        }
        run_len = (run_len - 1) / 2;
    }

    return out_len;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_block(uint8_t *block, int size, uint32_t seed)
{
    uint32_t x = seed;

    /* Generate data with some repetition (like typical text) */
    for (int i = 0; i < size; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;

        /* Bias towards lower values (like BWT output) */
        int r = x % 100;
        if (r < 30) {
            block[i] = 0;  /* High frequency of zeros */
        } else if (r < 50) {
            block[i] = 1;
        } else if (r < 65) {
            block[i] = 2;
        } else if (r < 80) {
            block[i] = (x >> 8) % 10;
        } else {
            block[i] = (x >> 8) % MTF_ALPHABET_SIZE;
        }
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    memset(input_block, 0, sizeof(input_block));
    memset(output_block, 0, sizeof(output_block));
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();
    int total_zeros = 0;
    int total_runs = 0;

    /* Start timing */
    BENCH_START();

    for (int b = 0; b < MTF_NUM_BLOCKS; b++) {
        /* Generate input block */
        generate_block(input_block, MTF_BLOCK_SIZE, 0x12345678 + b * 1000);

        /* MTF encode */
        mtf_encode(input_block, output_block, MTF_BLOCK_SIZE);

        /* Count zeros and runs */
        int run_counts[256];
        int num_runs = count_zero_runs(output_block, MTF_BLOCK_SIZE, run_counts);
        total_runs += num_runs;

        /* Count total zeros */
        for (int i = 0; i < MTF_BLOCK_SIZE; i++) {
            if (output_block[i] == 0) total_zeros++;
        }

        /* Encode runs */
        uint8_t run_encoded[32];
        for (int r = 0; r < num_runs && r < 10; r++) {
            int enc_len = encode_runs(run_counts[r], run_encoded);
            for (int i = 0; i < enc_len; i++) {
                csum = checksum_update(csum, run_encoded[i]);
            }
        }

        /* MTF decode (for verification) */
        uint8_t decoded[MTF_BLOCK_SIZE];
        mtf_decode(output_block, decoded, MTF_BLOCK_SIZE);

        /* Verify roundtrip */
        for (int i = 0; i < MTF_BLOCK_SIZE; i++) {
            if (decoded[i] != input_block[i]) {
                result.status = BENCH_ERR_CHECKSUM;
            }
        }

        /* Update checksum with output statistics */
        csum = checksum_update(csum, (uint32_t)num_runs);
        csum = checksum_update(csum, checksum_buffer(output_block, MTF_BLOCK_SIZE));
    }

    /* End timing */
    BENCH_END();

    csum = checksum_update(csum, (uint32_t)total_zeros);
    csum = checksum_update(csum, (uint32_t)total_runs);

    BENCH_VOLATILE(total_zeros);

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
    mtf_transform,
    "Move-To-Front transform",
    "401.bzip2",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    MTF_NUM_BLOCKS
);

KERNEL_REGISTER(mtf_transform)
