/*
 * SPECInt2006-micro: intra_predict kernel
 * Captures H.264 intra prediction from 464.h264ref
 *
 * Pattern: Directional pixel prediction from neighbors
 * Memory: 2D block access, reference pixel fetching
 * Branch: Mode-dependent prediction paths
 */

#include "bench.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef INTRA_BLOCK_SIZE
#define INTRA_BLOCK_SIZE    16
#endif

#ifndef INTRA_NUM_MODES_4x4
#define INTRA_NUM_MODES_4x4 9
#endif

#ifndef INTRA_NUM_MODES_16x16
#define INTRA_NUM_MODES_16x16 4
#endif

#ifndef INTRA_NUM_BLOCKS
#define INTRA_NUM_BLOCKS    20
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Prediction modes for 4x4 blocks */
#define INTRA_4x4_VERTICAL      0
#define INTRA_4x4_HORIZONTAL    1
#define INTRA_4x4_DC            2
#define INTRA_4x4_DIAG_DOWN_LEFT  3
#define INTRA_4x4_DIAG_DOWN_RIGHT 4
#define INTRA_4x4_VERT_RIGHT    5
#define INTRA_4x4_HORIZ_DOWN    6
#define INTRA_4x4_VERT_LEFT     7
#define INTRA_4x4_HORIZ_UP      8

/* Prediction modes for 16x16 blocks */
#define INTRA_16x16_VERTICAL    0
#define INTRA_16x16_HORIZONTAL  1
#define INTRA_16x16_DC          2
#define INTRA_16x16_PLANE       3

/* Reference pixels (above and left neighbors) */
typedef struct {
    uint8_t above[INTRA_BLOCK_SIZE + 1];  /* Top row + top-left */
    uint8_t left[INTRA_BLOCK_SIZE + 1];   /* Left column + top-left */
    uint8_t above_right[INTRA_BLOCK_SIZE]; /* Above-right for 4x4 */
} intra_ref_t;

/* Block for prediction */
typedef struct {
    uint8_t pixels[INTRA_BLOCK_SIZE][INTRA_BLOCK_SIZE];
} intra_block_t;

static intra_ref_t ref;
static intra_block_t pred_block;
static intra_block_t orig_block;

/* ============================================================================
 * 4x4 Intra Prediction Modes
 * ============================================================================ */

/* Vertical prediction: copy top row */
static void intra_4x4_vertical(uint8_t pred[4][4], const uint8_t *above)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            pred[y][x] = above[x + 1];
        }
    }
}

/* Horizontal prediction: copy left column */
static void intra_4x4_horizontal(uint8_t pred[4][4], const uint8_t *left)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            pred[y][x] = left[y + 1];
        }
    }
}

/* DC prediction: average of neighbors */
static void intra_4x4_dc(uint8_t pred[4][4], const uint8_t *above, const uint8_t *left)
{
    int sum = 0;
    for (int i = 1; i <= 4; i++) {
        sum += above[i] + left[i];
    }
    uint8_t dc = (sum + 4) >> 3;

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            pred[y][x] = dc;
        }
    }
}

/* Diagonal Down-Left */
static void intra_4x4_diag_down_left(uint8_t pred[4][4], const uint8_t *above,
                                      const uint8_t *above_right)
{
    uint8_t ref_pixels[9];
    for (int i = 0; i < 4; i++) ref_pixels[i] = above[i + 1];
    for (int i = 0; i < 4; i++) ref_pixels[4 + i] = above_right[i];
    ref_pixels[8] = above_right[3];

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = x + y;
            if (idx < 6) {
                pred[y][x] = (ref_pixels[idx] + 2 * ref_pixels[idx + 1] +
                             ref_pixels[idx + 2] + 2) >> 2;
            } else {
                pred[y][x] = ref_pixels[7];
            }
        }
    }
}

/* Diagonal Down-Right */
static void intra_4x4_diag_down_right(uint8_t pred[4][4], const uint8_t *above,
                                       const uint8_t *left)
{
    /* Build diagonal reference array */
    uint8_t ref_pixels[9];
    for (int i = 0; i < 4; i++) ref_pixels[i] = left[4 - i];
    ref_pixels[4] = above[0];  /* top-left corner */
    for (int i = 0; i < 4; i++) ref_pixels[5 + i] = above[i + 1];

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = 4 + x - y;
            pred[y][x] = (ref_pixels[idx - 1] + 2 * ref_pixels[idx] +
                         ref_pixels[idx + 1] + 2) >> 2;
        }
    }
}

/* Vertical-Right */
static void intra_4x4_vert_right(uint8_t pred[4][4], const uint8_t *above,
                                  const uint8_t *left)
{
    uint8_t ref_pixels[10] = {0};  /* Initialize to avoid uninitialized access */
    for (int i = 0; i < 3; i++) ref_pixels[i] = left[3 - i];
    ref_pixels[3] = above[0];
    for (int i = 0; i < 5; i++) ref_pixels[4 + i] = above[i + 1];

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int zy = 2 * x - y;
            int idx = zy + 3;
            if (idx >= 0 && idx < 9) {
                if (y == 0 || y == 2) {
                    pred[y][x] = (ref_pixels[idx] + ref_pixels[idx + 1] + 1) >> 1;
                } else {
                    pred[y][x] = (ref_pixels[idx - 1] + 2 * ref_pixels[idx] +
                                 ref_pixels[idx + 1] + 2) >> 2;
                }
            } else {
                pred[y][x] = ref_pixels[3];
            }
        }
    }
}

/* Horizontal-Down */
static void intra_4x4_horiz_down(uint8_t pred[4][4], const uint8_t *above,
                                  const uint8_t *left)
{
    uint8_t ref_pixels[10] = {0};  /* Initialize to avoid uninitialized access */
    for (int i = 0; i < 4; i++) ref_pixels[i] = left[4 - i];
    ref_pixels[4] = above[0];
    for (int i = 0; i < 4; i++) ref_pixels[5 + i] = above[i + 1];

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int zx = 2 * y - x;
            int idx = zx + 4;
            if (idx >= 0 && idx < 9) {
                if (x == 0 || x == 2) {
                    pred[y][x] = (ref_pixels[idx] + ref_pixels[idx + 1] + 1) >> 1;
                } else {
                    pred[y][x] = (ref_pixels[idx - 1] + 2 * ref_pixels[idx] +
                                 ref_pixels[idx + 1] + 2) >> 2;
                }
            } else {
                pred[y][x] = ref_pixels[4];
            }
        }
    }
}

/* Vertical-Left */
static void intra_4x4_vert_left(uint8_t pred[4][4], const uint8_t *above,
                                 const uint8_t *above_right)
{
    uint8_t ref_pixels[9];
    for (int i = 0; i < 4; i++) ref_pixels[i] = above[i + 1];
    for (int i = 0; i < 4; i++) ref_pixels[4 + i] = above_right[i];
    ref_pixels[8] = above_right[3];

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int idx = x + (y >> 1);
            if (y & 1) {
                pred[y][x] = (ref_pixels[idx] + 2 * ref_pixels[idx + 1] +
                             ref_pixels[idx + 2] + 2) >> 2;
            } else {
                pred[y][x] = (ref_pixels[idx] + ref_pixels[idx + 1] + 1) >> 1;
            }
        }
    }
}

/* Horizontal-Up */
static void intra_4x4_horiz_up(uint8_t pred[4][4], const uint8_t *left)
{
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int zy = y + (x >> 1);
            if (zy < 3) {
                if (x & 1) {
                    pred[y][x] = (left[zy + 1] + 2 * left[zy + 2] + left[zy + 3] + 2) >> 2;
                } else {
                    pred[y][x] = (left[zy + 1] + left[zy + 2] + 1) >> 1;
                }
            } else if (zy == 3) {
                if (x & 1) {
                    pred[y][x] = left[4];
                } else {
                    pred[y][x] = (left[4] + left[4] + 1) >> 1;
                }
            } else {
                pred[y][x] = left[4];
            }
        }
    }
}

/* ============================================================================
 * 16x16 Intra Prediction Modes
 * ============================================================================ */

/* Vertical prediction for 16x16 */
static void intra_16x16_vertical(intra_block_t *pred, const intra_ref_t *r)
{
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            pred->pixels[y][x] = r->above[x + 1];
        }
    }
}

/* Horizontal prediction for 16x16 */
static void intra_16x16_horizontal(intra_block_t *pred, const intra_ref_t *r)
{
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            pred->pixels[y][x] = r->left[y + 1];
        }
    }
}

/* DC prediction for 16x16 */
static void intra_16x16_dc(intra_block_t *pred, const intra_ref_t *r)
{
    int sum = 0;
    for (int i = 1; i <= 16; i++) {
        sum += r->above[i] + r->left[i];
    }
    uint8_t dc = (sum + 16) >> 5;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            pred->pixels[y][x] = dc;
        }
    }
}

/* Plane prediction for 16x16 */
static void intra_16x16_plane(intra_block_t *pred, const intra_ref_t *r)
{
    /* Calculate H and V parameters */
    int H = 0, V = 0;

    for (int i = 1; i <= 8; i++) {
        H += i * (r->above[8 + i] - r->above[8 - i]);
        V += i * (r->left[8 + i] - r->left[8 - i]);
    }

    int a = 16 * (r->above[16] + r->left[16]);
    int b = (5 * H + 32) >> 6;
    int c = (5 * V + 32) >> 6;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int val = (a + b * (x - 7) + c * (y - 7) + 16) >> 5;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            pred->pixels[y][x] = (uint8_t)val;
        }
    }
}

/* ============================================================================
 * SAD (Sum of Absolute Differences) Calculation
 * ============================================================================ */

static int calc_sad_4x4(const uint8_t pred[4][4], const uint8_t orig[4][4])
{
    int sad = 0;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int diff = pred[y][x] - orig[y][x];
            sad += (diff < 0) ? -diff : diff;
        }
    }
    return sad;
}

static int calc_sad_16x16(const intra_block_t *pred, const intra_block_t *orig)
{
    int sad = 0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int diff = pred->pixels[y][x] - orig->pixels[y][x];
            sad += (diff < 0) ? -diff : diff;
        }
    }
    return sad;
}

/* ============================================================================
 * Mode Selection
 * ============================================================================ */

/* Find best 4x4 mode for a sub-block */
static int find_best_4x4_mode(const uint8_t orig[4][4], const uint8_t *above,
                               const uint8_t *left, const uint8_t *above_right)
{
    uint8_t pred[4][4];
    int best_mode = 0;
    int best_sad = 0x7FFFFFFF;

    /* Try all 9 modes */
    for (int mode = 0; mode < 9; mode++) {
        switch (mode) {
        case INTRA_4x4_VERTICAL:
            intra_4x4_vertical(pred, above);
            break;
        case INTRA_4x4_HORIZONTAL:
            intra_4x4_horizontal(pred, left);
            break;
        case INTRA_4x4_DC:
            intra_4x4_dc(pred, above, left);
            break;
        case INTRA_4x4_DIAG_DOWN_LEFT:
            intra_4x4_diag_down_left(pred, above, above_right);
            break;
        case INTRA_4x4_DIAG_DOWN_RIGHT:
            intra_4x4_diag_down_right(pred, above, left);
            break;
        case INTRA_4x4_VERT_RIGHT:
            intra_4x4_vert_right(pred, above, left);
            break;
        case INTRA_4x4_HORIZ_DOWN:
            intra_4x4_horiz_down(pred, above, left);
            break;
        case INTRA_4x4_VERT_LEFT:
            intra_4x4_vert_left(pred, above, above_right);
            break;
        case INTRA_4x4_HORIZ_UP:
            intra_4x4_horiz_up(pred, left);
            break;
        }

        int sad = calc_sad_4x4(pred, orig);
        if (sad < best_sad) {
            best_sad = sad;
            best_mode = mode;
        }
    }

    return best_mode;
}

/* Find best 16x16 mode */
static int find_best_16x16_mode(const intra_block_t *orig, const intra_ref_t *r)
{
    int best_mode = 0;
    int best_sad = 0x7FFFFFFF;

    for (int mode = 0; mode < 4; mode++) {
        switch (mode) {
        case INTRA_16x16_VERTICAL:
            intra_16x16_vertical(&pred_block, r);
            break;
        case INTRA_16x16_HORIZONTAL:
            intra_16x16_horizontal(&pred_block, r);
            break;
        case INTRA_16x16_DC:
            intra_16x16_dc(&pred_block, r);
            break;
        case INTRA_16x16_PLANE:
            intra_16x16_plane(&pred_block, r);
            break;
        }

        int sad = calc_sad_16x16(&pred_block, orig);
        if (sad < best_sad) {
            best_sad = sad;
            best_mode = mode;
        }
    }

    return best_mode;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_test_block(intra_block_t *block, intra_ref_t *r, uint32_t seed)
{
    uint32_t x = seed;

    /* Generate original block with some structure */
    for (int y = 0; y < 16; y++) {
        for (int i = 0; i < 16; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            /* Add gradient + noise */
            int base = (y * 8) + (i * 4);
            int noise = (x % 64) - 32;
            int val = base + noise;
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            block->pixels[y][i] = (uint8_t)val;
        }
    }

    /* Generate reference pixels (from neighbors) */
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->above[0] = (uint8_t)(x % 256);  /* top-left */
    r->left[0] = r->above[0];

    for (int i = 1; i <= 16; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        r->above[i] = (uint8_t)(64 + (x % 128));
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        r->left[i] = (uint8_t)(64 + (x % 128));
    }

    for (int i = 0; i < 16; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        r->above_right[i] = (uint8_t)(64 + (x % 128));
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    memset(&ref, 0, sizeof(ref));
    memset(&pred_block, 0, sizeof(pred_block));
    memset(&orig_block, 0, sizeof(orig_block));
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();
    int total_sad_4x4 = 0;
    int total_sad_16x16 = 0;
    int mode_counts[13] = {0};  /* 9 for 4x4, 4 for 16x16 */

    /* Start timing */
    BENCH_START();

    for (int b = 0; b < INTRA_NUM_BLOCKS; b++) {
        /* Generate test block */
        generate_test_block(&orig_block, &ref, 0x12345678 + b * 1000);

        /* 16x16 mode selection */
        int best_16x16 = find_best_16x16_mode(&orig_block, &ref);
        mode_counts[9 + best_16x16]++;

        /* Apply best 16x16 prediction */
        switch (best_16x16) {
        case INTRA_16x16_VERTICAL:
            intra_16x16_vertical(&pred_block, &ref);
            break;
        case INTRA_16x16_HORIZONTAL:
            intra_16x16_horizontal(&pred_block, &ref);
            break;
        case INTRA_16x16_DC:
            intra_16x16_dc(&pred_block, &ref);
            break;
        case INTRA_16x16_PLANE:
            intra_16x16_plane(&pred_block, &ref);
            break;
        }
        total_sad_16x16 += calc_sad_16x16(&pred_block, &orig_block);

        /* 4x4 mode selection for each sub-block */
        for (int by = 0; by < 4; by++) {
            for (int bx = 0; bx < 4; bx++) {
                /* Extract 4x4 sub-block */
                uint8_t sub_orig[4][4];
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        sub_orig[y][x] = orig_block.pixels[by * 4 + y][bx * 4 + x];
                    }
                }

                /* Build local reference */
                uint8_t local_above[5], local_left[5], local_above_right[4];

                if (by == 0) {
                    for (int i = 0; i < 5; i++) {
                        local_above[i] = ref.above[bx * 4 + i];
                    }
                } else {
                    local_above[0] = (bx == 0) ? ref.left[by * 4] :
                                     orig_block.pixels[by * 4 - 1][bx * 4 - 1];
                    for (int i = 0; i < 4; i++) {
                        local_above[i + 1] = orig_block.pixels[by * 4 - 1][bx * 4 + i];
                    }
                }

                if (bx == 0) {
                    for (int i = 0; i < 5; i++) {
                        local_left[i] = ref.left[by * 4 + i];
                    }
                } else {
                    local_left[0] = (by == 0) ? ref.above[bx * 4] :
                                    orig_block.pixels[by * 4 - 1][bx * 4 - 1];
                    for (int i = 0; i < 4; i++) {
                        local_left[i + 1] = orig_block.pixels[by * 4 + i][bx * 4 - 1];
                    }
                }

                /* Above-right pixels */
                if (by == 0 && bx < 3) {
                    for (int i = 0; i < 4; i++) {
                        local_above_right[i] = ref.above[(bx + 1) * 4 + i + 1];
                    }
                } else if (by > 0 && bx < 3) {
                    for (int i = 0; i < 4; i++) {
                        local_above_right[i] = orig_block.pixels[by * 4 - 1][(bx + 1) * 4 + i];
                    }
                } else {
                    for (int i = 0; i < 4; i++) {
                        local_above_right[i] = local_above[4];
                    }
                }

                int best_4x4 = find_best_4x4_mode(sub_orig, local_above,
                                                  local_left, local_above_right);
                mode_counts[best_4x4]++;

                /* Calculate SAD for best 4x4 mode */
                uint8_t pred_4x4[4][4];
                switch (best_4x4) {
                case INTRA_4x4_VERTICAL:
                    intra_4x4_vertical(pred_4x4, local_above);
                    break;
                case INTRA_4x4_HORIZONTAL:
                    intra_4x4_horizontal(pred_4x4, local_left);
                    break;
                case INTRA_4x4_DC:
                    intra_4x4_dc(pred_4x4, local_above, local_left);
                    break;
                case INTRA_4x4_DIAG_DOWN_LEFT:
                    intra_4x4_diag_down_left(pred_4x4, local_above, local_above_right);
                    break;
                case INTRA_4x4_DIAG_DOWN_RIGHT:
                    intra_4x4_diag_down_right(pred_4x4, local_above, local_left);
                    break;
                case INTRA_4x4_VERT_RIGHT:
                    intra_4x4_vert_right(pred_4x4, local_above, local_left);
                    break;
                case INTRA_4x4_HORIZ_DOWN:
                    intra_4x4_horiz_down(pred_4x4, local_above, local_left);
                    break;
                case INTRA_4x4_VERT_LEFT:
                    intra_4x4_vert_left(pred_4x4, local_above, local_above_right);
                    break;
                case INTRA_4x4_HORIZ_UP:
                    intra_4x4_horiz_up(pred_4x4, local_left);
                    break;
                }
                total_sad_4x4 += calc_sad_4x4(pred_4x4, sub_orig);

                csum = checksum_update(csum, (uint32_t)best_4x4);
            }
        }

        csum = checksum_update(csum, (uint32_t)best_16x16);
    }

    /* End timing */
    BENCH_END();

    /* Update checksum with statistics */
    csum = checksum_update(csum, (uint32_t)total_sad_4x4);
    csum = checksum_update(csum, (uint32_t)total_sad_16x16);
    for (int i = 0; i < 13; i++) {
        csum = checksum_update(csum, (uint32_t)mode_counts[i]);
    }

    BENCH_VOLATILE(total_sad_4x4);

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
    intra_predict,
    "H.264 intra prediction",
    "464.h264ref",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    INTRA_NUM_BLOCKS
);

KERNEL_REGISTER(intra_predict)
