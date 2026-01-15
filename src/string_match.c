/*
 * SPECInt2006-micro: string_match kernel
 * Captures regex/pattern matching from 400.perlbench
 *
 * Pattern: String searching with backtracking
 * Memory: Sequential text scanning
 * Branch: Data-dependent pattern decisions
 */

#include "bench.h"


/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef TEXT_SIZE
#define TEXT_SIZE           1024
#endif

#ifndef NUM_PATTERNS
#define NUM_PATTERNS        10
#endif

#ifndef PATTERN_MAX_LEN
#define PATTERN_MAX_LEN     16
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

typedef struct {
    char pattern[PATTERN_MAX_LEN];
    int len;
} pattern_t;

static char text[TEXT_SIZE];
static pattern_t patterns[NUM_PATTERNS];

/* ============================================================================
 * Simple Pattern Matcher (KMP-style with wildcards)
 * ============================================================================ */

/* Build failure function for KMP */
static void build_failure(const char *pattern, int len, int *failure)
{
    failure[0] = -1;
    int k = -1;

    for (int i = 1; i < len; i++) {
        while (k >= 0 && pattern[k + 1] != pattern[i]) {
            k = failure[k];
        }
        if (pattern[k + 1] == pattern[i]) {
            k++;
        }
        failure[i] = k;
    }
}

/* KMP search - returns number of matches */
static int kmp_search(const char *text, int text_len,
                     const char *pattern, int pattern_len)
{
    if (pattern_len == 0) return 0;

    int failure[PATTERN_MAX_LEN];
    build_failure(pattern, pattern_len, failure);

    int matches = 0;
    int j = -1;

    for (int i = 0; i < text_len; i++) {
        while (j >= 0 && pattern[j + 1] != text[i]) {
            j = failure[j];
        }
        if (pattern[j + 1] == text[i]) {
            j++;
        }
        if (j == pattern_len - 1) {
            matches++;
            j = failure[j];
        }
    }

    return matches;
}

/* Boyer-Moore-Horspool search */
static int bmh_search(const char *text, int text_len,
                     const char *pattern, int pattern_len)
{
    if (pattern_len == 0 || pattern_len > text_len) return 0;

    /* Build bad character table */
    int skip[256];
    for (int i = 0; i < 256; i++) {
        skip[i] = pattern_len;
    }
    for (int i = 0; i < pattern_len - 1; i++) {
        skip[(uint8_t)pattern[i]] = pattern_len - 1 - i;
    }

    int matches = 0;
    int i = pattern_len - 1;

    while (i < text_len) {
        int j = pattern_len - 1;
        int k = i;

        while (j >= 0 && text[k] == pattern[j]) {
            j--;
            k--;
        }

        if (j < 0) {
            matches++;
            i += pattern_len;
        } else {
            i += skip[(uint8_t)text[i]];
        }
    }

    return matches;
}

/* Simple wildcard match (? matches any single char) */
__attribute__((unused))
static bool wildcard_match(const char *text, const char *pattern)
{
    while (*pattern) {
        if (*pattern == '?') {
            if (!*text) return false;
            text++;
            pattern++;
        } else if (*pattern == '*') {
            /* * matches zero or more chars */
            pattern++;
            if (!*pattern) return true;
            while (*text) {
                if (wildcard_match(text, pattern)) return true;
                text++;
            }
            return false;
        } else {
            if (*text != *pattern) return false;
            text++;
            pattern++;
        }
    }
    return *text == '\0';
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_text(char *text, int size, uint32_t seed)
{
    uint32_t x = seed;
    const char *words[] = {"the", "quick", "brown", "fox", "jumps", "over",
                          "lazy", "dog", "hello", "world", "test", "data"};
    int num_words = 12;

    int pos = 0;
    while (pos < size - 1) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const char *word = words[x % num_words];

        while (*word && pos < size - 1) {
            text[pos++] = *word++;
        }

        if (pos < size - 1) {
            text[pos++] = (x % 5 == 0) ? '\n' : ' ';
        }
    }
    text[pos] = '\0';
}

static void generate_patterns(pattern_t *patterns, int count, const char *text, uint32_t seed)
{
    uint32_t x = seed;

    for (int i = 0; i < count; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;

        /* Extract substring from text as pattern */
        int text_len = strlen(text);
        int start = x % (text_len - 8);
        int len = 3 + (x % 6);

        for (int j = 0; j < len && j < PATTERN_MAX_LEN - 1; j++) {
            patterns[i].pattern[j] = text[start + j];
        }
        patterns[i].pattern[len] = '\0';
        patterns[i].len = len;
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    generate_text(text, TEXT_SIZE, 0x12345678);
    generate_patterns(patterns, NUM_PATTERNS, text, 0xABCDEF00);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();
    int total_matches = 0;

    int text_len = strlen(text);

    /* Start timing */
    BENCH_START();

    /* Run both KMP and BMH for each pattern */
    for (int i = 0; i < NUM_PATTERNS; i++) {
        int kmp_count = kmp_search(text, text_len,
                                   patterns[i].pattern, patterns[i].len);
        int bmh_count = bmh_search(text, text_len,
                                   patterns[i].pattern, patterns[i].len);

        total_matches += kmp_count + bmh_count;
        csum = checksum_update(csum, (uint32_t)kmp_count);
        csum = checksum_update(csum, (uint32_t)bmh_count);
    }

    /* End timing */
    BENCH_END();

    csum = checksum_update(csum, (uint32_t)total_matches);

    /* Prevent optimization */
    BENCH_VOLATILE(total_matches);

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
    string_match,
    "String pattern matching",
    "400.perlbench",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    NUM_PATTERNS
);

KERNEL_REGISTER(string_match)
