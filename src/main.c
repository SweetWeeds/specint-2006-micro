/*
 * SPECInt2006-micro: main.c
 * Benchmark harness main entry point for nexus-am
 */

#include "bench.h"

/* Kernel registry */
static const kernel_desc_t *kernels[MAX_KERNELS];
static int num_kernels = 0;

/* Output format */
static output_format_t output_format = OUTPUT_HUMAN;

/* Summary statistics */
static bench_stats_t all_stats[MAX_KERNELS];
static int stats_count = 0;

/* ============================================================================
 * Base Cycle Counts for SPECInt2006 Score Calculation
 * Score = BASE_CYCLE / actual_cycles (score of 1.0 when cycles == BASE_CYCLE)
 * Values from BASE_CYCLE.txt, stored as integer * 100 for precision
 * ============================================================================ */
typedef struct {
    const char *benchmark;
    uint64_t base_cycle_x100;  /* Base cycle * 100 for integer arithmetic */
} benchmark_base_t;

static const benchmark_base_t base_cycles[] = {
    { "400.perlbench",    76896437 },  /* 768964.37 */
    { "401.bzip2",       250882020 },  /* 2508820.2 */
    { "403.gcc",         375198808 },  /* 3751988.08 */
    { "429.mcf",           7163965 },  /* 71639.65 */
    { "445.gobmk",       752228100 },  /* 7522281 */
    { "456.hmmer",       755623794 },  /* 7556237.94 */
    { "458.sjeng",          103360 },  /* 1033.6 */
    { "462.libquantum",  331920736 },  /* 3319207.36 */
    { "464.h264ref",     448875792 },  /* 4488757.92 */
    { "471.omnetpp",     172806876 },  /* 1728068.76 */
    { "473.astar",      2553353913 },  /* 25533539.13 */
    { "483.xalancbmk",    29604689 },  /* 296046.89 */
    { NULL, 0 }
};
#define NUM_BENCHMARKS 12

/*
 * Register a kernel
 */
void kernel_register(const kernel_desc_t *desc)
{
    if (num_kernels < MAX_KERNELS) {
        kernels[num_kernels++] = desc;
    }
}

/*
 * Get kernel by name
 */
const kernel_desc_t *kernel_get(const char *name)
{
    for (int i = 0; i < num_kernels; i++) {
        if (strcmp(kernels[i]->name, name) == 0) {
            return kernels[i];
        }
    }
    return NULL;
}

/*
 * Get kernel by index
 */
const kernel_desc_t *kernel_get_by_index(int index)
{
    if (index >= 0 && index < num_kernels) {
        return kernels[index];
    }
    return NULL;
}

/*
 * Get number of registered kernels
 */
int kernel_count(void)
{
    return num_kernels;
}

/*
 * Set output format
 */
void bench_set_output_format(output_format_t format)
{
    output_format = format;
}

/*
 * Print benchmark header
 */
void bench_print_header(void)
{
    if (output_format == OUTPUT_HUMAN) {
        printf("================================================================================\n");
        printf("SPECInt2006-micro Benchmark Results\n");
        printf("Architecture: %s\n", ARCH_NAME);
        printf("Platform: %s\n", PLATFORM_NAME);
        printf("================================================================================\n\n");
        printf("%-20s %12s %12s %12s %10s %s\n",
               "Kernel", "Min Cycles", "Avg Cycles", "Max Cycles", "Checksum", "Status");
        printf("--------------------------------------------------------------------------------\n");
    } else if (output_format == OUTPUT_CSV) {
        printf("kernel,min_cycles,avg_cycles,max_cycles,checksum,status\n");
    }
}

/*
 * Print benchmark statistics
 */
void bench_print_stats(const bench_stats_t *stats)
{
    const char *status_str = stats->status == BENCH_OK ? "PASS" : "FAIL";

    if (output_format == OUTPUT_HUMAN) {
        printf("%-20s %12lu %12lu %12lu 0x%08x %s\n",
               stats->kernel->name,
               (unsigned long)stats->cycles_min,
               (unsigned long)stats->cycles_avg,
               (unsigned long)stats->cycles_max,
               stats->checksum,
               status_str);
    } else if (output_format == OUTPUT_CSV) {
        printf("%s,%lu,%lu,%lu,0x%08x,%s\n",
               stats->kernel->name,
               (unsigned long)stats->cycles_min,
               (unsigned long)stats->cycles_avg,
               (unsigned long)stats->cycles_max,
               stats->checksum,
               status_str);
    } else {  /* OUTPUT_MACHINE */
        printf("[BENCH_START]\n");
        printf("kernel=%s\n", stats->kernel->name);
        printf("arch=%s\n", ARCH_NAME);
        printf("source=%s\n", stats->kernel->source_benchmark ? stats->kernel->source_benchmark : "unknown");
        printf("[RESULT]\n");
        printf("cycles_min=%lu\n", (unsigned long)stats->cycles_min);
        printf("cycles_avg=%lu\n", (unsigned long)stats->cycles_avg);
        printf("cycles_max=%lu\n", (unsigned long)stats->cycles_max);
        printf("checksum=0x%08x\n", stats->checksum);
        printf("expected=0x%08x\n", stats->kernel->expected_checksum);
        printf("runs_total=%d\n", stats->runs_total);
        printf("runs_pass=%d\n", stats->runs_pass);
        printf("runs_fail=%d\n", stats->runs_fail);
        printf("status=%s\n", status_str);
        printf("[BENCH_END]\n\n");
    }
}

/*
 * Count leading zeros for 64-bit value
 */
static int clz64(uint64_t x)
{
    if (x == 0) return 64;
    int n = 0;
    if ((x & 0xFFFFFFFF00000000ULL) == 0) { n += 32; x <<= 32; }
    if ((x & 0xFFFF000000000000ULL) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF00000000000000ULL) == 0) { n +=  8; x <<=  8; }
    if ((x & 0xF000000000000000ULL) == 0) { n +=  4; x <<=  4; }
    if ((x & 0xC000000000000000ULL) == 0) { n +=  2; x <<=  2; }
    if ((x & 0x8000000000000000ULL) == 0) { n +=  1; }
    return n;
}

/*
 * Calculate geometric mean of cycle counts
 */
static uint64_t calc_geomean(const bench_stats_t *stats, int count)
{
    if (count == 0) return 0;
    if (count == 1) return stats[0].cycles_avg;

    uint64_t log_sum = 0;
    const int FRAC_BITS = 20;

    for (int i = 0; i < count; i++) {
        uint64_t val = stats[i].cycles_avg;
        if (val == 0) val = 1;

        int msb = 63 - clz64(val);
        uint64_t log2_val = ((uint64_t)msb << FRAC_BITS);

        if (msb > 0 && msb < 44) {
            uint64_t base = 1ULL << msb;
            uint64_t frac = ((val - base) << FRAC_BITS) / base;
            log2_val += frac;
        }

        log_sum += log2_val;
    }

    uint64_t log_avg = log_sum / count;
    int int_part = (int)(log_avg >> FRAC_BITS);
    uint64_t frac_part = log_avg & ((1ULL << FRAC_BITS) - 1);

    if (int_part >= 63) return UINT64_MAX;

    uint64_t result = 1ULL << int_part;
    result += (result * frac_part) >> FRAC_BITS;

    return result;
}

/*
 * Per-benchmark statistics for BASE_CYCLE scoring
 */
typedef struct {
    const char *benchmark;
    uint64_t cycles_sum;
    uint64_t base_cycle_x100;
    uint64_t score_x100;  /* Score * 100 = base_cycle_x100 / cycles */
} benchmark_score_t;

/*
 * Calculate per-benchmark sum of cycles
 */
static uint64_t calc_benchmark_sum(const bench_stats_t *stats, int count, const char *benchmark)
{
    uint64_t sum = 0;

    for (int i = 0; i < count; i++) {
        if (stats[i].kernel->source_benchmark &&
            strcmp(stats[i].kernel->source_benchmark, benchmark) == 0) {
            sum += stats[i].cycles_avg;
        }
    }

    return sum;
}

/*
 * Print benchmark summary with BASE_CYCLE scoring
 * Score = BASE_CYCLE / actual_cycles
 */
static void bench_print_summary(const bench_stats_t *stats, int count)
{
    if (count == 0) return;

    int passed = 0, failed = 0;
    uint64_t total_cycles = 0;

    for (int i = 0; i < count; i++) {
        if (stats[i].status == BENCH_OK) {
            passed++;
        } else {
            failed++;
        }
        total_cycles += stats[i].cycles_avg;
    }

    /* Calculate per-benchmark statistics */
    benchmark_score_t bench_scores[NUM_BENCHMARKS];
    int bench_count = 0;

    for (int b = 0; base_cycles[b].benchmark != NULL; b++) {
        const char *bench_name = base_cycles[b].benchmark;
        uint64_t sum = calc_benchmark_sum(stats, count, bench_name);

        if (sum > 0) {
            bench_scores[bench_count].benchmark = bench_name;
            bench_scores[bench_count].cycles_sum = sum;
            bench_scores[bench_count].base_cycle_x100 = base_cycles[b].base_cycle_x100;
            /* Score = BASE_CYCLE / cycles, stored as score * 100 */
            bench_scores[bench_count].score_x100 = base_cycles[b].base_cycle_x100 / sum;
            bench_count++;
        }
    }

    /* Calculate overall geomean score (geometric mean of all benchmark scores) */
    uint64_t score_log_sum = 0;
    const int FRAC_BITS = 20;
    for (int i = 0; i < bench_count; i++) {
        uint64_t val = bench_scores[i].score_x100;
        if (val == 0) val = 1;
        int msb = 63 - clz64(val);
        uint64_t log2_val = ((uint64_t)msb << FRAC_BITS);
        if (msb > 0 && msb < 44) {
            uint64_t base = 1ULL << msb;
            uint64_t frac = ((val - base) << FRAC_BITS) / base;
            log2_val += frac;
        }
        score_log_sum += log2_val;
    }

    uint64_t geomean_score_x100 = 0;
    if (bench_count > 0) {
        uint64_t log_avg = score_log_sum / bench_count;
        int int_part = (int)(log_avg >> FRAC_BITS);
        uint64_t frac_part = log_avg & ((1ULL << FRAC_BITS) - 1);
        if (int_part < 63) {
            geomean_score_x100 = 1ULL << int_part;
            geomean_score_x100 += (geomean_score_x100 * frac_part) >> FRAC_BITS;
        }
    }

    /* Calculate raw geomean cycles */
    uint64_t raw_geomean = calc_geomean(stats, count);

    if (output_format == OUTPUT_HUMAN) {
        printf("--------------------------------------------------------------------------------\n");
        printf("\n");
        printf("Per-Benchmark Scores (BASE_CYCLE / Cycles):\n");
        printf("%-16s %12s %14s %8s\n", "Benchmark", "Cycles", "Base Cycle", "Score");
        printf("--------------------------------------------------------------------------------\n");
        for (int i = 0; i < bench_count; i++) {
            printf("%-16s %12lu %14lu %5lu.%02lu\n",
                   bench_scores[i].benchmark,
                   (unsigned long)bench_scores[i].cycles_sum,
                   (unsigned long)(bench_scores[i].base_cycle_x100 / 100),
                   (unsigned long)(bench_scores[i].score_x100 / 100),
                   (unsigned long)(bench_scores[i].score_x100 % 100));
        }
        printf("--------------------------------------------------------------------------------\n");
        printf("%-16s %12s %14s %5lu.%02lu\n",
               "GEOMEAN", "-", "-",
               (unsigned long)(geomean_score_x100 / 100),
               (unsigned long)(geomean_score_x100 % 100));
        printf("\n");
        printf("Summary:\n");
        printf("  Kernels:        %d total, %d passed, %d failed\n", count, passed, failed);
        printf("  Total Cycles:   %lu\n", (unsigned long)total_cycles);
        printf("  Raw Geomean:    %lu cycles\n", (unsigned long)raw_geomean);
        printf("  Final Score:    %lu.%02lu\n",
               (unsigned long)(geomean_score_x100 / 100),
               (unsigned long)(geomean_score_x100 % 100));
        printf("\n");
    } else if (output_format == OUTPUT_CSV) {
        printf("\n");
        printf("# Per-Benchmark Scores (BASE_CYCLE / Cycles)\n");
        printf("benchmark,cycles_sum,base_cycle,score\n");
        for (int i = 0; i < bench_count; i++) {
            printf("%s,%lu,%lu,%.2f\n",
                   bench_scores[i].benchmark,
                   (unsigned long)bench_scores[i].cycles_sum,
                   (unsigned long)(bench_scores[i].base_cycle_x100 / 100),
                   bench_scores[i].score_x100 / 100.0);
        }
        printf("GEOMEAN,-,-,%.2f\n", geomean_score_x100 / 100.0);
        printf("\n");
        printf("# Summary\n");
        printf("kernels_total,%d\n", count);
        printf("kernels_passed,%d\n", passed);
        printf("kernels_failed,%d\n", failed);
        printf("total_cycles,%lu\n", (unsigned long)total_cycles);
        printf("raw_geomean_cycles,%lu\n", (unsigned long)raw_geomean);
        printf("final_score,%.2f\n", geomean_score_x100 / 100.0);
    } else {
        printf("[PER_BENCHMARK]\n");
        for (int i = 0; i < bench_count; i++) {
            printf("%s=%lu,%.2f\n",
                   bench_scores[i].benchmark,
                   (unsigned long)bench_scores[i].cycles_sum,
                   bench_scores[i].score_x100 / 100.0);
        }
        printf("[SUMMARY]\n");
        printf("kernels_total=%d\n", count);
        printf("kernels_passed=%d\n", passed);
        printf("kernels_failed=%d\n", failed);
        printf("total_cycles=%lu\n", (unsigned long)total_cycles);
        printf("raw_geomean_cycles=%lu\n", (unsigned long)raw_geomean);
        printf("final_score=%.2f\n", geomean_score_x100 / 100.0);
        printf("[END]\n");
    }
}

/*
 * Print benchmark footer
 */
void bench_print_footer(void)
{
    if (stats_count > 0) {
        bench_print_summary(all_stats, stats_count);
    } else if (output_format == OUTPUT_HUMAN) {
        printf("--------------------------------------------------------------------------------\n");
        printf("\n");
    }
}

/*
 * Run a single kernel
 */
bench_stats_t bench_run(const kernel_desc_t *kernel, const bench_config_t *config)
{
    bench_stats_t stats = {
        .kernel = kernel,
        .cycles_min = UINT64_MAX,
        .cycles_max = 0,
        .cycles_avg = 0,
        .cycles_total = 0,
        .checksum = 0,
        .runs_total = 0,
        .runs_pass = 0,
        .runs_fail = 0,
        .status = BENCH_OK
    };

    /* Initialize kernel if needed */
    if (kernel->init) {
        kernel->init();
    }

    /* Warmup runs */
    for (int i = 0; i < config->warmup_runs; i++) {
        bench_result_t result = kernel->run();
        (void)result;
    }

    /* Measured runs */
    for (int i = 0; i < config->measure_runs; i++) {
        bench_result_t result = kernel->run();
        stats.runs_total++;

        if (result.status == BENCH_OK) {
            stats.runs_pass++;
            stats.cycles_total += result.cycles;
            stats.checksum = result.checksum;

            if (result.cycles < stats.cycles_min) {
                stats.cycles_min = result.cycles;
            }
            if (result.cycles > stats.cycles_max) {
                stats.cycles_max = result.cycles;
            }

            /* Verify checksum if requested */
            if (config->verify && kernel->expected_checksum != 0) {
                if (result.checksum != kernel->expected_checksum) {
                    stats.runs_fail++;
                    stats.runs_pass--;
                    stats.status = BENCH_ERR_CHECKSUM;
                    if (config->verbose) {
                        printf("  Checksum mismatch: got 0x%08x, expected 0x%08x\n",
                               result.checksum, kernel->expected_checksum);
                    }
                }
            }
        } else {
            stats.runs_fail++;
            stats.status = result.status;
        }
    }

    /* Calculate average */
    if (stats.runs_pass > 0) {
        stats.cycles_avg = stats.cycles_total / stats.runs_pass;
    }

    /* Cleanup kernel if needed */
    if (kernel->cleanup) {
        kernel->cleanup();
    }

    return stats;
}

/*
 * Print benchmark group header
 */
static void print_group_header(const char *benchmark)
{
    if (output_format == OUTPUT_HUMAN) {
        printf("\n[%s]\n", benchmark);
    } else if (output_format == OUTPUT_CSV) {
        printf("# %s\n", benchmark);
    }
}

/*
 * Run all registered kernels
 */
void bench_run_all(const bench_config_t *config)
{
    stats_count = 0;
    const char *current_benchmark = NULL;

    bench_print_header();

    for (int i = 0; i < num_kernels; i++) {
        /* Print group header when benchmark changes */
        const char *bench = kernels[i]->source_benchmark;
        if (bench && (!current_benchmark || strcmp(bench, current_benchmark) != 0)) {
            print_group_header(bench);
            current_benchmark = bench;
        }

        bench_stats_t stats = bench_run(kernels[i], config);
        bench_print_stats(&stats);

        if (stats_count < MAX_KERNELS) {
            all_stats[stats_count++] = stats;
        }
    }

    bench_print_footer();
}

/*
 * Checksum buffer
 */
uint32_t checksum_buffer(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t csum = checksum_init();

    while (len >= 4) {
        uint32_t value = p[0] | ((uint32_t)p[1] << 8) |
                        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        csum = checksum_update(csum, value);
        p += 4;
        len -= 4;
    }

    if (len > 0) {
        uint32_t value = 0;
        for (size_t i = 0; i < len; i++) {
            value |= (uint32_t)p[i] << (i * 8);
        }
        csum = checksum_update(csum, value);
    }

    return csum;
}

/*
 * Checksum array of 32-bit integers
 */
uint32_t checksum_array32(const uint32_t *arr, size_t count)
{
    uint32_t csum = checksum_init();
    for (size_t i = 0; i < count; i++) {
        csum = checksum_update(csum, arr[i]);
    }
    return csum;
}

/*
 * Checksum array of 64-bit integers
 */
uint32_t checksum_array64(const uint64_t *arr, size_t count)
{
    uint32_t csum = checksum_init();
    for (size_t i = 0; i < count; i++) {
        csum = checksum_update(csum, (uint32_t)arr[i]);
        csum = checksum_update(csum, (uint32_t)(arr[i] >> 32));
    }
    return csum;
}

/*
 * Register all kernels manually, grouped by SPECInt2006 benchmark
 * (nexus-am does not support __attribute__((constructor)))
 */
static void register_all_kernels(void)
{
    /* 400.perlbench */
    kernel_register(&kernel_hash_lookup);
    kernel_register(&kernel_string_match);
    kernel_register(&kernel_regex_compile);

    /* 401.bzip2 */
    kernel_register(&kernel_bwt_sort);
    kernel_register(&kernel_huffman_tree);
    kernel_register(&kernel_mtf_transform);

    /* 403.gcc */
    kernel_register(&kernel_tree_walk);
    kernel_register(&kernel_ssa_dataflow);

    /* 429.mcf */
    kernel_register(&kernel_graph_simplex);

    /* 445.gobmk */
    kernel_register(&kernel_go_liberty);
    kernel_register(&kernel_influence_field);

    /* 456.hmmer */
    kernel_register(&kernel_viterbi_hmm);
    kernel_register(&kernel_forward_backward);

    /* 458.sjeng */
    kernel_register(&kernel_game_tree);

    /* 462.libquantum */
    kernel_register(&kernel_quantum_sim);

    /* 464.h264ref */
    kernel_register(&kernel_dct_4x4);
    kernel_register(&kernel_block_sad);
    kernel_register(&kernel_intra_predict);

    /* 471.omnetpp */
    kernel_register(&kernel_priority_queue);

    /* 473.astar */
    kernel_register(&kernel_astar_path);

    /* 483.xalancbmk */
    kernel_register(&kernel_xpath_eval);
}

/*
 * Main entry point for nexus-am
 */
int main(void)
{
    bench_config_t config = BENCH_CONFIG_DEFAULT;

    /* Register all kernels */
    register_all_kernels();

    /* Run all benchmarks */
    bench_run_all(&config);

    return 0;
}
