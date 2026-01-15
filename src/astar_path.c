/*
 * SPECInt2006-micro: astar_path kernel
 * Captures A* pathfinding from 473.astar
 *
 * Pattern: Priority queue operations, heuristic search, graph traversal
 * Memory: Grid access, heap manipulation
 * Branch: Data-dependent (obstacle layout, path decisions)
 */

#include "bench.h"

/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef ASTAR_MAP_SIZE
#define ASTAR_MAP_SIZE          32      /* 32x32 map */
#endif

#ifndef ASTAR_NUM_OBSTACLES
#define ASTAR_NUM_OBSTACLES     200     /* Number of obstacle cells */
#endif

#ifndef ASTAR_NUM_QUERIES
#define ASTAR_NUM_QUERIES       10      /* Number of pathfinding queries */
#endif

/* Map cell types */
#define CELL_EMPTY              0
#define CELL_OBSTACLE           255
#define CELL_START              1
#define CELL_GOAL               2

/* Cost constants */
#define COST_STRAIGHT           10      /* Cost of moving straight */
#define COST_DIAGONAL           14      /* Cost of moving diagonally (approx sqrt(2)*10) */
#define COST_INFINITE           0x7FFFFFFF

/* Priority queue size */
#define PQ_MAX_SIZE             (ASTAR_MAP_SIZE * ASTAR_MAP_SIZE)

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Map cell */
typedef struct {
    uint8_t terrain;        /* Terrain cost multiplier (0=obstacle, 1-254=passable) */
    uint8_t visited;        /* Already expanded */
} map_cell_t;

/* A* node */
typedef struct {
    int16_t x, y;           /* Position */
    int32_t g;              /* Cost from start */
    int32_t f;              /* g + heuristic */
    int16_t parent_x;       /* Parent position for path reconstruction */
    int16_t parent_y;
} astar_node_t;

/* Priority queue (min-heap by f value) */
typedef struct {
    astar_node_t nodes[PQ_MAX_SIZE];
    int size;
} priority_queue_t;

/* Map state */
typedef struct {
    map_cell_t cells[ASTAR_MAP_SIZE][ASTAR_MAP_SIZE];
    int32_t g_cost[ASTAR_MAP_SIZE][ASTAR_MAP_SIZE];
    int width;
    int height;
} map_t;

/* Query definition */
typedef struct {
    int16_t start_x, start_y;
    int16_t goal_x, goal_y;
} path_query_t;

/* Static storage */
static map_t map;
static priority_queue_t open_set;
static path_query_t queries[ASTAR_NUM_QUERIES];

/* ============================================================================
 * Priority Queue Operations (Binary Min-Heap)
 * ============================================================================ */

static void pq_init(priority_queue_t *pq)
{
    pq->size = 0;
}

INLINE void pq_swap(astar_node_t *a, astar_node_t *b)
{
    astar_node_t temp = *a;
    *a = *b;
    *b = temp;
}

static void pq_push(priority_queue_t *pq, const astar_node_t *node)
{
    if (pq->size >= PQ_MAX_SIZE) return;

    /* Add to end */
    int i = pq->size;
    pq->nodes[i] = *node;
    pq->size++;

    /* Bubble up */
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (pq->nodes[i].f < pq->nodes[parent].f) {
            pq_swap(&pq->nodes[i], &pq->nodes[parent]);
            i = parent;
        } else {
            break;
        }
    }
}

static astar_node_t pq_pop(priority_queue_t *pq)
{
    astar_node_t result = pq->nodes[0];

    /* Move last to root */
    pq->size--;
    if (pq->size > 0) {
        pq->nodes[0] = pq->nodes[pq->size];

        /* Bubble down */
        int i = 0;
        while (1) {
            int left = 2 * i + 1;
            int right = 2 * i + 2;
            int smallest = i;

            if (left < pq->size && pq->nodes[left].f < pq->nodes[smallest].f) {
                smallest = left;
            }
            if (right < pq->size && pq->nodes[right].f < pq->nodes[smallest].f) {
                smallest = right;
            }

            if (smallest != i) {
                pq_swap(&pq->nodes[i], &pq->nodes[smallest]);
                i = smallest;
            } else {
                break;
            }
        }
    }

    return result;
}

INLINE int pq_empty(const priority_queue_t *pq)
{
    return pq->size == 0;
}

/* ============================================================================
 * Heuristic Functions
 * ============================================================================ */

/* Manhattan distance heuristic */
INLINE int32_t heuristic_manhattan(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (dx + dy) * COST_STRAIGHT;
}

/* Diagonal distance heuristic (Chebyshev) */
INLINE int32_t heuristic_diagonal(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2;
    int dy = y1 - y2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int diag = dx < dy ? dx : dy;
    int straight = dx + dy - 2 * diag;

    return diag * COST_DIAGONAL + straight * COST_STRAIGHT;
}

/* ============================================================================
 * A* Search Algorithm
 * ============================================================================ */

/* Direction offsets for 8-directional movement */
static const int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
static const int dy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
static const int cost8[8] = {
    COST_DIAGONAL, COST_STRAIGHT, COST_DIAGONAL,
    COST_STRAIGHT, COST_STRAIGHT,
    COST_DIAGONAL, COST_STRAIGHT, COST_DIAGONAL
};

/* Find path from start to goal, returns path length or -1 if not found */
static int astar_search(map_t *m, int sx, int sy, int gx, int gy, int *nodes_expanded)
{
    *nodes_expanded = 0;

    /* Validate positions */
    if (sx < 0 || sx >= m->width || sy < 0 || sy >= m->height ||
        gx < 0 || gx >= m->width || gy < 0 || gy >= m->height) {
        return -1;
    }

    if (m->cells[sy][sx].terrain == CELL_OBSTACLE ||
        m->cells[gy][gx].terrain == CELL_OBSTACLE) {
        return -1;
    }

    /* Initialize g_cost array */
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            m->g_cost[y][x] = COST_INFINITE;
            m->cells[y][x].visited = 0;
        }
    }

    /* Initialize open set with start node */
    pq_init(&open_set);

    astar_node_t start_node = {
        .x = sx, .y = sy,
        .g = 0,
        .f = heuristic_diagonal(sx, sy, gx, gy),
        .parent_x = -1, .parent_y = -1
    };
    pq_push(&open_set, &start_node);
    m->g_cost[sy][sx] = 0;

    /* A* main loop */
    while (!pq_empty(&open_set)) {
        astar_node_t current = pq_pop(&open_set);
        int cx = current.x;
        int cy = current.y;

        /* Skip if already visited with better cost */
        if (m->cells[cy][cx].visited) {
            continue;
        }
        m->cells[cy][cx].visited = 1;
        (*nodes_expanded)++;

        /* Goal reached? */
        if (cx == gx && cy == gy) {
            /* Simple path length estimation from g cost */
            int path_len = current.g / COST_STRAIGHT;
            return path_len;
        }

        /* Expand neighbors */
        for (int d = 0; d < 8; d++) {
            int nx = cx + dx8[d];
            int ny = cy + dy8[d];

            /* Check bounds */
            if (nx < 0 || nx >= m->width || ny < 0 || ny >= m->height) {
                continue;
            }

            /* Check obstacle */
            if (m->cells[ny][nx].terrain == CELL_OBSTACLE) {
                continue;
            }

            /* Check already visited */
            if (m->cells[ny][nx].visited) {
                continue;
            }

            /* Calculate new g cost */
            int32_t move_cost = cost8[d] * m->cells[ny][nx].terrain;
            int32_t new_g = current.g + move_cost;

            /* Update if better path found */
            if (new_g < m->g_cost[ny][nx]) {
                m->g_cost[ny][nx] = new_g;

                astar_node_t neighbor = {
                    .x = nx, .y = ny,
                    .g = new_g,
                    .f = new_g + heuristic_diagonal(nx, ny, gx, gy),
                    .parent_x = cx, .parent_y = cy
                };
                pq_push(&open_set, &neighbor);
            }
        }
    }

    /* No path found */
    return -1;
}

/* ============================================================================
 * Map Generation
 * ============================================================================ */

static void generate_map(map_t *m, uint32_t seed)
{
    m->width = ASTAR_MAP_SIZE;
    m->height = ASTAR_MAP_SIZE;

    uint32_t x = seed;

    /* Initialize all cells as passable with varying terrain costs */
    for (int y = 0; y < m->height; y++) {
        for (int x_ = 0; x_ < m->width; x_++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            /* Terrain cost 1-3 */
            m->cells[y][x_].terrain = 1 + (x % 3);
            m->cells[y][x_].visited = 0;
        }
    }

    /* Add obstacles */
    for (int i = 0; i < ASTAR_NUM_OBSTACLES; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int ox = x % m->width;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int oy = x % m->height;

        m->cells[oy][ox].terrain = CELL_OBSTACLE;
    }

    /* Ensure corners are passable for queries */
    m->cells[0][0].terrain = 1;
    m->cells[0][m->width - 1].terrain = 1;
    m->cells[m->height - 1][0].terrain = 1;
    m->cells[m->height - 1][m->width - 1].terrain = 1;
    m->cells[m->height / 2][m->width / 2].terrain = 1;

    /* Generate queries */
    for (int i = 0; i < ASTAR_NUM_QUERIES; i++) {
        /* Ensure start and goal are passable */
        do {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            queries[i].start_x = x % m->width;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            queries[i].start_y = x % m->height;
        } while (m->cells[queries[i].start_y][queries[i].start_x].terrain == CELL_OBSTACLE);

        do {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            queries[i].goal_x = x % m->width;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            queries[i].goal_y = x % m->height;
        } while (m->cells[queries[i].goal_y][queries[i].goal_x].terrain == CELL_OBSTACLE);
    }
}

/* ============================================================================
 * Additional Pathfinding Utilities
 * ============================================================================ */

/* Flood fill to check connectivity */
static int flood_fill_count(map_t *m, int sx, int sy)
{
    if (sx < 0 || sx >= m->width || sy < 0 || sy >= m->height) {
        return 0;
    }
    if (m->cells[sy][sx].terrain == CELL_OBSTACLE) {
        return 0;
    }

    /* BFS flood fill */
    static uint8_t ff_visited[ASTAR_MAP_SIZE][ASTAR_MAP_SIZE];
    memset(ff_visited, 0, sizeof(ff_visited));

    static int queue_x[PQ_MAX_SIZE];
    static int queue_y[PQ_MAX_SIZE];
    int head = 0, tail = 0;

    queue_x[tail] = sx;
    queue_y[tail] = sy;
    tail++;
    ff_visited[sy][sx] = 1;

    int count = 0;

    while (head < tail) {
        int cx = queue_x[head];
        int cy = queue_y[head];
        head++;
        count++;

        /* Check 4-directional neighbors */
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx8[d * 2 + 1];  /* Only straight directions */
            int ny = cy + dy8[d * 2 + 1];

            if (nx < 0 || nx >= m->width || ny < 0 || ny >= m->height) {
                continue;
            }
            if (ff_visited[ny][nx]) {
                continue;
            }
            if (m->cells[ny][nx].terrain == CELL_OBSTACLE) {
                continue;
            }

            ff_visited[ny][nx] = 1;
            if (tail < PQ_MAX_SIZE) {
                queue_x[tail] = nx;
                queue_y[tail] = ny;
                tail++;
            }
        }
    }

    return count;
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    generate_map(&map, 0xFEEDFACE);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();

    int total_path_length = 0;
    int total_nodes_expanded = 0;
    int paths_found = 0;
    int paths_not_found = 0;

    BENCH_START();

    /* Run A* queries */
    for (int q = 0; q < ASTAR_NUM_QUERIES; q++) {
        int nodes_expanded = 0;
        int path_len = astar_search(&map,
                                    queries[q].start_x, queries[q].start_y,
                                    queries[q].goal_x, queries[q].goal_y,
                                    &nodes_expanded);

        total_nodes_expanded += nodes_expanded;
        csum = checksum_update(csum, (uint32_t)nodes_expanded);

        if (path_len >= 0) {
            total_path_length += path_len;
            paths_found++;
            csum = checksum_update(csum, (uint32_t)path_len);
        } else {
            paths_not_found++;
            csum = checksum_update(csum, 0xFFFFFFFF);
        }
    }

    /* Additional metrics: flood fill from center */
    int connectivity = flood_fill_count(&map, map.width / 2, map.height / 2);
    csum = checksum_update(csum, (uint32_t)connectivity);

    /* Heuristic calculations */
    int32_t heuristic_sum = 0;
    for (int q = 0; q < ASTAR_NUM_QUERIES; q++) {
        int32_t h = heuristic_diagonal(queries[q].start_x, queries[q].start_y,
                                       queries[q].goal_x, queries[q].goal_y);
        heuristic_sum += h;
        csum = checksum_update(csum, (uint32_t)h);
    }

    BENCH_END();

    /* Final checksum */
    csum = checksum_update(csum, (uint32_t)total_path_length);
    csum = checksum_update(csum, (uint32_t)total_nodes_expanded);
    csum = checksum_update(csum, (uint32_t)paths_found);
    csum = checksum_update(csum, (uint32_t)paths_not_found);

    BENCH_VOLATILE(total_path_length);
    BENCH_VOLATILE(total_nodes_expanded);
    BENCH_VOLATILE(paths_found);
    BENCH_VOLATILE(connectivity);

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
    astar_path,
    "A* pathfinding on 2D grid maps",
    "473.astar",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    ASTAR_NUM_QUERIES
);

KERNEL_REGISTER(astar_path)
