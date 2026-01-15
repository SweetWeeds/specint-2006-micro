/*
 * SPECInt2006-micro: xpath_eval kernel
 * Captures XPath evaluation and DOM traversal from 483.xalancbmk
 *
 * Pattern: Tree traversal, string matching, predicate evaluation
 * Memory: Pointer-heavy tree navigation
 * Branch: Data-dependent (node types, predicate results)
 */

#include "bench.h"

/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef XPATH_NUM_NODES
#define XPATH_NUM_NODES         256     /* Number of DOM nodes */
#endif

#ifndef XPATH_MAX_CHILDREN
#define XPATH_MAX_CHILDREN      8       /* Max children per node */
#endif

#ifndef XPATH_MAX_DEPTH
#define XPATH_MAX_DEPTH         8       /* Max tree depth */
#endif

#ifndef XPATH_NUM_QUERIES
#define XPATH_NUM_QUERIES       20      /* Number of XPath queries */
#endif

#ifndef XPATH_NAME_LEN
#define XPATH_NAME_LEN          16      /* Max node/attribute name length */
#endif

/* Node types */
#define NODE_ELEMENT            1
#define NODE_TEXT               2
#define NODE_ATTRIBUTE          3
#define NODE_COMMENT            4

/* XPath axis types */
#define AXIS_CHILD              0
#define AXIS_DESCENDANT         1
#define AXIS_PARENT             2
#define AXIS_ANCESTOR           3
#define AXIS_FOLLOWING_SIBLING  4
#define AXIS_PRECEDING_SIBLING  5
#define AXIS_SELF               6

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* DOM node */
typedef struct dom_node {
    uint8_t  type;                          /* Node type */
    uint8_t  depth;                         /* Depth in tree */
    int16_t  index;                         /* Index in node array */
    int16_t  parent_idx;                    /* Parent node index (-1 for root) */
    int16_t  first_child_idx;               /* First child index (-1 if none) */
    int16_t  next_sibling_idx;              /* Next sibling index (-1 if none) */
    int16_t  num_children;                  /* Number of children */
    char     name[XPATH_NAME_LEN];          /* Element/attribute name */
    char     value[XPATH_NAME_LEN];         /* Text/attribute value */
    int32_t  int_value;                     /* Numeric value for predicates */
} dom_node_t;

/* XPath step (simplified) */
typedef struct {
    uint8_t  axis;                          /* Axis type */
    char     node_test[XPATH_NAME_LEN];     /* Node name to match ("*" for any) */
    int32_t  predicate_type;                /* 0=none, 1=position, 2=attr_eq */
    int32_t  predicate_value;               /* Value for predicate */
} xpath_step_t;

/* XPath query */
typedef struct {
    xpath_step_t steps[XPATH_MAX_DEPTH];
    int num_steps;
} xpath_query_t;

/* Result set */
typedef struct {
    int16_t nodes[XPATH_NUM_NODES];
    int count;
} node_set_t;

/* DOM tree storage */
static dom_node_t nodes[XPATH_NUM_NODES];
static int num_nodes;
static xpath_query_t queries[XPATH_NUM_QUERIES];

/* ============================================================================
 * String Utilities
 * ============================================================================ */

INLINE int str_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

INLINE int str_match(const char *pattern, const char *str)
{
    /* Simple wildcard match: "*" matches anything */
    if (pattern[0] == '*' && pattern[1] == '\0') {
        return 1;
    }
    return str_equal(pattern, str);
}

static void str_copy(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (i < max_len - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============================================================================
 * DOM Tree Operations
 * ============================================================================ */

static dom_node_t *get_node(int idx)
{
    if (idx >= 0 && idx < num_nodes) {
        return &nodes[idx];
    }
    return NULL;
}

static dom_node_t *get_parent(dom_node_t *node)
{
    return get_node(node->parent_idx);
}

static dom_node_t *get_first_child(dom_node_t *node)
{
    return get_node(node->first_child_idx);
}

static dom_node_t *get_next_sibling(dom_node_t *node)
{
    return get_node(node->next_sibling_idx);
}

/* ============================================================================
 * XPath Axis Navigation
 * ============================================================================ */

/* Get all children of a node */
static void axis_child(dom_node_t *context, node_set_t *result)
{
    dom_node_t *child = get_first_child(context);
    while (child) {
        if (result->count < XPATH_NUM_NODES) {
            result->nodes[result->count++] = child->index;
        }
        child = get_next_sibling(child);
    }
}

/* Get all descendants of a node (DFS) */
static void axis_descendant_helper(dom_node_t *node, node_set_t *result)
{
    dom_node_t *child = get_first_child(node);
    while (child) {
        if (result->count < XPATH_NUM_NODES) {
            result->nodes[result->count++] = child->index;
        }
        axis_descendant_helper(child, result);
        child = get_next_sibling(child);
    }
}

static void axis_descendant(dom_node_t *context, node_set_t *result)
{
    axis_descendant_helper(context, result);
}

/* Get parent */
static void axis_parent(dom_node_t *context, node_set_t *result)
{
    dom_node_t *parent = get_parent(context);
    if (parent && result->count < XPATH_NUM_NODES) {
        result->nodes[result->count++] = parent->index;
    }
}

/* Get all ancestors */
static void axis_ancestor(dom_node_t *context, node_set_t *result)
{
    dom_node_t *ancestor = get_parent(context);
    while (ancestor) {
        if (result->count < XPATH_NUM_NODES) {
            result->nodes[result->count++] = ancestor->index;
        }
        ancestor = get_parent(ancestor);
    }
}

/* Get following siblings */
static void axis_following_sibling(dom_node_t *context, node_set_t *result)
{
    dom_node_t *sibling = get_next_sibling(context);
    while (sibling) {
        if (result->count < XPATH_NUM_NODES) {
            result->nodes[result->count++] = sibling->index;
        }
        sibling = get_next_sibling(sibling);
    }
}

/* Get preceding siblings */
static void axis_preceding_sibling(dom_node_t *context, node_set_t *result)
{
    dom_node_t *parent = get_parent(context);
    if (!parent) return;

    dom_node_t *child = get_first_child(parent);
    while (child && child->index != context->index) {
        if (result->count < XPATH_NUM_NODES) {
            result->nodes[result->count++] = child->index;
        }
        child = get_next_sibling(child);
    }
}

/* Self axis */
static void axis_self(dom_node_t *context, node_set_t *result)
{
    if (result->count < XPATH_NUM_NODES) {
        result->nodes[result->count++] = context->index;
    }
}

/* ============================================================================
 * XPath Evaluation
 * ============================================================================ */

/* Apply axis navigation */
static void apply_axis(uint8_t axis, dom_node_t *context, node_set_t *result)
{
    switch (axis) {
        case AXIS_CHILD:
            axis_child(context, result);
            break;
        case AXIS_DESCENDANT:
            axis_descendant(context, result);
            break;
        case AXIS_PARENT:
            axis_parent(context, result);
            break;
        case AXIS_ANCESTOR:
            axis_ancestor(context, result);
            break;
        case AXIS_FOLLOWING_SIBLING:
            axis_following_sibling(context, result);
            break;
        case AXIS_PRECEDING_SIBLING:
            axis_preceding_sibling(context, result);
            break;
        case AXIS_SELF:
            axis_self(context, result);
            break;
    }
}

/* Apply node test (name matching) */
static void apply_node_test(const char *test, node_set_t *input, node_set_t *output)
{
    for (int i = 0; i < input->count; i++) {
        dom_node_t *node = get_node(input->nodes[i]);
        if (node && str_match(test, node->name)) {
            if (output->count < XPATH_NUM_NODES) {
                output->nodes[output->count++] = node->index;
            }
        }
    }
}

/* Apply predicate */
static void apply_predicate(const xpath_step_t *step, node_set_t *input, node_set_t *output)
{
    if (step->predicate_type == 0) {
        /* No predicate - copy all */
        for (int i = 0; i < input->count; i++) {
            if (output->count < XPATH_NUM_NODES) {
                output->nodes[output->count++] = input->nodes[i];
            }
        }
    } else if (step->predicate_type == 1) {
        /* Position predicate [n] */
        int pos = step->predicate_value - 1;  /* XPath is 1-indexed */
        if (pos >= 0 && pos < input->count) {
            output->nodes[output->count++] = input->nodes[pos];
        }
    } else if (step->predicate_type == 2) {
        /* Attribute value predicate [@attr = value] */
        for (int i = 0; i < input->count; i++) {
            dom_node_t *node = get_node(input->nodes[i]);
            if (node && node->int_value == step->predicate_value) {
                if (output->count < XPATH_NUM_NODES) {
                    output->nodes[output->count++] = node->index;
                }
            }
        }
    }
}

/* Evaluate single XPath step */
static void eval_step(const xpath_step_t *step, node_set_t *context_nodes, node_set_t *result)
{
    node_set_t axis_result = {.count = 0};
    node_set_t test_result = {.count = 0};

    /* Apply axis to all context nodes */
    for (int i = 0; i < context_nodes->count; i++) {
        dom_node_t *ctx = get_node(context_nodes->nodes[i]);
        if (ctx) {
            apply_axis(step->axis, ctx, &axis_result);
        }
    }

    /* Apply node test */
    apply_node_test(step->node_test, &axis_result, &test_result);

    /* Apply predicate */
    apply_predicate(step, &test_result, result);
}

/* Evaluate complete XPath query */
static int eval_xpath(const xpath_query_t *query, int start_node_idx, node_set_t *result)
{
    node_set_t current = {.count = 0};
    node_set_t next = {.count = 0};

    /* Start with root or specified node */
    current.nodes[0] = start_node_idx;
    current.count = 1;

    /* Evaluate each step */
    for (int s = 0; s < query->num_steps; s++) {
        next.count = 0;
        eval_step(&query->steps[s], &current, &next);

        /* Swap current and next */
        current = next;

        if (current.count == 0) {
            break;  /* No more nodes to process */
        }
    }

    /* Copy to result */
    *result = current;
    return current.count;
}

/* ============================================================================
 * DOM Tree Generation
 * ============================================================================ */

static void generate_tree(uint32_t seed)
{
    uint32_t x = seed;
    num_nodes = 0;

    /* Node name templates */
    static const char *element_names[] = {
        "root", "item", "data", "node", "elem", "child", "leaf", "entry"
    };
    static const int num_element_names = 8;

    /* Create root node */
    dom_node_t *root = &nodes[num_nodes];
    root->type = NODE_ELEMENT;
    root->depth = 0;
    root->index = num_nodes;
    root->parent_idx = -1;
    root->first_child_idx = -1;
    root->next_sibling_idx = -1;
    root->num_children = 0;
    str_copy(root->name, "root", XPATH_NAME_LEN);
    root->value[0] = '\0';
    root->int_value = 0;
    num_nodes++;

    /* Build tree using BFS-like approach */
    int queue[XPATH_NUM_NODES];
    int head = 0, tail = 0;

    queue[tail++] = 0;  /* Start with root */

    while (head < tail && num_nodes < XPATH_NUM_NODES) {
        int parent_idx = queue[head++];
        dom_node_t *parent = &nodes[parent_idx];

        if (parent->depth >= XPATH_MAX_DEPTH - 1) {
            continue;  /* Max depth reached */
        }

        /* Determine number of children */
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int num_children = 1 + (x % XPATH_MAX_CHILDREN);
        if (num_nodes + num_children > XPATH_NUM_NODES) {
            num_children = XPATH_NUM_NODES - num_nodes;
        }

        int prev_child_idx = -1;
        for (int c = 0; c < num_children && num_nodes < XPATH_NUM_NODES; c++) {
            dom_node_t *child = &nodes[num_nodes];

            /* Determine node type */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            int type_choice = x % 10;
            if (type_choice < 7) {
                child->type = NODE_ELEMENT;
            } else if (type_choice < 9) {
                child->type = NODE_TEXT;
            } else {
                child->type = NODE_ATTRIBUTE;
            }

            child->depth = parent->depth + 1;
            child->index = num_nodes;
            child->parent_idx = parent_idx;
            child->first_child_idx = -1;
            child->next_sibling_idx = -1;
            child->num_children = 0;

            /* Set name */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            int name_idx = x % num_element_names;
            str_copy(child->name, element_names[name_idx], XPATH_NAME_LEN);

            /* Set value */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            child->int_value = x % 100;

            /* Link to parent */
            if (c == 0) {
                parent->first_child_idx = num_nodes;
            }
            if (prev_child_idx >= 0) {
                nodes[prev_child_idx].next_sibling_idx = num_nodes;
            }
            prev_child_idx = num_nodes;
            parent->num_children++;

            /* Add element nodes to queue for further expansion */
            if (child->type == NODE_ELEMENT && tail < XPATH_NUM_NODES) {
                queue[tail++] = num_nodes;
            }

            num_nodes++;
        }
    }
}

static void generate_queries(uint32_t seed)
{
    uint32_t x = seed;

    static const char *test_names[] = {"*", "item", "data", "node", "elem", "child"};
    static const int num_test_names = 6;

    for (int q = 0; q < XPATH_NUM_QUERIES; q++) {
        xpath_query_t *query = &queries[q];

        /* Determine number of steps */
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        query->num_steps = 1 + (x % (XPATH_MAX_DEPTH - 1));

        for (int s = 0; s < query->num_steps; s++) {
            xpath_step_t *step = &query->steps[s];

            /* Choose axis */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            step->axis = x % 7;

            /* Choose node test */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            int test_idx = x % num_test_names;
            str_copy(step->node_test, test_names[test_idx], XPATH_NAME_LEN);

            /* Choose predicate */
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            step->predicate_type = x % 3;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            step->predicate_value = 1 + (x % 10);
        }
    }
}

/* ============================================================================
 * Additional Tree Analysis
 * ============================================================================ */

/* Count nodes matching a name */
static int count_by_name(const char *name)
{
    int count = 0;
    for (int i = 0; i < num_nodes; i++) {
        if (str_match(name, nodes[i].name)) {
            count++;
        }
    }
    return count;
}

/* Calculate total tree depth */
static int max_depth(void)
{
    int max_d = 0;
    for (int i = 0; i < num_nodes; i++) {
        if (nodes[i].depth > max_d) {
            max_d = nodes[i].depth;
        }
    }
    return max_d;
}

/* Sum of all int_values */
static int32_t sum_values(void)
{
    int32_t sum = 0;
    for (int i = 0; i < num_nodes; i++) {
        sum += nodes[i].int_value;
    }
    return sum;
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    generate_tree(0xBADCAFE0);
    generate_queries(0xDEADC0DE);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();

    int total_results = 0;
    int total_steps = 0;

    BENCH_START();

    /* Execute XPath queries */
    for (int q = 0; q < XPATH_NUM_QUERIES; q++) {
        node_set_t result_set = {.count = 0};
        int count = eval_xpath(&queries[q], 0, &result_set);

        total_results += count;
        total_steps += queries[q].num_steps;
        csum = checksum_update(csum, (uint32_t)count);

        /* Checksum result node indices */
        for (int i = 0; i < result_set.count && i < 10; i++) {
            csum = checksum_update(csum, (uint32_t)result_set.nodes[i]);
        }
    }

    /* Tree statistics */
    int depth = max_depth();
    csum = checksum_update(csum, (uint32_t)depth);

    int32_t value_sum = sum_values();
    csum = checksum_update(csum, (uint32_t)value_sum);

    int item_count = count_by_name("item");
    csum = checksum_update(csum, (uint32_t)item_count);

    int data_count = count_by_name("data");
    csum = checksum_update(csum, (uint32_t)data_count);

    /* Descendant count from root */
    node_set_t descendants = {.count = 0};
    axis_descendant(&nodes[0], &descendants);
    csum = checksum_update(csum, (uint32_t)descendants.count);

    BENCH_END();

    /* Final statistics checksum */
    csum = checksum_update(csum, (uint32_t)total_results);
    csum = checksum_update(csum, (uint32_t)total_steps);
    csum = checksum_update(csum, (uint32_t)num_nodes);

    BENCH_VOLATILE(total_results);
    BENCH_VOLATILE(total_steps);
    BENCH_VOLATILE(depth);
    BENCH_VOLATILE(value_sum);

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
    xpath_eval,
    "XPath query evaluation on DOM tree",
    "483.xalancbmk",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    XPATH_NUM_QUERIES
);

KERNEL_REGISTER(xpath_eval)
