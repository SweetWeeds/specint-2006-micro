/*
 * SPECInt2006-micro: regex_compile kernel
 * Captures regex compilation patterns from 400.perlbench
 *
 * Pattern: NFA construction from regular expression
 * Memory: Dynamic state allocation, transition tables
 * Branch: Pattern-dependent control flow
 */

#include "bench.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef REGEX_MAX_STATES
#define REGEX_MAX_STATES    128
#endif

#ifndef REGEX_MAX_TRANS
#define REGEX_MAX_TRANS     256
#endif

#ifndef REGEX_NUM_PATTERNS
#define REGEX_NUM_PATTERNS  20
#endif

#ifndef REGEX_MAX_PATTERN_LEN
#define REGEX_MAX_PATTERN_LEN 32
#endif

/* ============================================================================
 * Data Structures (NFA representation)
 * ============================================================================ */

/* Transition types */
#define TRANS_EPSILON   0
#define TRANS_CHAR      1
#define TRANS_CHARCLASS 2
#define TRANS_ANY       3

typedef struct {
    uint8_t type;
    uint8_t ch;           /* Character to match */
    uint8_t char_class;   /* Character class ID */
    int16_t next_state;   /* Target state */
} nfa_trans_t;

typedef struct {
    int16_t num_trans;
    int16_t trans_start;  /* Index into transitions array */
    uint8_t is_accept;
} nfa_state_t;

typedef struct {
    nfa_state_t states[REGEX_MAX_STATES];
    nfa_trans_t transitions[REGEX_MAX_TRANS];
    int16_t num_states;
    int16_t num_trans;
    int16_t start_state;
    uint32_t char_classes[8];  /* Bitmask for character classes */
} nfa_t;

/* Static storage */
static nfa_t nfa;
static char patterns[REGEX_NUM_PATTERNS][REGEX_MAX_PATTERN_LEN];
static int pattern_lengths[REGEX_NUM_PATTERNS];

/* ============================================================================
 * NFA Construction (Thompson's construction)
 * ============================================================================ */

static int16_t nfa_add_state(nfa_t *n, uint8_t is_accept)
{
    if (n->num_states >= REGEX_MAX_STATES) return -1;
    int16_t id = n->num_states++;
    n->states[id].num_trans = 0;
    n->states[id].trans_start = n->num_trans;
    n->states[id].is_accept = is_accept;
    return id;
}

static int nfa_add_trans(nfa_t *n, int16_t from, uint8_t type, uint8_t ch, int16_t to)
{
    if (n->num_trans >= REGEX_MAX_TRANS) return -1;
    int16_t idx = n->num_trans++;
    n->transitions[idx].type = type;
    n->transitions[idx].ch = ch;
    n->transitions[idx].char_class = 0;
    n->transitions[idx].next_state = to;
    n->states[from].num_trans++;
    return 0;
}

/* Parse character class like [a-z] or [0-9] */
static int parse_char_class(const char *pattern, int *pos, int len, uint32_t *class_mask)
{
    *class_mask = 0;
    (*pos)++;  /* Skip '[' */

    int negate = 0;
    if (*pos < len && pattern[*pos] == '^') {
        negate = 1;
        (*pos)++;
    }

    while (*pos < len && pattern[*pos] != ']') {
        char c = pattern[*pos];
        if (*pos + 2 < len && pattern[*pos + 1] == '-') {
            /* Range like a-z */
            char start = c;
            char end = pattern[*pos + 2];
            for (char ch = start; ch <= end; ch++) {
                *class_mask |= (1U << (ch & 31));
            }
            *pos += 3;
        } else {
            *class_mask |= (1U << (c & 31));
            (*pos)++;
        }
    }

    if (*pos < len) (*pos)++;  /* Skip ']' */

    if (negate) *class_mask = ~(*class_mask);
    return 0;
}

/* Compile regex pattern to NFA using Thompson's construction */
static int regex_compile_pattern(nfa_t *n, const char *pattern, int len)
{
    n->num_states = 0;
    n->num_trans = 0;

    int16_t start = nfa_add_state(n, 0);
    n->start_state = start;

    int16_t current = start;
    int pos = 0;
    int class_idx = 0;

    while (pos < len) {
        char c = pattern[pos];
        int16_t next_state;

        switch (c) {
        case '.':
            /* Match any character */
            next_state = nfa_add_state(n, 0);
            nfa_add_trans(n, current, TRANS_ANY, 0, next_state);
            current = next_state;
            pos++;
            break;

        case '*':
            /* Kleene star - add epsilon loops */
            if (current != start) {
                nfa_add_trans(n, current, TRANS_EPSILON, 0, current - 1);
                nfa_add_trans(n, current - 1, TRANS_EPSILON, 0, current);
            }
            pos++;
            break;

        case '+':
            /* One or more - add epsilon back loop */
            if (current != start) {
                nfa_add_trans(n, current, TRANS_EPSILON, 0, current - 1);
            }
            pos++;
            break;

        case '?':
            /* Zero or one - add epsilon bypass */
            if (current != start) {
                nfa_add_trans(n, current - 1, TRANS_EPSILON, 0, current);
            }
            pos++;
            break;

        case '[':
            /* Character class */
            next_state = nfa_add_state(n, 0);
            if (class_idx < 8) {
                parse_char_class(pattern, &pos, len, &n->char_classes[class_idx]);
                n->transitions[n->num_trans - 1].char_class = class_idx++;
            }
            nfa_add_trans(n, current, TRANS_CHARCLASS, 0, next_state);
            current = next_state;
            break;

        case '|':
            /* Alternation - simplified handling */
            next_state = nfa_add_state(n, 0);
            nfa_add_trans(n, start, TRANS_EPSILON, 0, next_state);
            current = next_state;
            pos++;
            break;

        case '(':
        case ')':
            /* Grouping - simplified */
            pos++;
            break;

        case '\\':
            /* Escape sequence */
            pos++;
            if (pos < len) {
                next_state = nfa_add_state(n, 0);
                char escaped = pattern[pos];
                if (escaped == 'd') {
                    /* Digit class */
                    nfa_add_trans(n, current, TRANS_CHARCLASS, 0, next_state);
                } else if (escaped == 'w') {
                    /* Word character */
                    nfa_add_trans(n, current, TRANS_CHARCLASS, 1, next_state);
                } else if (escaped == 's') {
                    /* Whitespace */
                    nfa_add_trans(n, current, TRANS_CHARCLASS, 2, next_state);
                } else {
                    nfa_add_trans(n, current, TRANS_CHAR, escaped, next_state);
                }
                current = next_state;
                pos++;
            }
            break;

        default:
            /* Literal character */
            next_state = nfa_add_state(n, 0);
            nfa_add_trans(n, current, TRANS_CHAR, c, next_state);
            current = next_state;
            pos++;
            break;
        }
    }

    /* Mark final state as accepting */
    n->states[current].is_accept = 1;

    return 0;
}

/* ============================================================================
 * NFA Simulation (for verification)
 * ============================================================================ */

static int nfa_match(const nfa_t *n, const char *text, int text_len)
{
    /* Epsilon closure using state set */
    uint8_t current_states[REGEX_MAX_STATES / 8 + 1];
    uint8_t next_states[REGEX_MAX_STATES / 8 + 1];

    memset(current_states, 0, sizeof(current_states));
    current_states[n->start_state / 8] |= (1 << (n->start_state % 8));

    /* Add epsilon closure of start state */
    for (int i = 0; i < n->num_states; i++) {
        if (!(current_states[i / 8] & (1 << (i % 8)))) continue;

        int trans_start = n->states[i].trans_start;
        int trans_end = trans_start + n->states[i].num_trans;
        for (int t = trans_start; t < trans_end && t < n->num_trans; t++) {
            if (n->transitions[t].type == TRANS_EPSILON) {
                int16_t next = n->transitions[t].next_state;
                if (next >= 0 && next < n->num_states) {
                    current_states[next / 8] |= (1 << (next % 8));
                }
            }
        }
    }

    /* Process each character */
    for (int pos = 0; pos < text_len; pos++) {
        char c = text[pos];
        memset(next_states, 0, sizeof(next_states));

        for (int i = 0; i < n->num_states; i++) {
            if (!(current_states[i / 8] & (1 << (i % 8)))) continue;

            int trans_start = n->states[i].trans_start;
            int trans_end = trans_start + n->states[i].num_trans;
            for (int t = trans_start; t < trans_end && t < n->num_trans; t++) {
                const nfa_trans_t *tr = &n->transitions[t];
                int match = 0;

                switch (tr->type) {
                case TRANS_CHAR:
                    match = (tr->ch == c);
                    break;
                case TRANS_ANY:
                    match = (c != '\n');
                    break;
                case TRANS_CHARCLASS:
                    match = (n->char_classes[tr->char_class] & (1U << (c & 31))) != 0;
                    break;
                }

                if (match && tr->next_state >= 0 && tr->next_state < n->num_states) {
                    next_states[tr->next_state / 8] |= (1 << (tr->next_state % 8));
                }
            }
        }

        memcpy(current_states, next_states, sizeof(current_states));
    }

    /* Check if any accepting state is active */
    for (int i = 0; i < n->num_states; i++) {
        if ((current_states[i / 8] & (1 << (i % 8))) && n->states[i].is_accept) {
            return 1;
        }
    }

    return 0;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_patterns(uint32_t seed)
{
    /* Generate various regex patterns */
    const char *templates[] = {
        "a+b*c",
        "[a-z]+",
        "\\d+\\.\\d+",
        "foo|bar|baz",
        "[A-Za-z_][A-Za-z0-9_]*",
        ".*pattern.*",
        "(ab)+c?",
        "[0-9]{2,4}",
        "\\w+@\\w+",
        "^start.*end$",
        "[^aeiou]+",
        "a.b.c",
        "(a|b)*abb",
        "[a-f0-9]+",
        "test\\d+",
        "x+y+z+",
        "[abc][def]",
        "\\s+\\w+\\s+",
        "a?b?c?",
        ".*"
    };

    uint32_t x = seed;
    for (int i = 0; i < REGEX_NUM_PATTERNS; i++) {
        const char *tmpl = templates[i % 20];
        int len = strlen(tmpl);
        if (len >= REGEX_MAX_PATTERN_LEN) len = REGEX_MAX_PATTERN_LEN - 1;
        memcpy(patterns[i], tmpl, len);
        patterns[i][len] = '\0';
        pattern_lengths[i] = len;

        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    generate_patterns(0x12345678);
    memset(&nfa, 0, sizeof(nfa));
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();
    int total_states = 0;
    int total_trans = 0;

    /* Start timing */
    BENCH_START();

    /* Compile all patterns */
    for (int i = 0; i < REGEX_NUM_PATTERNS; i++) {
        regex_compile_pattern(&nfa, patterns[i], pattern_lengths[i]);

        total_states += nfa.num_states;
        total_trans += nfa.num_trans;

        csum = checksum_update(csum, (uint32_t)nfa.num_states);
        csum = checksum_update(csum, (uint32_t)nfa.num_trans);

        /* Test match on sample text */
        const char *test_text = "abctest123foo";
        int matched = nfa_match(&nfa, test_text, strlen(test_text));
        csum = checksum_update(csum, (uint32_t)matched);
    }

    /* End timing */
    BENCH_END();

    csum = checksum_update(csum, (uint32_t)total_states);
    csum = checksum_update(csum, (uint32_t)total_trans);

    BENCH_VOLATILE(total_states);

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
    regex_compile,
    "Regex NFA compilation",
    "400.perlbench",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    REGEX_NUM_PATTERNS
);

KERNEL_REGISTER(regex_compile)
