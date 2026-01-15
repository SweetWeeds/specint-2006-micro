/*
 * SPECInt2006-micro: hash_lookup kernel
 * Captures hash table lookup patterns from 400.perlbench
 *
 * Pattern: Chained hash table with string keys (similar to Perl's HV)
 * Memory: Random pointer chasing
 * Branch: Data-dependent (hash collision handling)
 */

#include "bench.h"


/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef HASH_NUM_BUCKETS
#define HASH_NUM_BUCKETS    256     /* Power of 2 for fast modulo */
#endif

#ifndef HASH_NUM_ENTRIES
#define HASH_NUM_ENTRIES    512     /* Total entries in table */
#endif

#ifndef HASH_NUM_LOOKUPS
#define HASH_NUM_LOOKUPS    100     /* Number of lookups per run */
#endif

#ifndef HASH_KEY_LEN
#define HASH_KEY_LEN        16      /* Average key length */
#endif

/* ============================================================================
 * Data Structures (similar to Perl's hash implementation)
 * ============================================================================ */

/* Hash entry (like Perl's HE) */
typedef struct hash_entry {
    struct hash_entry *next;    /* Next in chain */
    uint32_t hash;              /* Cached hash value */
    uint16_t key_len;           /* Key length */
    uint16_t flags;             /* Entry flags */
    int32_t value;              /* Stored value */
    char key[HASH_KEY_LEN];     /* Key data (fixed size for simplicity) */
} hash_entry_t;

/* Hash table (like Perl's HV) */
typedef struct {
    hash_entry_t **buckets;     /* Bucket array */
    uint32_t num_buckets;       /* Number of buckets */
    uint32_t mask;              /* Mask for bucket index (num_buckets - 1) */
    uint32_t num_entries;       /* Number of entries */
} hash_table_t;

/* Static storage */
static hash_table_t table;
static hash_entry_t *buckets[HASH_NUM_BUCKETS];
static hash_entry_t entries[HASH_NUM_ENTRIES];
static char lookup_keys[HASH_NUM_LOOKUPS][HASH_KEY_LEN];

/* ============================================================================
 * Hash Function (DJB2 - used in many hash table implementations)
 * ============================================================================ */

static uint32_t djb2_hash(const char *key, size_t len)
{
    uint32_t hash = 5381;

    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (uint8_t)key[i];  /* hash * 33 + c */
    }

    return hash;
}

/* ============================================================================
 * Hash Table Operations
 * ============================================================================ */

/* Initialize hash table */
static void hash_init(hash_table_t *ht, hash_entry_t **bucket_array, uint32_t num_buckets)
{
    ht->buckets = bucket_array;
    ht->num_buckets = num_buckets;
    ht->mask = num_buckets - 1;
    ht->num_entries = 0;

    /* Clear buckets */
    for (uint32_t i = 0; i < num_buckets; i++) {
        bucket_array[i] = NULL;
    }
}

/* Insert entry into hash table */
static void hash_insert(hash_table_t *ht, hash_entry_t *entry, const char *key, size_t key_len, int32_t value)
{
    /* Compute hash */
    uint32_t hash = djb2_hash(key, key_len);
    uint32_t bucket_idx = hash & ht->mask;

    /* Set up entry */
    entry->hash = hash;
    entry->key_len = key_len;
    entry->flags = 0;
    entry->value = value;
    memcpy(entry->key, key, key_len < HASH_KEY_LEN ? key_len : HASH_KEY_LEN);

    /* Insert at head of chain */
    entry->next = ht->buckets[bucket_idx];
    ht->buckets[bucket_idx] = entry;
    ht->num_entries++;
}

/* Lookup entry in hash table */
static hash_entry_t *hash_lookup(hash_table_t *ht, const char *key, size_t key_len)
{
    /* Compute hash */
    uint32_t hash = djb2_hash(key, key_len);
    uint32_t bucket_idx = hash & ht->mask;

    /* Search chain */
    hash_entry_t *entry = ht->buckets[bucket_idx];
    while (entry) {
        /* Check hash first (fast path) */
        if (entry->hash == hash) {
            /* Then check key */
            if (entry->key_len == key_len &&
                memcmp(entry->key, key, key_len) == 0) {
                return entry;
            }
        }
        entry = entry->next;
    }

    return NULL;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_key(char *key, uint32_t seed)
{
    /* Generate pseudo-random key using LFSR */
    uint32_t x = seed ^ 0xDEADBEEF;

    for (int i = 0; i < HASH_KEY_LEN; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        /* Use printable ASCII characters */
        key[i] = 'a' + (x % 26);
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    /* Initialize random seed */
    srand(12345);

    /* Initialize hash table */
    hash_init(&table, buckets, HASH_NUM_BUCKETS);

    /* Insert entries with generated keys */
    for (uint32_t i = 0; i < HASH_NUM_ENTRIES; i++) {
        char key[HASH_KEY_LEN];
        generate_key(key, i * 7 + 13);
        hash_insert(&table, &entries[i], key, HASH_KEY_LEN, (int32_t)(i * 100));
    }

    /* Generate lookup keys (mix of existing and non-existing) */
    for (uint32_t i = 0; i < HASH_NUM_LOOKUPS; i++) {
        if (i < HASH_NUM_LOOKUPS * 3 / 4) {
            /* Existing key (75% hit rate) */
            uint32_t idx = (i * 5) % HASH_NUM_ENTRIES;
            generate_key(lookup_keys[i], idx * 7 + 13);
        } else {
            /* Non-existing key (25% miss rate) */
            generate_key(lookup_keys[i], i * 1000 + 999999);
        }
    }
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();
    int32_t found_count = 0;
    int32_t value_sum = 0;

    /* Start timing */
    BENCH_START();

    /* Perform lookups */
    for (uint32_t i = 0; i < HASH_NUM_LOOKUPS; i++) {
        hash_entry_t *entry = hash_lookup(&table, lookup_keys[i], HASH_KEY_LEN);

        if (entry) {
            found_count++;
            value_sum += entry->value;
            csum = checksum_update(csum, (uint32_t)entry->value);
        } else {
            csum = checksum_update(csum, 0xFFFFFFFF);
        }

        /* Prevent optimization */
        BENCH_VOLATILE(entry);
    }

    /* End timing */
    BENCH_END();

    /* Include counts in checksum */
    csum = checksum_update(csum, (uint32_t)found_count);
    csum = checksum_update(csum, (uint32_t)value_sum);

    result.cycles = BENCH_CYCLES();
    result.checksum = csum;

    return result;
}

static void kernel_cleanup_func(void)
{
    /* Nothing to clean up (static storage) */
}

/* ============================================================================
 * Kernel Registration
 * ============================================================================ */

KERNEL_DECLARE(
    hash_lookup,
    "Hash table lookup (chained)",
    "400.perlbench",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,  /* Checksum computed at runtime */
    HASH_NUM_LOOKUPS
);

KERNEL_REGISTER(hash_lookup)
