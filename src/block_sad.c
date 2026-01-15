/*
 * SPECInt2006-micro: block_sad kernel
 * Captures motion estimation SAD from 464.h264ref
 *
 * Pattern: Sum of Absolute Differences for block matching
 * Memory: Regular 2D block access with search window
 * Branch: Predictable loops
 */

#include "bench.h"


/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef FRAME_WIDTH
#define FRAME_WIDTH         64
#endif

#ifndef FRAME_HEIGHT
#define FRAME_HEIGHT        64
#endif

#ifndef BLOCK_SIZE
#define BLOCK_SIZE          16      /* Macroblock size */
#endif

#ifndef SEARCH_RANGE
#define SEARCH_RANGE        8       /* +/- pixels */
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

static uint8_t current_frame[FRAME_HEIGHT][FRAME_WIDTH];
static uint8_t reference_frame[FRAME_HEIGHT][FRAME_WIDTH];
static int16_t mv_x[FRAME_HEIGHT / BLOCK_SIZE][FRAME_WIDTH / BLOCK_SIZE];
static int16_t mv_y[FRAME_HEIGHT / BLOCK_SIZE][FRAME_WIDTH / BLOCK_SIZE];

/* ============================================================================
 * SAD Computation Functions
 * ============================================================================ */

/* SAD for 16x16 block */
static uint32_t sad_16x16(const uint8_t *cur, int cur_stride,
                         const uint8_t *ref, int ref_stride)
{
    uint32_t sad = 0;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int diff = (int)cur[x] - (int)ref[x];
            sad += (diff < 0) ? -diff : diff;
        }
        cur += cur_stride;
        ref += ref_stride;
    }

    return sad;
}

/* SAD for 8x8 block */
__attribute__((unused))
static uint32_t sad_8x8(const uint8_t *cur, int cur_stride,
                       const uint8_t *ref, int ref_stride)
{
    uint32_t sad = 0;

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int diff = (int)cur[x] - (int)ref[x];
            sad += (diff < 0) ? -diff : diff;
        }
        cur += cur_stride;
        ref += ref_stride;
    }

    return sad;
}

/* SAD for 4x4 block */
__attribute__((unused))
static uint32_t sad_4x4(const uint8_t *cur, int cur_stride,
                       const uint8_t *ref, int ref_stride)
{
    uint32_t sad = 0;

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int diff = (int)cur[x] - (int)ref[x];
            sad += (diff < 0) ? -diff : diff;
        }
        cur += cur_stride;
        ref += ref_stride;
    }

    return sad;
}

/* ============================================================================
 * Motion Estimation
 * ============================================================================ */

/* Full search motion estimation */
static uint32_t full_search(int block_x, int block_y,
                           int *best_mx, int *best_my)
{
    const uint8_t *cur = &current_frame[block_y][block_x];
    uint32_t best_sad = UINT32_MAX;
    *best_mx = 0;
    *best_my = 0;

    /* Search window */
    int min_y = (block_y >= SEARCH_RANGE) ? -SEARCH_RANGE : -block_y;
    int max_y = (block_y + BLOCK_SIZE + SEARCH_RANGE <= FRAME_HEIGHT) ?
                SEARCH_RANGE : FRAME_HEIGHT - block_y - BLOCK_SIZE;
    int min_x = (block_x >= SEARCH_RANGE) ? -SEARCH_RANGE : -block_x;
    int max_x = (block_x + BLOCK_SIZE + SEARCH_RANGE <= FRAME_WIDTH) ?
                SEARCH_RANGE : FRAME_WIDTH - block_x - BLOCK_SIZE;

    for (int my = min_y; my <= max_y; my++) {
        for (int mx = min_x; mx <= max_x; mx++) {
            const uint8_t *ref = &reference_frame[block_y + my][block_x + mx];
            uint32_t sad = sad_16x16(cur, FRAME_WIDTH, ref, FRAME_WIDTH);

            if (sad < best_sad) {
                best_sad = sad;
                *best_mx = mx;
                *best_my = my;
            }
        }
    }

    return best_sad;
}

/* Diamond search pattern */
static const int diamond_pattern[9][2] = {
    {0, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    {1, -1}, {1, 1}, {-1, 1}, {-1, -1}
};

/* Diamond search motion estimation */
static uint32_t diamond_search(int block_x, int block_y,
                              int *best_mx, int *best_my)
{
    const uint8_t *cur = &current_frame[block_y][block_x];
    int cx = 0, cy = 0;  /* Center of search */
    uint32_t best_sad = UINT32_MAX;

    /* Large diamond search */
    for (int iter = 0; iter < 16; iter++) {
        int new_cx = cx, new_cy = cy;
        uint32_t new_best = best_sad;

        for (int i = 0; i < 9; i++) {
            int mx = cx + diamond_pattern[i][0];
            int my = cy + diamond_pattern[i][1];

            /* Check bounds */
            if (block_x + mx < 0 || block_x + mx + BLOCK_SIZE > FRAME_WIDTH ||
                block_y + my < 0 || block_y + my + BLOCK_SIZE > FRAME_HEIGHT) {
                continue;
            }

            const uint8_t *ref = &reference_frame[block_y + my][block_x + mx];
            uint32_t sad = sad_16x16(cur, FRAME_WIDTH, ref, FRAME_WIDTH);

            if (sad < new_best) {
                new_best = sad;
                new_cx = mx;
                new_cy = my;
            }
        }

        if (new_cx == cx && new_cy == cy) {
            break;  /* Converged */
        }

        cx = new_cx;
        cy = new_cy;
        best_sad = new_best;
    }

    *best_mx = cx;
    *best_my = cy;
    return best_sad;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_frames(uint32_t seed)
{
    uint32_t x = seed;

    /* Generate reference frame with patterns */
    for (int y = 0; y < FRAME_HEIGHT; y++) {
        for (int i = 0; i < FRAME_WIDTH; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            /* Create some spatial correlation */
            reference_frame[y][i] = (uint8_t)(128 + (y / 4) * 3 + (i / 4) * 2 + (x % 30) - 15);
        }
    }

    /* Generate current frame as shifted version with noise */
    int global_mv_x = 2;  /* Global motion */
    int global_mv_y = 1;

    for (int y = 0; y < FRAME_HEIGHT; y++) {
        for (int i = 0; i < FRAME_WIDTH; i++) {
            int ref_y = y + global_mv_y;
            int ref_x = i + global_mv_x;

            if (ref_y >= 0 && ref_y < FRAME_HEIGHT &&
                ref_x >= 0 && ref_x < FRAME_WIDTH) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                int noise = (x % 10) - 5;
                current_frame[y][i] = (uint8_t)CLAMP(
                    (int)reference_frame[ref_y][ref_x] + noise, 0, 255);
            } else {
                current_frame[y][i] = 128;
            }
        }
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    generate_frames(0x12345678);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t total_sad = 0;
    uint32_t csum = checksum_init();

    /* Start timing */
    BENCH_START();

    /* Process each macroblock */
    int num_blocks_y = FRAME_HEIGHT / BLOCK_SIZE;
    int num_blocks_x = FRAME_WIDTH / BLOCK_SIZE;

    for (int by = 0; by < num_blocks_y; by++) {
        for (int bx = 0; bx < num_blocks_x; bx++) {
            int block_y = by * BLOCK_SIZE;
            int block_x = bx * BLOCK_SIZE;

            int mx, my;

            /* Use diamond search for speed */
            uint32_t sad = diamond_search(block_x, block_y, &mx, &my);

            /* Refine with full search in small window */
            int full_mx, full_my;
            uint32_t full_sad = full_search(block_x, block_y, &full_mx, &full_my);

            if (full_sad < sad) {
                mx = full_mx;
                my = full_my;
                sad = full_sad;
            }

            mv_x[by][bx] = mx;
            mv_y[by][bx] = my;
            total_sad += sad;

            csum = checksum_update(csum, sad);
            csum = checksum_update(csum, (uint32_t)((mx << 16) | (my & 0xFFFF)));
        }
    }

    /* End timing */
    BENCH_END();

    csum = checksum_update(csum, total_sad);

    /* Prevent optimization */
    BENCH_VOLATILE(total_sad);

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
    block_sad,
    "Block SAD motion estimation",
    "464.h264ref",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    (FRAME_HEIGHT / BLOCK_SIZE) * (FRAME_WIDTH / BLOCK_SIZE)
);

KERNEL_REGISTER(block_sad)
