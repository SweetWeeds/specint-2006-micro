/*
 * SPECInt2006-micro: game_tree kernel
 * Captures alpha-beta search from 458.sjeng
 *
 * Pattern: Recursive game tree search with pruning
 * Memory: Stack-heavy with hash table lookups
 * Branch: Highly unpredictable (game-dependent)
 */

#include "bench.h"


/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef GAME_SEARCH_DEPTH
#define GAME_SEARCH_DEPTH   4       /* Search depth */
#endif

#ifndef GAME_BRANCHING
#define GAME_BRANCHING      8       /* Average branching factor */
#endif

#ifndef GAME_TT_SIZE
#define GAME_TT_SIZE        256     /* Transposition table size */
#endif

#ifndef GAME_BOARD_SIZE
#define GAME_BOARD_SIZE     64      /* 8x8 board */
#endif

/* Score constants */
#define SCORE_INF           30000
#define SCORE_MATE          20000

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Move representation */
typedef struct {
    uint8_t from;
    uint8_t to;
    int16_t score;          /* Move ordering score */
} move_t;

/* Transposition table entry */
typedef struct {
    uint64_t hash;
    int16_t score;
    uint8_t depth;
    uint8_t flag;           /* 0=exact, 1=lower, 2=upper */
    move_t best_move;
} tt_entry_t;

/* Game state */
typedef struct {
    int8_t board[GAME_BOARD_SIZE];
    uint64_t hash;
    int8_t side_to_move;    /* 1 = white, -1 = black */
    int ply;                /* Current ply in search */
} game_state_t;

/* Killer moves (for move ordering) */
typedef struct {
    move_t killer1;
    move_t killer2;
} killer_t;

/* Static storage */
static game_state_t state;
static tt_entry_t tt[GAME_TT_SIZE];
static killer_t killers[GAME_SEARCH_DEPTH + 1];
static uint64_t zobrist_piece[12][GAME_BOARD_SIZE];
static uint64_t zobrist_side;
static int nodes_searched;

/* ============================================================================
 * Zobrist Hashing
 * ============================================================================ */

static void init_zobrist(uint32_t seed)
{
    uint32_t x = seed;

    for (int p = 0; p < 12; p++) {
        for (int sq = 0; sq < GAME_BOARD_SIZE; sq++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            uint64_t h = x;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            h = (h << 32) | x;
            zobrist_piece[p][sq] = h;
        }
    }

    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    zobrist_side = ((uint64_t)x << 32);
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    zobrist_side |= x;
}

static uint64_t compute_hash(const game_state_t *gs)
{
    uint64_t h = 0;

    for (int sq = 0; sq < GAME_BOARD_SIZE; sq++) {
        int piece = gs->board[sq];
        if (piece != 0) {
            int p_idx = (piece > 0) ? (piece - 1) : (6 - piece - 1);
            h ^= zobrist_piece[p_idx][sq];
        }
    }

    if (gs->side_to_move < 0) {
        h ^= zobrist_side;
    }

    return h;
}

/* ============================================================================
 * Transposition Table
 * ============================================================================ */

static tt_entry_t *tt_probe(uint64_t hash)
{
    int idx = (int)(hash % GAME_TT_SIZE);
    if (tt[idx].hash == hash) {
        return &tt[idx];
    }
    return NULL;
}

static void tt_store(uint64_t hash, int16_t score, uint8_t depth, uint8_t flag, move_t *best)
{
    int idx = (int)(hash % GAME_TT_SIZE);
    tt[idx].hash = hash;
    tt[idx].score = score;
    tt[idx].depth = depth;
    tt[idx].flag = flag;
    if (best) {
        tt[idx].best_move = *best;
    }
}

/* ============================================================================
 * Move Generation and Evaluation
 * ============================================================================ */

/* Generate pseudo-legal moves (simplified) */
static int generate_moves(const game_state_t *gs, move_t *moves)
{
    int num_moves = 0;
    int side = gs->side_to_move;

    for (int sq = 0; sq < GAME_BOARD_SIZE; sq++) {
        int piece = gs->board[sq];
        if (piece == 0 || (piece > 0) != (side > 0)) {
            continue;
        }

        /* Generate moves for this piece (simplified - just adjacent squares) */
        int x = sq % 8;
        int y = sq / 8;

        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;

                int nx = x + dx;
                int ny = y + dy;

                if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
                    int to = ny * 8 + nx;
                    int target = gs->board[to];

                    /* Can move to empty or capture opponent */
                    if (target == 0 || (target > 0) != (piece > 0)) {
                        moves[num_moves].from = sq;
                        moves[num_moves].to = to;
                        moves[num_moves].score = 0;
                        num_moves++;

                        if (num_moves >= GAME_BRANCHING * 4) {
                            return num_moves;
                        }
                    }
                }
            }
        }
    }

    return num_moves;
}

/* Simple evaluation function */
static int16_t evaluate(const game_state_t *gs)
{
    int16_t score = 0;

    /* Material count with piece values */
    static const int16_t piece_values[7] = {0, 100, 300, 300, 500, 900, 10000};

    for (int sq = 0; sq < GAME_BOARD_SIZE; sq++) {
        int piece = gs->board[sq];
        if (piece != 0) {
            int value = piece_values[piece > 0 ? piece : -piece];
            score += (piece > 0) ? value : -value;
        }
    }

    /* Position bonus (center control) */
    for (int sq = 0; sq < GAME_BOARD_SIZE; sq++) {
        int piece = gs->board[sq];
        if (piece != 0) {
            int x = sq % 8;
            int y = sq / 8;
            int center_dist = abs(x - 3) + abs(y - 3);
            int bonus = (4 - center_dist) * 5;
            score += (piece > 0) ? bonus : -bonus;
        }
    }

    return gs->side_to_move > 0 ? score : -score;
}

/* Move ordering */
static void order_moves(move_t *moves, int num_moves, int ply, move_t *tt_move)
{
    /* Score moves for ordering */
    for (int i = 0; i < num_moves; i++) {
        moves[i].score = 0;

        /* TT move gets highest priority */
        if (tt_move && moves[i].from == tt_move->from && moves[i].to == tt_move->to) {
            moves[i].score = 10000;
        }
        /* Killer moves */
        else if (ply < GAME_SEARCH_DEPTH) {
            if (moves[i].from == killers[ply].killer1.from &&
                moves[i].to == killers[ply].killer1.to) {
                moves[i].score = 9000;
            } else if (moves[i].from == killers[ply].killer2.from &&
                       moves[i].to == killers[ply].killer2.to) {
                moves[i].score = 8000;
            }
        }
    }

    /* Simple insertion sort */
    for (int i = 1; i < num_moves; i++) {
        move_t key = moves[i];
        int j = i - 1;
        while (j >= 0 && moves[j].score < key.score) {
            moves[j + 1] = moves[j];
            j--;
        }
        moves[j + 1] = key;
    }
}

/* ============================================================================
 * Alpha-Beta Search
 * ============================================================================ */

static void make_move(game_state_t *gs, const move_t *move)
{
    int piece = gs->board[move->from];
    gs->board[move->to] = piece;
    gs->board[move->from] = 0;
    gs->side_to_move = -gs->side_to_move;
    gs->ply++;
    gs->hash ^= zobrist_side;
}

static void unmake_move(game_state_t *gs, const move_t *move, int8_t captured)
{
    int piece = gs->board[move->to];
    gs->board[move->from] = piece;
    gs->board[move->to] = captured;
    gs->side_to_move = -gs->side_to_move;
    gs->ply--;
    gs->hash ^= zobrist_side;
}

static int16_t alpha_beta(game_state_t *gs, int depth, int16_t alpha, int16_t beta)
{
    nodes_searched++;

    /* Check TT */
    tt_entry_t *tt_entry = tt_probe(gs->hash);
    move_t *tt_move = NULL;

    if (tt_entry && tt_entry->depth >= depth) {
        if (tt_entry->flag == 0) {
            return tt_entry->score;
        } else if (tt_entry->flag == 1 && tt_entry->score >= beta) {
            return beta;
        } else if (tt_entry->flag == 2 && tt_entry->score <= alpha) {
            return alpha;
        }
        tt_move = &tt_entry->best_move;
    }

    /* Leaf node */
    if (depth == 0) {
        return evaluate(gs);
    }

    /* Generate and order moves */
    move_t moves[GAME_BRANCHING * 4];
    int num_moves = generate_moves(gs, moves);

    if (num_moves == 0) {
        return -SCORE_MATE + gs->ply;  /* Checkmate or stalemate */
    }

    order_moves(moves, num_moves, gs->ply, tt_move);

    /* Limit branching factor */
    if (num_moves > GAME_BRANCHING) {
        num_moves = GAME_BRANCHING;
    }

    /* Search moves */
    int16_t best_score = -SCORE_INF;
    move_t best_move = moves[0];
    uint8_t flag = 2;  /* Upper bound initially */

    for (int i = 0; i < num_moves; i++) {
        int8_t captured = gs->board[moves[i].to];

        make_move(gs, &moves[i]);
        int16_t score = -alpha_beta(gs, depth - 1, -beta, -alpha);
        unmake_move(gs, &moves[i], captured);

        if (score > best_score) {
            best_score = score;
            best_move = moves[i];

            if (score > alpha) {
                alpha = score;
                flag = 0;  /* Exact score */

                if (score >= beta) {
                    /* Beta cutoff - store killer move */
                    if (gs->ply < GAME_SEARCH_DEPTH && captured == 0) {
                        killers[gs->ply].killer2 = killers[gs->ply].killer1;
                        killers[gs->ply].killer1 = moves[i];
                    }
                    flag = 1;  /* Lower bound */
                    break;
                }
            }
        }
    }

    /* Store in TT */
    tt_store(gs->hash, best_score, depth, flag, &best_move);

    return best_score;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void init_board(game_state_t *gs, uint32_t seed)
{
    /* Clear board */
    memset(gs->board, 0, sizeof(gs->board));

    /* Set up initial position (simplified chess-like) */
    /* Pawns: value 1 */
    for (int i = 0; i < 8; i++) {
        gs->board[8 + i] = 1;   /* White pawns */
        gs->board[48 + i] = -1; /* Black pawns */
    }

    /* Pieces: values 2-6 */
    gs->board[0] = gs->board[7] = 4;    /* Rooks */
    gs->board[1] = gs->board[6] = 2;    /* Knights */
    gs->board[2] = gs->board[5] = 3;    /* Bishops */
    gs->board[3] = 5;                    /* Queen */
    gs->board[4] = 6;                    /* King */

    gs->board[56] = gs->board[63] = -4;
    gs->board[57] = gs->board[62] = -2;
    gs->board[58] = gs->board[61] = -3;
    gs->board[59] = -5;
    gs->board[60] = -6;

    /* Make a few random moves to vary the position */
    uint32_t x = seed;
    for (int i = 0; i < 4; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int from = x % 64;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int to = x % 64;

        if (gs->board[from] != 0 && gs->board[to] == 0) {
            gs->board[to] = gs->board[from];
            gs->board[from] = 0;
        }
    }

    gs->side_to_move = 1;
    gs->ply = 0;
    gs->hash = compute_hash(gs);
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    /* Initialize Zobrist keys */
    init_zobrist(0xCAFEBABE);

    /* Clear TT */
    memset(tt, 0, sizeof(tt));

    /* Clear killers */
    memset(killers, 0, sizeof(killers));

    /* Initialize board */
    init_board(&state, 0x12345678);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };

    nodes_searched = 0;

    /* Start timing */
    BENCH_START();

    /* Run alpha-beta search */
    int16_t score = alpha_beta(&state, GAME_SEARCH_DEPTH, -SCORE_INF, SCORE_INF);

    /* End timing */
    BENCH_END();

    /* Prevent optimization */
    BENCH_VOLATILE(score);
    BENCH_VOLATILE(nodes_searched);

    /* Checksum */
    uint32_t csum = checksum_init();
    csum = checksum_update(csum, (uint32_t)(int32_t)score);
    csum = checksum_update(csum, (uint32_t)nodes_searched);
    csum = checksum_update(csum, GAME_SEARCH_DEPTH);

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
    game_tree,
    "Alpha-beta game tree search",
    "458.sjeng",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    1
);

KERNEL_REGISTER(game_tree)
