/*
 * SPECInt2006-micro: ssa_dataflow kernel
 * Captures SSA form and dataflow analysis from 403.gcc
 *
 * Pattern: Control flow graph, dominators, phi functions
 * Memory: Graph traversal with work lists
 * Branch: Complex control flow patterns
 */

#include "bench.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef CFG_MAX_BLOCKS
#define CFG_MAX_BLOCKS      64
#endif

#ifndef CFG_MAX_EDGES
#define CFG_MAX_EDGES       128
#endif

#ifndef CFG_MAX_VARS
#define CFG_MAX_VARS        32
#endif

#ifndef CFG_NUM_CFGS
#define CFG_NUM_CFGS        5
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Basic block */
typedef struct {
    int16_t id;
    int16_t num_succs;
    int16_t num_preds;
    int16_t succs[4];       /* Successor blocks */
    int16_t preds[4];       /* Predecessor blocks */
    int16_t idom;           /* Immediate dominator */
    int16_t dom_level;      /* Depth in dominator tree */
    uint64_t def_vars;      /* Variables defined in this block */
    uint64_t use_vars;      /* Variables used in this block */
    uint64_t live_in;       /* Live-in variables */
    uint64_t live_out;      /* Live-out variables */
    uint8_t phi_funcs[CFG_MAX_VARS];  /* Phi function count per var */
} basic_block_t;

/* Control Flow Graph */
typedef struct {
    basic_block_t blocks[CFG_MAX_BLOCKS];
    int16_t num_blocks;
    int16_t entry;
    int16_t exit;
    uint64_t dom_frontier[CFG_MAX_BLOCKS];  /* Dominance frontier sets */
} cfg_t;

/* Static storage */
static cfg_t cfg;

/* ============================================================================
 * Dominator Tree Construction (Cooper-Harvey-Kennedy algorithm)
 * ============================================================================ */

/* Intersect function for dominator computation */
static int16_t intersect(const cfg_t *g, int16_t b1, int16_t b2)
{
    int16_t finger1 = b1;
    int16_t finger2 = b2;
    int max_iters = CFG_MAX_BLOCKS * 2;  /* Safety bound */

    while (finger1 != finger2 && max_iters-- > 0) {
        while (g->blocks[finger1].dom_level > g->blocks[finger2].dom_level && max_iters-- > 0) {
            int16_t next = g->blocks[finger1].idom;
            if (next < 0 || next >= g->num_blocks || next == finger1) break;
            finger1 = next;
        }
        while (g->blocks[finger2].dom_level > g->blocks[finger1].dom_level && max_iters-- > 0) {
            int16_t next = g->blocks[finger2].idom;
            if (next < 0 || next >= g->num_blocks || next == finger2) break;
            finger2 = next;
        }
        /* If dom_levels are equal but fingers differ, move both up */
        if (finger1 != finger2 &&
            g->blocks[finger1].dom_level == g->blocks[finger2].dom_level) {
            int16_t next1 = g->blocks[finger1].idom;
            int16_t next2 = g->blocks[finger2].idom;
            if (next1 >= 0 && next1 < g->num_blocks && next1 != finger1) finger1 = next1;
            if (next2 >= 0 && next2 < g->num_blocks && next2 != finger2) finger2 = next2;
        }
    }

    return finger1;
}

/* Compute dominators using iterative algorithm */
static void compute_dominators(cfg_t *g)
{
    /* Initialize */
    for (int i = 0; i < g->num_blocks; i++) {
        g->blocks[i].idom = -1;
        g->blocks[i].dom_level = 0;
    }
    g->blocks[g->entry].idom = g->entry;
    g->blocks[g->entry].dom_level = 0;

    /* Iterate until fixed point */
    int changed = 1;
    while (changed) {
        changed = 0;

        for (int i = 0; i < g->num_blocks; i++) {
            if (i == g->entry) continue;

            basic_block_t *b = &g->blocks[i];
            if (b->num_preds == 0) continue;

            /* Find first processed predecessor */
            int16_t new_idom = -1;
            for (int p = 0; p < b->num_preds; p++) {
                int16_t pred = b->preds[p];
                if (g->blocks[pred].idom >= 0) {
                    new_idom = pred;
                    break;
                }
            }

            if (new_idom < 0) continue;

            /* Intersect with other predecessors */
            for (int p = 0; p < b->num_preds; p++) {
                int16_t pred = b->preds[p];
                if (pred != new_idom && g->blocks[pred].idom >= 0) {
                    new_idom = intersect(g, pred, new_idom);
                }
            }

            if (b->idom != new_idom) {
                b->idom = new_idom;
                b->dom_level = g->blocks[new_idom].dom_level + 1;
                changed = 1;
            }
        }
    }
}

/* Compute dominance frontiers */
static void compute_dominance_frontier(cfg_t *g)
{
    for (int i = 0; i < g->num_blocks; i++) {
        g->dom_frontier[i] = 0;
    }

    for (int i = 0; i < g->num_blocks; i++) {
        basic_block_t *b = &g->blocks[i];
        if (b->num_preds < 2) continue;

        for (int p = 0; p < b->num_preds; p++) {
            int16_t runner = b->preds[p];
            while (runner >= 0 && runner != b->idom) {
                g->dom_frontier[runner] |= (1ULL << i);
                runner = g->blocks[runner].idom;
            }
        }
    }
}

/* ============================================================================
 * SSA Phi Function Placement
 * ============================================================================ */

/* Place phi functions using dominance frontiers */
static int place_phi_functions(cfg_t *g)
{
    int total_phi = 0;

    /* Clear phi counts */
    for (int i = 0; i < g->num_blocks; i++) {
        memset(g->blocks[i].phi_funcs, 0, CFG_MAX_VARS);
    }

    /* For each variable */
    for (int v = 0; v < CFG_MAX_VARS; v++) {
        uint64_t var_mask = 1ULL << v;

        /* Find all blocks that define this variable */
        uint64_t def_blocks = 0;
        for (int i = 0; i < g->num_blocks; i++) {
            if (g->blocks[i].def_vars & var_mask) {
                def_blocks |= (1ULL << i);
            }
        }

        if (def_blocks == 0) continue;

        /* Work list of blocks needing phi functions */
        uint64_t worklist = def_blocks;
        uint64_t has_phi = 0;

        while (worklist) {
            /* Get next block from worklist */
            int b = 0;
            while (!(worklist & (1ULL << b))) b++;
            worklist &= ~(1ULL << b);

            /* Add phi to dominance frontier */
            uint64_t df = g->dom_frontier[b];
            while (df) {
                int y = 0;
                while (!(df & (1ULL << y))) y++;
                df &= ~(1ULL << y);

                if (!(has_phi & (1ULL << y))) {
                    g->blocks[y].phi_funcs[v]++;
                    total_phi++;
                    has_phi |= (1ULL << y);

                    if (!(def_blocks & (1ULL << y))) {
                        worklist |= (1ULL << y);
                    }
                }
            }
        }
    }

    return total_phi;
}

/* ============================================================================
 * Liveness Analysis
 * ============================================================================ */

/* Compute live-in and live-out sets */
static void compute_liveness(cfg_t *g)
{
    /* Initialize */
    for (int i = 0; i < g->num_blocks; i++) {
        g->blocks[i].live_in = 0;
        g->blocks[i].live_out = 0;
    }

    /* Iterate until fixed point (backward analysis) */
    int changed = 1;
    while (changed) {
        changed = 0;

        for (int i = g->num_blocks - 1; i >= 0; i--) {
            basic_block_t *b = &g->blocks[i];

            /* live_out = union of live_in of successors */
            uint64_t new_live_out = 0;
            for (int s = 0; s < b->num_succs; s++) {
                new_live_out |= g->blocks[b->succs[s]].live_in;
            }

            /* live_in = use ∪ (live_out - def) */
            uint64_t new_live_in = b->use_vars | (new_live_out & ~b->def_vars);

            if (b->live_in != new_live_in || b->live_out != new_live_out) {
                b->live_in = new_live_in;
                b->live_out = new_live_out;
                changed = 1;
            }
        }
    }
}

/* ============================================================================
 * Test CFG Generation
 * ============================================================================ */

static void generate_cfg(cfg_t *g, uint32_t seed)
{
    uint32_t x = seed;

    /* Generate random CFG with loops and branches */
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->num_blocks = 8 + (x % (CFG_MAX_BLOCKS - 8));
    g->entry = 0;
    g->exit = g->num_blocks - 1;

    /* Initialize blocks */
    for (int i = 0; i < g->num_blocks; i++) {
        g->blocks[i].id = i;
        g->blocks[i].num_succs = 0;
        g->blocks[i].num_preds = 0;
        g->blocks[i].idom = -1;
        g->blocks[i].dom_level = 0;

        /* Generate random def/use sets */
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        g->blocks[i].def_vars = x & ((1ULL << CFG_MAX_VARS) - 1);
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        g->blocks[i].use_vars = x & ((1ULL << CFG_MAX_VARS) - 1);
    }

    /* Add edges (ensure connectivity) */
    for (int i = 0; i < g->num_blocks - 1; i++) {
        /* Add fallthrough edge */
        int succ = i + 1;
        g->blocks[i].succs[g->blocks[i].num_succs++] = succ;
        g->blocks[succ].preds[g->blocks[succ].num_preds++] = i;

        /* Maybe add branch edge */
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        if ((x % 3) == 0 && g->blocks[i].num_succs < 4) {
            int target = (i + 2 + (x % (g->num_blocks - i - 1))) % g->num_blocks;
            if (target > i && g->blocks[target].num_preds < 4) {
                g->blocks[i].succs[g->blocks[i].num_succs++] = target;
                g->blocks[target].preds[g->blocks[target].num_preds++] = i;
            }
        }

        /* Maybe add back edge (loop) */
        if ((x % 5) == 0 && i > 2 && g->blocks[i].num_succs < 4) {
            int target = x % i;
            if (g->blocks[target].num_preds < 4) {
                g->blocks[i].succs[g->blocks[i].num_succs++] = target;
                g->blocks[target].preds[g->blocks[target].num_preds++] = i;
            }
        }
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    memset(&cfg, 0, sizeof(cfg));
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();
    int total_phi = 0;
    int total_live = 0;

    /* Start timing */
    BENCH_START();

    for (int c = 0; c < CFG_NUM_CFGS; c++) {
        /* Generate CFG */
        generate_cfg(&cfg, 0x12345678 + c * 1000);

        /* Compute dominators */
        compute_dominators(&cfg);

        /* Compute dominance frontiers */
        compute_dominance_frontier(&cfg);

        /* Place phi functions */
        int phi_count = place_phi_functions(&cfg);
        total_phi += phi_count;

        /* Compute liveness */
        compute_liveness(&cfg);

        /* Count live variables */
        for (int i = 0; i < cfg.num_blocks; i++) {
            total_live += __builtin_popcountll(cfg.blocks[i].live_in);
            total_live += __builtin_popcountll(cfg.blocks[i].live_out);

            csum = checksum_update(csum, (uint32_t)cfg.blocks[i].idom);
            csum = checksum_update(csum, (uint32_t)(cfg.blocks[i].live_in & 0xFFFFFFFF));
            csum = checksum_update(csum, (uint32_t)cfg.dom_frontier[i]);
        }

        csum = checksum_update(csum, (uint32_t)phi_count);
    }

    /* End timing */
    BENCH_END();

    csum = checksum_update(csum, (uint32_t)total_phi);
    csum = checksum_update(csum, (uint32_t)total_live);

    BENCH_VOLATILE(total_phi);

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
    ssa_dataflow,
    "SSA form and dataflow analysis",
    "403.gcc",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    CFG_NUM_CFGS
);

KERNEL_REGISTER(ssa_dataflow)
