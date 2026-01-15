/*
 * SPECInt2006-micro: influence_field kernel
 * Captures influence/territory estimation from 445.gobmk
 *
 * Pattern: Board influence propagation (like Bouzy's algorithm)
 * Memory: 2D grid updates, distance transforms
 * Branch: Territory classification
 */

#include "bench.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef INFLUENCE_BOARD_SIZE
#define INFLUENCE_BOARD_SIZE    19
#endif

#ifndef INFLUENCE_DILATION
#define INFLUENCE_DILATION      6
#endif

#ifndef INFLUENCE_EROSION
#define INFLUENCE_EROSION       5
#endif

#ifndef INFLUENCE_NUM_EVALS
#define INFLUENCE_NUM_EVALS     10
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

#define EMPTY   0
#define BLACK   1
#define WHITE   2

typedef struct {
    int8_t stones[INFLUENCE_BOARD_SIZE][INFLUENCE_BOARD_SIZE];
    int16_t influence[INFLUENCE_BOARD_SIZE][INFLUENCE_BOARD_SIZE];
    int16_t territory[INFLUENCE_BOARD_SIZE][INFLUENCE_BOARD_SIZE];
} influence_board_t;

static influence_board_t board;

/* Direction offsets */
static const int dx[4] = {0, 1, 0, -1};
static const int dy[4] = {1, 0, -1, 0};

/* ============================================================================
 * Influence Field Computation (Bouzy's algorithm)
 * ============================================================================ */

/* Initialize influence from stone positions */
static void init_influence(influence_board_t *b)
{
    for (int y = 0; y < INFLUENCE_BOARD_SIZE; y++) {
        for (int x = 0; x < INFLUENCE_BOARD_SIZE; x++) {
            if (b->stones[y][x] == BLACK) {
                b->influence[y][x] = 64;  /* Positive for black */
            } else if (b->stones[y][x] == WHITE) {
                b->influence[y][x] = -64;  /* Negative for white */
            } else {
                b->influence[y][x] = 0;
            }
        }
    }
}

/* Dilation step: spread influence to neighbors */
static void dilate_influence(influence_board_t *b)
{
    int16_t temp[INFLUENCE_BOARD_SIZE][INFLUENCE_BOARD_SIZE];

    for (int y = 0; y < INFLUENCE_BOARD_SIZE; y++) {
        for (int x = 0; x < INFLUENCE_BOARD_SIZE; x++) {
            int16_t val = b->influence[y][x];

            /* Add influence from self and neighbors */
            int16_t sum = val;
            int count = 1;

            for (int d = 0; d < 4; d++) {
                int ny = y + dy[d];
                int nx = x + dx[d];
                if (ny >= 0 && ny < INFLUENCE_BOARD_SIZE &&
                    nx >= 0 && nx < INFLUENCE_BOARD_SIZE) {
                    /* Only spread if same sign or zero */
                    int16_t nval = b->influence[ny][nx];
                    if ((val >= 0 && nval >= 0) || (val <= 0 && nval <= 0)) {
                        sum += nval / 2;
                        count++;
                    }
                }
            }

            /* Clamp to prevent overflow */
            if (sum > 127) sum = 127;
            if (sum < -127) sum = -127;

            temp[y][x] = sum;
        }
    }

    memcpy(b->influence, temp, sizeof(temp));
}

/* Erosion step: reduce isolated influence */
static void erode_influence(influence_board_t *b)
{
    int16_t temp[INFLUENCE_BOARD_SIZE][INFLUENCE_BOARD_SIZE];

    for (int y = 0; y < INFLUENCE_BOARD_SIZE; y++) {
        for (int x = 0; x < INFLUENCE_BOARD_SIZE; x++) {
            int16_t val = b->influence[y][x];

            if (val == 0) {
                temp[y][x] = 0;
                continue;
            }

            /* Count neighbors with same sign */
            int same_sign = 0;
            for (int d = 0; d < 4; d++) {
                int ny = y + dy[d];
                int nx = x + dx[d];
                if (ny >= 0 && ny < INFLUENCE_BOARD_SIZE &&
                    nx >= 0 && nx < INFLUENCE_BOARD_SIZE) {
                    int16_t nval = b->influence[ny][nx];
                    if ((val > 0 && nval > 0) || (val < 0 && nval < 0)) {
                        same_sign++;
                    }
                }
            }

            /* Erode if isolated */
            if (same_sign < 2) {
                if (val > 0) {
                    temp[y][x] = val - 1;
                } else {
                    temp[y][x] = val + 1;
                }
            } else {
                temp[y][x] = val;
            }
        }
    }

    memcpy(b->influence, temp, sizeof(temp));
}

/* Full Bouzy algorithm */
static void compute_influence(influence_board_t *b)
{
    init_influence(b);

    /* Dilation phase */
    for (int i = 0; i < INFLUENCE_DILATION; i++) {
        dilate_influence(b);
    }

    /* Erosion phase */
    for (int i = 0; i < INFLUENCE_EROSION; i++) {
        erode_influence(b);
    }
}

/* ============================================================================
 * Territory Estimation
 * ============================================================================ */

/* Classify territory from influence */
static void estimate_territory(influence_board_t *b, int *black_territory, int *white_territory)
{
    *black_territory = 0;
    *white_territory = 0;

    for (int y = 0; y < INFLUENCE_BOARD_SIZE; y++) {
        for (int x = 0; x < INFLUENCE_BOARD_SIZE; x++) {
            int16_t inf = b->influence[y][x];

            if (b->stones[y][x] == EMPTY) {
                if (inf > 10) {
                    b->territory[y][x] = BLACK;
                    (*black_territory)++;
                } else if (inf < -10) {
                    b->territory[y][x] = WHITE;
                    (*white_territory)++;
                } else {
                    b->territory[y][x] = EMPTY;  /* Neutral */
                }
            } else {
                b->territory[y][x] = b->stones[y][x];
            }
        }
    }
}

/* ============================================================================
 * Moyo (potential territory) computation
 * ============================================================================ */

/* Compute moyo using flood fill */
static int compute_moyo(influence_board_t *b, int color)
{
    int8_t visited[INFLUENCE_BOARD_SIZE][INFLUENCE_BOARD_SIZE];
    memset(visited, 0, sizeof(visited));

    int moyo_size = 0;
    int threshold = (color == BLACK) ? 5 : -5;

    for (int y = 0; y < INFLUENCE_BOARD_SIZE; y++) {
        for (int x = 0; x < INFLUENCE_BOARD_SIZE; x++) {
            if (visited[y][x]) continue;

            int16_t inf = b->influence[y][x];
            int is_moyo = (color == BLACK) ? (inf > threshold) : (inf < threshold);

            if (!is_moyo) continue;

            /* Flood fill to find connected moyo region */
            int stack[INFLUENCE_BOARD_SIZE * INFLUENCE_BOARD_SIZE][2];
            int sp = 0;
            int region_size = 0;

            stack[sp][0] = y;
            stack[sp][1] = x;
            sp++;
            visited[y][x] = 1;

            while (sp > 0) {
                sp--;
                int cy = stack[sp][0];
                int cx = stack[sp][1];
                region_size++;

                for (int d = 0; d < 4; d++) {
                    int ny = cy + dy[d];
                    int nx = cx + dx[d];

                    if (ny < 0 || ny >= INFLUENCE_BOARD_SIZE ||
                        nx < 0 || nx >= INFLUENCE_BOARD_SIZE) continue;
                    if (visited[ny][nx]) continue;

                    int16_t ninf = b->influence[ny][nx];
                    int n_is_moyo = (color == BLACK) ? (ninf > threshold) : (ninf < threshold);

                    if (n_is_moyo) {
                        visited[ny][nx] = 1;
                        stack[sp][0] = ny;
                        stack[sp][1] = nx;
                        sp++;
                    }
                }
            }

            moyo_size += region_size;
        }
    }

    return moyo_size;
}

/* ============================================================================
 * Test Board Generation
 * ============================================================================ */

static void generate_board(influence_board_t *b, uint32_t seed)
{
    uint32_t x = seed;

    memset(b->stones, EMPTY, sizeof(b->stones));

    /* Place random stones (typical game position) */
    int num_stones = 40 + (seed % 60);

    for (int i = 0; i < num_stones; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int py = x % INFLUENCE_BOARD_SIZE;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int px = x % INFLUENCE_BOARD_SIZE;

        if (b->stones[py][px] == EMPTY) {
            b->stones[py][px] = (i % 2 == 0) ? BLACK : WHITE;
        }
    }

    /* Add some patterns (corners, sides) */
    /* Star points for 19x19 */
    if (INFLUENCE_BOARD_SIZE == 19) {
        int star_points[][2] = {{3,3}, {3,9}, {3,15}, {9,3}, {9,9}, {9,15}, {15,3}, {15,9}, {15,15}};
        for (int i = 0; i < 9; i++) {
            if ((seed >> i) & 1) {
                int py = star_points[i][0];
                int px = star_points[i][1];
                if (b->stones[py][px] == EMPTY) {
                    b->stones[py][px] = ((seed >> (i+10)) & 1) ? BLACK : WHITE;
                }
            }
        }
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    memset(&board, 0, sizeof(board));
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();
    int total_black = 0;
    int total_white = 0;

    /* Start timing */
    BENCH_START();

    for (int e = 0; e < INFLUENCE_NUM_EVALS; e++) {
        /* Generate board position */
        generate_board(&board, 0x12345678 + e * 1000);

        /* Compute influence */
        compute_influence(&board);

        /* Estimate territory */
        int black_terr, white_terr;
        estimate_territory(&board, &black_terr, &white_terr);
        total_black += black_terr;
        total_white += white_terr;

        /* Compute moyo */
        int black_moyo = compute_moyo(&board, BLACK);
        int white_moyo = compute_moyo(&board, WHITE);

        /* Update checksum */
        csum = checksum_update(csum, (uint32_t)black_terr);
        csum = checksum_update(csum, (uint32_t)white_terr);
        csum = checksum_update(csum, (uint32_t)black_moyo);
        csum = checksum_update(csum, (uint32_t)white_moyo);

        /* Include influence field in checksum */
        for (int y = 0; y < INFLUENCE_BOARD_SIZE; y++) {
            for (int x = 0; x < INFLUENCE_BOARD_SIZE; x++) {
                csum = checksum_update(csum, (uint32_t)(board.influence[y][x] + 128));
            }
        }
    }

    /* End timing */
    BENCH_END();

    csum = checksum_update(csum, (uint32_t)total_black);
    csum = checksum_update(csum, (uint32_t)total_white);

    BENCH_VOLATILE(total_black);

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
    influence_field,
    "Territory influence computation",
    "445.gobmk",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    INFLUENCE_NUM_EVALS
);

KERNEL_REGISTER(influence_field)
