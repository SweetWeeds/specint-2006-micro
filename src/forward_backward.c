/*
 * SPECInt2006-micro: forward_backward kernel
 * Captures Forward/Backward HMM algorithms from 456.hmmer
 *
 * Pattern: Dynamic programming matrix computation
 * Memory: 2D matrix traversal, probability accumulation
 * Branch: Log-space arithmetic for numerical stability
 */

#include "bench.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef FB_SEQ_LENGTH
#define FB_SEQ_LENGTH       64
#endif

#ifndef FB_NUM_STATES
#define FB_NUM_STATES       16
#endif

#ifndef FB_ALPHABET_SIZE
#define FB_ALPHABET_SIZE    20
#endif

#ifndef FB_NUM_SEQS
#define FB_NUM_SEQS         5
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Log probability representation (fixed-point for bare-metal) */
typedef int32_t logprob_t;

#define LOG_ZERO        (-1000000000)
#define LOG_ONE         0
#define LOGPROB_SCALE   1000  /* Fixed-point scale factor */

/* HMM model */
typedef struct {
    logprob_t trans[FB_NUM_STATES][FB_NUM_STATES];  /* Transition probabilities */
    logprob_t emit[FB_NUM_STATES][FB_ALPHABET_SIZE]; /* Emission probabilities */
    logprob_t begin[FB_NUM_STATES];  /* Initial state probabilities */
    logprob_t end[FB_NUM_STATES];    /* Terminal state probabilities */
} hmm_fb_t;

/* DP matrices */
typedef struct {
    logprob_t forward[FB_SEQ_LENGTH][FB_NUM_STATES];
    logprob_t backward[FB_SEQ_LENGTH][FB_NUM_STATES];
    logprob_t posterior[FB_SEQ_LENGTH][FB_NUM_STATES];
} dp_matrices_t;

static hmm_fb_t model;
static dp_matrices_t matrices;
static uint8_t sequence[FB_SEQ_LENGTH];

/* ============================================================================
 * Log-Space Arithmetic
 * ============================================================================ */

/* Log-sum-exp: log(exp(a) + exp(b)) with numerical stability */
static logprob_t log_add(logprob_t a, logprob_t b)
{
    if (a <= LOG_ZERO) return b;
    if (b <= LOG_ZERO) return a;

    logprob_t max_val = (a > b) ? a : b;
    logprob_t min_val = (a > b) ? b : a;

    /* Approximate log(1 + exp(min - max)) */
    int32_t diff = max_val - min_val;
    if (diff > 10 * LOGPROB_SCALE) return max_val;

    /* Lookup table approximation for log(1 + exp(-x)) */
    /* Using simple linear approximation */
    int32_t add = LOGPROB_SCALE - (diff * LOGPROB_SCALE) / (15 * LOGPROB_SCALE);
    if (add < 0) add = 0;

    return max_val + add;
}

/* ============================================================================
 * Forward Algorithm
 * ============================================================================ */

/* Compute forward probabilities */
static logprob_t forward_algorithm(const hmm_fb_t *hmm, const uint8_t *seq, int seq_len,
                                   logprob_t fwd[FB_SEQ_LENGTH][FB_NUM_STATES])
{
    /* Initialize first position */
    for (int k = 0; k < FB_NUM_STATES; k++) {
        fwd[0][k] = hmm->begin[k] + hmm->emit[k][seq[0]];
    }

    /* Forward recursion */
    for (int i = 1; i < seq_len; i++) {
        for (int k = 0; k < FB_NUM_STATES; k++) {
            logprob_t sum = LOG_ZERO;

            for (int j = 0; j < FB_NUM_STATES; j++) {
                logprob_t term = fwd[i-1][j] + hmm->trans[j][k];
                sum = log_add(sum, term);
            }

            fwd[i][k] = sum + hmm->emit[k][seq[i]];
        }
    }

    /* Termination: sum over final states */
    logprob_t total = LOG_ZERO;
    for (int k = 0; k < FB_NUM_STATES; k++) {
        total = log_add(total, fwd[seq_len-1][k] + hmm->end[k]);
    }

    return total;
}

/* ============================================================================
 * Backward Algorithm
 * ============================================================================ */

/* Compute backward probabilities */
static logprob_t backward_algorithm(const hmm_fb_t *hmm, const uint8_t *seq, int seq_len,
                                    logprob_t bwd[FB_SEQ_LENGTH][FB_NUM_STATES])
{
    /* Initialize last position */
    for (int k = 0; k < FB_NUM_STATES; k++) {
        bwd[seq_len-1][k] = hmm->end[k];
    }

    /* Backward recursion */
    for (int i = seq_len - 2; i >= 0; i--) {
        for (int k = 0; k < FB_NUM_STATES; k++) {
            logprob_t sum = LOG_ZERO;

            for (int j = 0; j < FB_NUM_STATES; j++) {
                logprob_t term = hmm->trans[k][j] + hmm->emit[j][seq[i+1]] + bwd[i+1][j];
                sum = log_add(sum, term);
            }

            bwd[i][k] = sum;
        }
    }

    /* Termination: sum over initial states */
    logprob_t total = LOG_ZERO;
    for (int k = 0; k < FB_NUM_STATES; k++) {
        total = log_add(total, hmm->begin[k] + hmm->emit[k][seq[0]] + bwd[0][k]);
    }

    return total;
}

/* ============================================================================
 * Posterior Decoding
 * ============================================================================ */

/* Compute posterior probabilities P(state k at position i | sequence) */
static void compute_posteriors(logprob_t fwd[FB_SEQ_LENGTH][FB_NUM_STATES],
                               logprob_t bwd[FB_SEQ_LENGTH][FB_NUM_STATES],
                               logprob_t post[FB_SEQ_LENGTH][FB_NUM_STATES],
                               logprob_t total_prob, int seq_len)
{
    for (int i = 0; i < seq_len; i++) {
        for (int k = 0; k < FB_NUM_STATES; k++) {
            post[i][k] = fwd[i][k] + bwd[i][k] - total_prob;
        }
    }
}

/* Find best state at each position (posterior decoding) */
static void posterior_decode(logprob_t post[FB_SEQ_LENGTH][FB_NUM_STATES],
                            int seq_len, int8_t *path)
{
    for (int i = 0; i < seq_len; i++) {
        int8_t best_state = 0;
        logprob_t best_prob = post[i][0];

        for (int k = 1; k < FB_NUM_STATES; k++) {
            if (post[i][k] > best_prob) {
                best_prob = post[i][k];
                best_state = k;
            }
        }

        path[i] = best_state;
    }
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_model(hmm_fb_t *hmm, uint32_t seed)
{
    uint32_t x = seed;

    /* Generate transition probabilities */
    for (int i = 0; i < FB_NUM_STATES; i++) {
        for (int j = 0; j < FB_NUM_STATES; j++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            /* Higher probability for staying in same state or moving forward */
            int base = (j == i) ? -1000 : (j == (i+1) % FB_NUM_STATES) ? -2000 : -5000;
            hmm->trans[i][j] = base + (int)(x % 1000) - 500;
        }
    }

    /* Generate emission probabilities */
    for (int i = 0; i < FB_NUM_STATES; i++) {
        for (int a = 0; a < FB_ALPHABET_SIZE; a++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            /* Each state has preferred emissions */
            int preferred = i % FB_ALPHABET_SIZE;
            int base = (a == preferred) ? -1000 : -3000;
            hmm->emit[i][a] = base + (x % 500) - 250;
        }
    }

    /* Begin/end probabilities */
    for (int i = 0; i < FB_NUM_STATES; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        hmm->begin[i] = (i == 0) ? -100 : -5000 + (int)(x % 1000);
        hmm->end[i] = (i == FB_NUM_STATES - 1) ? -100 : -5000 + (int)(x % 1000);
    }
}

static void generate_sequence(uint8_t *seq, int len, uint32_t seed)
{
    uint32_t x = seed;

    for (int i = 0; i < len; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        seq[i] = x % FB_ALPHABET_SIZE;
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    generate_model(&model, 0xDEADBEEF);
    memset(&matrices, 0, sizeof(matrices));
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();
    logprob_t total_fwd = 0;
    logprob_t total_bwd = 0;

    /* Start timing */
    BENCH_START();

    for (int s = 0; s < FB_NUM_SEQS; s++) {
        /* Generate sequence */
        generate_sequence(sequence, FB_SEQ_LENGTH, 0x12345678 + s * 1000);

        /* Forward algorithm */
        logprob_t fwd_score = forward_algorithm(&model, sequence, FB_SEQ_LENGTH,
                                                 matrices.forward);
        total_fwd += fwd_score;

        /* Backward algorithm */
        logprob_t bwd_score = backward_algorithm(&model, sequence, FB_SEQ_LENGTH,
                                                  matrices.backward);
        total_bwd += bwd_score;

        /* Compute posteriors */
        compute_posteriors(matrices.forward, matrices.backward,
                          matrices.posterior, fwd_score, FB_SEQ_LENGTH);

        /* Posterior decoding */
        int8_t path[FB_SEQ_LENGTH];
        posterior_decode(matrices.posterior, FB_SEQ_LENGTH, path);

        /* Update checksum */
        csum = checksum_update(csum, (uint32_t)(fwd_score & 0xFFFFFFFF));
        csum = checksum_update(csum, (uint32_t)(bwd_score & 0xFFFFFFFF));

        for (int i = 0; i < FB_SEQ_LENGTH; i++) {
            csum = checksum_update(csum, (uint32_t)path[i]);
        }

        /* Verify forward and backward give same total probability */
        /* Note: In fixed-point log-space, some numerical error is expected */
        int32_t diff = fwd_score - bwd_score;
        if (diff < 0) diff = -diff;
        if (diff > 100000) {  /* Allow larger tolerance for fixed-point arithmetic */
            result.status = BENCH_ERR_CHECKSUM;
        }
    }

    /* End timing */
    BENCH_END();

    csum = checksum_update(csum, (uint32_t)(total_fwd & 0xFFFFFFFF));
    csum = checksum_update(csum, (uint32_t)(total_bwd & 0xFFFFFFFF));

    BENCH_VOLATILE(total_fwd);

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
    forward_backward,
    "Forward/Backward HMM algorithms",
    "456.hmmer",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    FB_NUM_SEQS
);

KERNEL_REGISTER(forward_backward)
