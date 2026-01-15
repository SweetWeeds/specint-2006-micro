/*
 * SPECInt2006-micro: bench.h
 * Benchmark harness API
 */

#ifndef BENCH_H
#define BENCH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef NATIVE_BUILD
  /* Native x86 Linux build - use standard library */
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #define PLATFORM_NAME "linux"
#else
  /* nexus-am bare-metal build */
  #include <am.h>
  #include <klib.h>
  #define PLATFORM_NAME "nexus-am"
#endif

/* ============================================================================
 * Utility Macros
 * ============================================================================ */

#define INLINE static inline __attribute__((always_inline))
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define ALIGNED(n) __attribute__((aligned(n)))
#define UNUSED(x) (void)(x)

/* Min/Max */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) MIN(MAX(x, lo), hi)

/* ============================================================================
 * Platform Configuration
 * ============================================================================ */

#if defined(__riscv) && (__riscv_xlen == 64)
    #define ARCH_RISCV64
    #define ARCH_NAME "riscv64"
#elif defined(__riscv) && (__riscv_xlen == 32)
    #define ARCH_RISCV32
    #define ARCH_NAME "riscv32"
#elif defined(__x86_64__) || defined(_M_X64)
    #define ARCH_X86_64
    #define ARCH_NAME "x86-64"
#else
    #define ARCH_NAME "unknown"
#endif

/* ============================================================================
 * Memory Barrier
 * ============================================================================ */

INLINE void memory_barrier(void)
{
#if defined(ARCH_X86_64)
    __asm__ volatile ("mfence" ::: "memory");
#elif defined(ARCH_RISCV64) || defined(ARCH_RISCV32)
    __asm__ volatile ("fence rw, rw" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}

INLINE void compiler_barrier(void)
{
    __asm__ volatile ("" ::: "memory");
}

/* ============================================================================
 * Cycle Counter Reading
 * ============================================================================ */

#if defined(ARCH_X86_64)

INLINE uint64_t read_cycles(void)
{
    uint32_t lo, hi;
    __asm__ volatile (
        "rdtsc"
        : "=a"(lo), "=d"(hi)
    );
    return ((uint64_t)hi << 32) | lo;
}

#elif defined(ARCH_RISCV64)

INLINE uint64_t read_cycles(void)
{
    uint64_t cycles;
    __asm__ volatile (
        "rdcycle %0"
        : "=r"(cycles)
    );
    return cycles;
}

INLINE uint64_t read_instret(void)
{
    uint64_t instret;
    __asm__ volatile (
        "rdinstret %0"
        : "=r"(instret)
    );
    return instret;
}

#elif defined(ARCH_RISCV32)

INLINE uint64_t read_cycles(void)
{
    uint32_t lo, hi, hi2;
    do {
        __asm__ volatile (
            "rdcycleh %0\n\t"
            "rdcycle %1\n\t"
            "rdcycleh %2"
            : "=r"(hi), "=r"(lo), "=r"(hi2)
        );
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

#else

INLINE uint64_t read_cycles(void)
{
    /* Fallback: use uptime() from klib */
    return (uint64_t)uptime() * 1000;  /* Convert ms to rough cycle estimate */
}

#endif

/* ============================================================================
 * Timing Macros
 * ============================================================================ */

#define BENCH_START()       uint64_t _bench_start = read_cycles()
#define BENCH_END()         uint64_t _bench_end = read_cycles()
#define BENCH_CYCLES()      (_bench_end - _bench_start)

/* Prevent optimization of benchmark code */
#define BENCH_VOLATILE(x) __asm__ volatile ("" :: "r"(x) : "memory")

/* Force memory read/write to complete */
#define BENCH_FENCE() memory_barrier()

/* ============================================================================
 * Benchmark Result Structure
 * ============================================================================ */

typedef struct {
    uint64_t cycles;        /* Execution cycles */
    uint32_t checksum;      /* Result checksum for verification */
    int      status;        /* 0 = success, non-zero = error */
} bench_result_t;

/* Status codes */
#define BENCH_OK            0
#define BENCH_ERR_CHECKSUM  1
#define BENCH_ERR_TIMEOUT   2
#define BENCH_ERR_INTERNAL  3

/* ============================================================================
 * Kernel Function Signature
 * ============================================================================ */

typedef void (*kernel_init_t)(void);
typedef bench_result_t (*kernel_func_t)(void);
typedef void (*kernel_cleanup_t)(void);

/* ============================================================================
 * Kernel Descriptor
 * ============================================================================ */

typedef struct {
    const char     *name;               /* Kernel name */
    const char     *description;        /* Brief description */
    const char     *source_benchmark;   /* SPECInt2006 source benchmark */
    kernel_init_t   init;               /* Initialization (can be NULL) */
    kernel_func_t   run;                /* Main kernel function */
    kernel_cleanup_t cleanup;           /* Cleanup (can be NULL) */
    uint32_t        expected_checksum;  /* Expected checksum for verification */
    uint32_t        default_iterations; /* Default iteration count */
} kernel_desc_t;

/* ============================================================================
 * Kernel Registration
 * ============================================================================ */

#define MAX_KERNELS 32

void kernel_register(const kernel_desc_t *desc);
const kernel_desc_t *kernel_get(const char *name);
const kernel_desc_t *kernel_get_by_index(int index);
int kernel_count(void);

/* ============================================================================
 * Benchmark Execution
 * ============================================================================ */

typedef struct {
    int      warmup_runs;
    int      measure_runs;
    uint32_t iterations;
    bool     verify;
    bool     verbose;
} bench_config_t;

#define BENCH_CONFIG_DEFAULT { \
    .warmup_runs = 2,         \
    .measure_runs = 5,        \
    .iterations = 0,          \
    .verify = true,           \
    .verbose = false          \
}

typedef struct {
    const kernel_desc_t *kernel;
    uint64_t cycles_min;
    uint64_t cycles_max;
    uint64_t cycles_avg;
    uint64_t cycles_total;
    uint32_t checksum;
    int      runs_total;
    int      runs_pass;
    int      runs_fail;
    int      status;
} bench_stats_t;

bench_stats_t bench_run(const kernel_desc_t *kernel, const bench_config_t *config);
void bench_run_all(const bench_config_t *config);

/* ============================================================================
 * Result Reporting
 * ============================================================================ */

void bench_print_stats(const bench_stats_t *stats);
void bench_print_header(void);
void bench_print_footer(void);

typedef enum {
    OUTPUT_HUMAN,
    OUTPUT_CSV,
    OUTPUT_MACHINE
} output_format_t;

void bench_set_output_format(output_format_t format);

/* ============================================================================
 * Checksum Utilities
 * ============================================================================ */

INLINE uint32_t checksum_update(uint32_t csum, uint32_t value)
{
    /* FNV-1a style update */
    csum ^= value;
    csum *= 0x01000193;
    return csum;
}

INLINE uint32_t checksum_init(void)
{
    return 0x811c9dc5;  /* FNV-1a offset basis */
}

uint32_t checksum_buffer(const void *buf, size_t len);
uint32_t checksum_array32(const uint32_t *arr, size_t count);
uint32_t checksum_array64(const uint64_t *arr, size_t count);

/* ============================================================================
 * Utility Macros for Kernel Implementation
 * ============================================================================ */

/* KERNEL_DECLARE creates a globally visible kernel descriptor */
#define KERNEL_DECLARE(kname, kdesc, ksrc, kinit, krun, kcleanup, kchecksum, kiter) \
    const kernel_desc_t kernel_##kname = {                                          \
        .name = #kname,                                                             \
        .description = kdesc,                                                       \
        .source_benchmark = ksrc,                                                   \
        .init = kinit,                                                              \
        .run = krun,                                                                \
        .cleanup = kcleanup,                                                        \
        .expected_checksum = kchecksum,                                             \
        .default_iterations = kiter                                                 \
    }

/* KERNEL_REGISTER is now a no-op since we register manually in main */
#define KERNEL_REGISTER(kname) /* Manual registration in main.c */

/* External kernel declarations for manual registration */
extern const kernel_desc_t kernel_hash_lookup;
extern const kernel_desc_t kernel_string_match;
extern const kernel_desc_t kernel_regex_compile;
extern const kernel_desc_t kernel_bwt_sort;
extern const kernel_desc_t kernel_huffman_tree;
extern const kernel_desc_t kernel_mtf_transform;
extern const kernel_desc_t kernel_tree_walk;
extern const kernel_desc_t kernel_ssa_dataflow;
extern const kernel_desc_t kernel_graph_simplex;
extern const kernel_desc_t kernel_go_liberty;
extern const kernel_desc_t kernel_influence_field;
extern const kernel_desc_t kernel_viterbi_hmm;
extern const kernel_desc_t kernel_forward_backward;
extern const kernel_desc_t kernel_game_tree;
extern const kernel_desc_t kernel_quantum_sim;
extern const kernel_desc_t kernel_dct_4x4;
extern const kernel_desc_t kernel_block_sad;
extern const kernel_desc_t kernel_intra_predict;
extern const kernel_desc_t kernel_priority_queue;
extern const kernel_desc_t kernel_astar_path;
extern const kernel_desc_t kernel_xpath_eval;

#endif /* BENCH_H */
