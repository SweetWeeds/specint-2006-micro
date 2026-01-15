/*
 * SPECInt2006-micro: go_liberty kernel
 * Captures liberty counting and string analysis from 445.gobmk
 *
 * Pattern: Graph traversal on Go board, flood-fill, connected components
 * Memory: Irregular access patterns, pointer chasing within groups
 * Branch: Data-dependent (board state)
 */

#include "bench.h"

/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef GO_BOARD_SIZE
#define GO_BOARD_SIZE       9       /* 9x9 board (smaller for micro) */
#endif

#ifndef GO_NUM_STONES
#define GO_NUM_STONES       40      /* Number of stones to place */
#endif

#ifndef GO_NUM_QUERIES
#define GO_NUM_QUERIES      50      /* Number of liberty queries */
#endif

/* Board constants */
#define GO_EMPTY            0
#define GO_BLACK            1
#define GO_WHITE            2
#define GO_BORDER           3

#define GO_MAX_STRINGS      64      /* Maximum number of string groups */
#define GO_MAX_LIBERTIES    (GO_BOARD_SIZE * GO_BOARD_SIZE)

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* String (connected group) representation */
typedef struct {
    int8_t  color;                          /* GO_BLACK or GO_WHITE */
    int16_t stone_count;                    /* Number of stones in string */
    int16_t liberty_count;                  /* Number of liberties */
    uint8_t stones[GO_MAX_LIBERTIES];       /* List of stone positions */
    uint8_t liberties[GO_MAX_LIBERTIES];    /* List of liberty positions */
} go_string_t;

/* Board state */
typedef struct {
    int8_t  board[GO_BOARD_SIZE + 2][GO_BOARD_SIZE + 2];  /* With border */
    int16_t string_id[GO_BOARD_SIZE + 2][GO_BOARD_SIZE + 2]; /* String ID for each point */
    go_string_t strings[GO_MAX_STRINGS];
    int     num_strings;
} go_state_t;

/* Static storage */
static go_state_t state;
static uint8_t query_points[GO_NUM_QUERIES];  /* Points to query liberties for */
static int8_t visited[GO_BOARD_SIZE + 2][GO_BOARD_SIZE + 2];

/* ============================================================================
 * Board Utilities
 * ============================================================================ */

INLINE int pos_to_idx(int x, int y)
{
    return (y * (GO_BOARD_SIZE + 2) + x);
}

static void init_board(go_state_t *gs)
{
    /* Initialize with border */
    for (int y = 0; y < GO_BOARD_SIZE + 2; y++) {
        for (int x = 0; x < GO_BOARD_SIZE + 2; x++) {
            if (x == 0 || x == GO_BOARD_SIZE + 1 ||
                y == 0 || y == GO_BOARD_SIZE + 1) {
                gs->board[y][x] = GO_BORDER;
            } else {
                gs->board[y][x] = GO_EMPTY;
            }
            gs->string_id[y][x] = -1;
        }
    }
    gs->num_strings = 0;
}

/* ============================================================================
 * Flood-fill for String Detection
 * ============================================================================ */

static void flood_fill_string(go_state_t *gs, int start_x, int start_y,
                               go_string_t *str, int8_t color)
{
    /* Stack-based flood fill */
    uint8_t stack_x[GO_MAX_LIBERTIES];
    uint8_t stack_y[GO_MAX_LIBERTIES];
    int stack_ptr = 0;

    str->color = color;
    str->stone_count = 0;
    str->liberty_count = 0;

    /* Clear visited array for liberties */
    memset(visited, 0, sizeof(visited));

    /* Push start position */
    stack_x[stack_ptr] = start_x;
    stack_y[stack_ptr] = start_y;
    stack_ptr++;
    visited[start_y][start_x] = 1;

    while (stack_ptr > 0) {
        /* Pop */
        stack_ptr--;
        int x = stack_x[stack_ptr];
        int y = stack_y[stack_ptr];

        /* Add to string */
        str->stones[str->stone_count++] = pos_to_idx(x, y);

        /* Check four neighbors */
        static const int dx[4] = {-1, 1, 0, 0};
        static const int dy[4] = {0, 0, -1, 1};

        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d];
            int ny = y + dy[d];

            if (visited[ny][nx]) continue;

            int8_t neighbor = gs->board[ny][nx];

            if (neighbor == color) {
                /* Same color - add to stack */
                visited[ny][nx] = 1;
                if (stack_ptr < GO_MAX_LIBERTIES) {
                    stack_x[stack_ptr] = nx;
                    stack_y[stack_ptr] = ny;
                    stack_ptr++;
                }
            } else if (neighbor == GO_EMPTY) {
                /* Empty - it's a liberty */
                visited[ny][nx] = 1;
                str->liberties[str->liberty_count++] = pos_to_idx(nx, ny);
            }
        }
    }
}

/* ============================================================================
 * Liberty Counting Functions
 * ============================================================================ */

/* Count liberties for a single stone or group at (x, y) */
static int count_liberties(go_state_t *gs, int x, int y)
{
    int8_t color = gs->board[y][x];
    if (color == GO_EMPTY || color == GO_BORDER) {
        return 0;
    }

    /* Clear visited */
    memset(visited, 0, sizeof(visited));

    /* BFS to find all liberties of the string */
    uint8_t queue_x[GO_MAX_LIBERTIES];
    uint8_t queue_y[GO_MAX_LIBERTIES];
    int head = 0, tail = 0;
    int liberties = 0;

    queue_x[tail] = x;
    queue_y[tail] = y;
    tail++;
    visited[y][x] = 1;

    while (head < tail) {
        int cx = queue_x[head];
        int cy = queue_y[head];
        head++;

        /* Check four neighbors */
        static const int dx[4] = {-1, 1, 0, 0};
        static const int dy[4] = {0, 0, -1, 1};

        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d];
            int ny = cy + dy[d];

            if (visited[ny][nx]) continue;
            visited[ny][nx] = 1;

            int8_t neighbor = gs->board[ny][nx];

            if (neighbor == color) {
                /* Same color - continue traversal */
                if (tail < GO_MAX_LIBERTIES) {
                    queue_x[tail] = nx;
                    queue_y[tail] = ny;
                    tail++;
                }
            } else if (neighbor == GO_EMPTY) {
                /* Empty - count as liberty */
                liberties++;
            }
        }
    }

    return liberties;
}

/* Check if placing a stone would capture opponent stones */
static int would_capture(go_state_t *gs, int x, int y, int8_t color)
{
    int8_t opponent = (color == GO_BLACK) ? GO_WHITE : GO_BLACK;
    int captures = 0;

    static const int dx[4] = {-1, 1, 0, 0};
    static const int dy[4] = {0, 0, -1, 1};

    for (int d = 0; d < 4; d++) {
        int nx = x + dx[d];
        int ny = y + dy[d];

        if (gs->board[ny][nx] == opponent) {
            /* Temporarily place stone */
            int8_t old = gs->board[y][x];
            gs->board[y][x] = color;

            int libs = count_liberties(gs, nx, ny);
            if (libs == 0) {
                captures++;
            }

            /* Restore */
            gs->board[y][x] = old;
        }
    }

    return captures;
}

/* Evaluate influence at a point (simple version) */
static int evaluate_influence(go_state_t *gs, int x, int y)
{
    int black_influence = 0;
    int white_influence = 0;

    /* Count nearby stones with distance weighting */
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            int nx = x + dx;
            int ny = y + dy;

            if (nx < 1 || nx > GO_BOARD_SIZE || ny < 1 || ny > GO_BOARD_SIZE) {
                continue;
            }

            int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (dist == 0) continue;

            int weight = 10 - dist * 2;
            if (weight <= 0) continue;

            if (gs->board[ny][nx] == GO_BLACK) {
                black_influence += weight;
            } else if (gs->board[ny][nx] == GO_WHITE) {
                white_influence += weight;
            }
        }
    }

    return black_influence - white_influence;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_position(go_state_t *gs, uint32_t seed)
{
    uint32_t x = seed;

    init_board(gs);

    /* Place stones randomly */
    int placed = 0;
    int attempts = 0;
    int8_t color = GO_BLACK;

    while (placed < GO_NUM_STONES && attempts < GO_NUM_STONES * 10) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int px = 1 + (x % GO_BOARD_SIZE);
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int py = 1 + (x % GO_BOARD_SIZE);

        if (gs->board[py][px] == GO_EMPTY) {
            gs->board[py][px] = color;
            placed++;
            color = (color == GO_BLACK) ? GO_WHITE : GO_BLACK;
        }
        attempts++;
    }

    /* Generate query points (stones and empty points) */
    for (int i = 0; i < GO_NUM_QUERIES; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int qx = 1 + (x % GO_BOARD_SIZE);
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int qy = 1 + (x % GO_BOARD_SIZE);
        query_points[i] = pos_to_idx(qx, qy);
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    generate_position(&state, 0xDEADBEEF);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();

    int total_liberties = 0;
    int total_captures = 0;
    int total_influence = 0;
    int strings_found = 0;

    BENCH_START();

    /* Query 1: Count liberties for various points */
    for (int i = 0; i < GO_NUM_QUERIES; i++) {
        int idx = query_points[i];
        int x = idx % (GO_BOARD_SIZE + 2);
        int y = idx / (GO_BOARD_SIZE + 2);

        int libs = count_liberties(&state, x, y);
        total_liberties += libs;
        csum = checksum_update(csum, (uint32_t)libs);
    }

    /* Query 2: Check potential captures */
    for (int y = 1; y <= GO_BOARD_SIZE; y++) {
        for (int x = 1; x <= GO_BOARD_SIZE; x++) {
            if (state.board[y][x] == GO_EMPTY) {
                int cap_black = would_capture(&state, x, y, GO_BLACK);
                int cap_white = would_capture(&state, x, y, GO_WHITE);
                total_captures += cap_black + cap_white;
                csum = checksum_update(csum, (uint32_t)(cap_black * 16 + cap_white));
            }
        }
    }

    /* Query 3: Evaluate influence map */
    for (int y = 1; y <= GO_BOARD_SIZE; y++) {
        for (int x = 1; x <= GO_BOARD_SIZE; x++) {
            int inf = evaluate_influence(&state, x, y);
            total_influence += inf;
            csum = checksum_update(csum, (uint32_t)(int32_t)inf);
        }
    }

    /* Query 4: Find all strings */
    memset(visited, 0, sizeof(visited));
    for (int y = 1; y <= GO_BOARD_SIZE; y++) {
        for (int x = 1; x <= GO_BOARD_SIZE; x++) {
            if (state.board[y][x] != GO_EMPTY &&
                state.board[y][x] != GO_BORDER &&
                !visited[y][x]) {
                if (state.num_strings < GO_MAX_STRINGS) {
                    go_string_t *str = &state.strings[state.num_strings];
                    flood_fill_string(&state, x, y, str, state.board[y][x]);
                    state.num_strings++;
                    strings_found++;

                    csum = checksum_update(csum, (uint32_t)str->stone_count);
                    csum = checksum_update(csum, (uint32_t)str->liberty_count);
                }
            }
        }
    }

    BENCH_END();

    /* Final checksum updates */
    csum = checksum_update(csum, (uint32_t)total_liberties);
    csum = checksum_update(csum, (uint32_t)total_captures);
    csum = checksum_update(csum, (uint32_t)(int32_t)total_influence);
    csum = checksum_update(csum, (uint32_t)strings_found);

    BENCH_VOLATILE(total_liberties);
    BENCH_VOLATILE(total_captures);
    BENCH_VOLATILE(total_influence);
    BENCH_VOLATILE(strings_found);

    result.cycles = BENCH_CYCLES();
    result.checksum = csum;

    return result;
}

static void kernel_cleanup_func(void)
{
    /* Reset state for next run */
    state.num_strings = 0;
}

/* ============================================================================
 * Kernel Registration
 * ============================================================================ */

KERNEL_DECLARE(
    go_liberty,
    "Go board liberty counting and string analysis",
    "445.gobmk",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    GO_NUM_QUERIES
);

KERNEL_REGISTER(go_liberty)
