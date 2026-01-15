/*
 * SPECInt2006-micro: tree_walk kernel
 * Captures AST traversal patterns from 403.gcc
 *
 * Pattern: Recursive tree traversal with node processing
 * Memory: Random pointer chasing through tree nodes
 * Branch: Data-dependent traversal decisions
 */

#include "bench.h"


/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef TREE_NUM_NODES
#define TREE_NUM_NODES      256     /* Total nodes in tree */
#endif

#ifndef TREE_MAX_DEPTH
#define TREE_MAX_DEPTH      10      /* Maximum tree depth */
#endif

/* Node types (like GCC's tree codes) */
#define NODE_INTEGER        1
#define NODE_PLUS           2
#define NODE_MINUS          3
#define NODE_MULT           4
#define NODE_DIV            5
#define NODE_VAR            6
#define NODE_IF             7
#define NODE_BLOCK          8

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Tree node (similar to GCC's tree structure) */
typedef struct tree_node {
    uint8_t type;               /* Node type */
    uint8_t flags;              /* Node flags */
    int16_t value;              /* Integer value or variable index */
    struct tree_node *left;     /* Left child / condition */
    struct tree_node *right;    /* Right child / then branch */
    struct tree_node *next;     /* Next sibling / else branch */
} tree_node_t;

/* Static storage */
static tree_node_t nodes[TREE_NUM_NODES];
static int32_t variables[16];       /* Variable values */
static int nodes_used;

/* ============================================================================
 * Tree Construction
 * ============================================================================ */

static tree_node_t *alloc_node(uint8_t type)
{
    if (nodes_used >= TREE_NUM_NODES) {
        return NULL;
    }
    tree_node_t *node = &nodes[nodes_used++];
    node->type = type;
    node->flags = 0;
    node->value = 0;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    return node;
}

/* Build a random expression tree */
static tree_node_t *build_expr(uint32_t *seed, int depth)
{
    uint32_t x = *seed;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *seed = x;

    if (depth >= TREE_MAX_DEPTH - 2 || nodes_used >= TREE_NUM_NODES - 4) {
        /* Leaf node: integer or variable */
        tree_node_t *node = alloc_node((x % 2) ? NODE_INTEGER : NODE_VAR);
        if (node) {
            if (node->type == NODE_INTEGER) {
                node->value = (x % 200) - 100;
            } else {
                node->value = x % 16;  /* Variable index */
            }
        }
        return node;
    }

    /* Internal node: operator */
    int op = x % 6;
    tree_node_t *node = NULL;

    switch (op) {
        case 0:
        case 1:
            node = alloc_node(NODE_PLUS);
            break;
        case 2:
            node = alloc_node(NODE_MINUS);
            break;
        case 3:
            node = alloc_node(NODE_MULT);
            break;
        case 4:
            node = alloc_node(NODE_DIV);
            break;
        case 5:
            /* Conditional expression */
            node = alloc_node(NODE_IF);
            if (node) {
                node->left = build_expr(seed, depth + 1);   /* Condition */
                node->right = build_expr(seed, depth + 1);  /* Then */
                node->next = build_expr(seed, depth + 1);   /* Else */
            }
            return node;
    }

    if (node) {
        node->left = build_expr(seed, depth + 1);
        node->right = build_expr(seed, depth + 1);
    }

    return node;
}

/* Build a block with multiple statements */
static tree_node_t *build_block(uint32_t *seed, int num_statements, int depth)
{
    tree_node_t *block = alloc_node(NODE_BLOCK);
    if (!block || depth >= TREE_MAX_DEPTH) {
        return block;
    }

    tree_node_t *prev = NULL;
    for (int i = 0; i < num_statements && nodes_used < TREE_NUM_NODES - 10; i++) {
        tree_node_t *stmt = build_expr(seed, depth + 1);
        if (stmt) {
            if (prev) {
                prev->next = stmt;
            } else {
                block->left = stmt;
            }
            prev = stmt;
        }
    }

    return block;
}

/* ============================================================================
 * Tree Evaluation (like GCC's fold_const)
 * ============================================================================ */

/* Evaluate expression tree */
static int32_t eval_tree(const tree_node_t *node)
{
    if (!node) {
        return 0;
    }

    switch (node->type) {
        case NODE_INTEGER:
            return node->value;

        case NODE_VAR:
            return variables[node->value & 15];

        case NODE_PLUS:
            return eval_tree(node->left) + eval_tree(node->right);

        case NODE_MINUS:
            return eval_tree(node->left) - eval_tree(node->right);

        case NODE_MULT:
            return eval_tree(node->left) * eval_tree(node->right);

        case NODE_DIV: {
            int32_t r = eval_tree(node->right);
            if (r == 0) return 0;
            return eval_tree(node->left) / r;
        }

        case NODE_IF:
            if (eval_tree(node->left) != 0) {
                return eval_tree(node->right);
            } else {
                return eval_tree(node->next);
            }

        case NODE_BLOCK: {
            int32_t result = 0;
            const tree_node_t *stmt = node->left;
            while (stmt) {
                result = eval_tree(stmt);
                stmt = stmt->next;
            }
            return result;
        }

        default:
            return 0;
    }
}

/* ============================================================================
 * Tree Traversal and Analysis
 * ============================================================================ */

/* Count nodes by type */
static void count_nodes(const tree_node_t *node, int *counts)
{
    if (!node) return;

    counts[node->type]++;

    count_nodes(node->left, counts);
    count_nodes(node->right, counts);
    count_nodes(node->next, counts);
}

/* Compute tree depth */
static int tree_depth(const tree_node_t *node)
{
    if (!node) return 0;

    int left_depth = tree_depth(node->left);
    int right_depth = tree_depth(node->right);
    int next_depth = tree_depth(node->next);

    int max = left_depth;
    if (right_depth > max) max = right_depth;
    if (next_depth > max) max = next_depth;

    return max + 1;
}

/* Check if tree contains only constants (constant folding candidate) */
static bool is_constant(const tree_node_t *node)
{
    if (!node) return true;

    switch (node->type) {
        case NODE_INTEGER:
            return true;
        case NODE_VAR:
            return false;
        case NODE_IF:
            return is_constant(node->left) &&
                   is_constant(node->right) &&
                   is_constant(node->next);
        default:
            return is_constant(node->left) && is_constant(node->right);
    }
}

/* Constant folding optimization (like GCC's fold) */
static tree_node_t *fold_constants(tree_node_t *node)
{
    if (!node) return NULL;

    /* Recurse on children first */
    node->left = fold_constants(node->left);
    node->right = fold_constants(node->right);
    node->next = fold_constants(node->next);

    /* Check if this subtree is constant */
    if (node->type != NODE_INTEGER && node->type != NODE_VAR &&
        node->type != NODE_BLOCK && is_constant(node)) {
        /* Evaluate and replace with constant */
        int32_t value = eval_tree(node);
        node->type = NODE_INTEGER;
        node->value = (int16_t)value;
        node->left = NULL;
        node->right = NULL;
        node->next = NULL;
    }

    return node;
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static tree_node_t *root;

static void kernel_init_func(void)
{
    /* Initialize variables */
    uint32_t seed = 0xABCDEF12;
    for (int i = 0; i < 16; i++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        variables[i] = (seed % 100) - 50;
    }

    /* Build tree */
    nodes_used = 0;
    seed = 0x12345678;
    root = build_block(&seed, 8, 0);  /* 8 statements in block */
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();

    /* Start timing */
    BENCH_START();

    /* Evaluate tree */
    int32_t eval_result = eval_tree(root);
    csum = checksum_update(csum, (uint32_t)eval_result);

    /* Count nodes */
    int counts[16] = {0};
    count_nodes(root, counts);
    for (int i = 0; i < 16; i++) {
        csum = checksum_update(csum, (uint32_t)counts[i]);
    }

    /* Compute depth */
    int depth = tree_depth(root);
    csum = checksum_update(csum, (uint32_t)depth);

    /* Fold constants and re-evaluate */
    fold_constants(root);
    int32_t folded_result = eval_tree(root);
    csum = checksum_update(csum, (uint32_t)folded_result);

    /* End timing */
    BENCH_END();

    /* Prevent optimization */
    BENCH_VOLATILE(folded_result);

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
    tree_walk,
    "AST tree traversal",
    "403.gcc",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    1
);

KERNEL_REGISTER(tree_walk)
