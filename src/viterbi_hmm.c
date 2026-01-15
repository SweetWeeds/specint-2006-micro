/*
 * SPECInt2006-micro: viterbi_hmm kernel
 * Captures Viterbi algorithm from 456.hmmer
 *
 * Pattern: Dynamic programming with 2D array access
 * Memory: Row-by-row sequential access
 * Branch: Mostly predictable loops
 */

#include "bench.h"


/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef HMM_SEQ_LENGTH
#define HMM_SEQ_LENGTH      50      /* Sequence length */
#endif

#ifndef HMM_MODEL_SIZE
#define HMM_MODEL_SIZE      32      /* Number of model states */
#endif

#ifndef HMM_ALPHABET_SIZE
#define HMM_ALPHABET_SIZE   20      /* Amino acids or similar */
#endif

/* Score constants (log probabilities scaled to integers) */
#define SCORE_MIN           (-999999)
#define SCORE_SCALE         1000

/* ============================================================================
 * Data Structures (similar to HMMER's plan7)
 * ============================================================================ */

/* HMM parameters */
typedef struct {
    int32_t match_emit[HMM_MODEL_SIZE][HMM_ALPHABET_SIZE];    /* Match emissions */
    int32_t insert_emit[HMM_MODEL_SIZE][HMM_ALPHABET_SIZE];   /* Insert emissions */
    int32_t trans_mm[HMM_MODEL_SIZE];    /* Match to Match */
    int32_t trans_mi[HMM_MODEL_SIZE];    /* Match to Insert */
    int32_t trans_md[HMM_MODEL_SIZE];    /* Match to Delete */
    int32_t trans_im[HMM_MODEL_SIZE];    /* Insert to Match */
    int32_t trans_ii[HMM_MODEL_SIZE];    /* Insert to Insert */
    int32_t trans_dm[HMM_MODEL_SIZE];    /* Delete to Match */
    int32_t trans_dd[HMM_MODEL_SIZE];    /* Delete to Delete */
    int32_t begin[HMM_MODEL_SIZE];       /* Begin transition */
    int32_t end[HMM_MODEL_SIZE];         /* End transition */
} hmm_model_t;

/* DP matrix row */
typedef struct {
    int32_t m[HMM_MODEL_SIZE];    /* Match scores */
    int32_t i[HMM_MODEL_SIZE];    /* Insert scores */
    int32_t d[HMM_MODEL_SIZE];    /* Delete scores */
} dp_row_t;

/* Static storage */
static hmm_model_t model;
static uint8_t sequence[HMM_SEQ_LENGTH];
static dp_row_t dp_prev;
static dp_row_t dp_curr;

/* ============================================================================
 * Viterbi Algorithm
 * ============================================================================ */

INLINE int32_t score_max3(int32_t a, int32_t b, int32_t c)
{
    int32_t max = a;
    if (b > max) max = b;
    if (c > max) max = c;
    return max;
}

/* Run Viterbi algorithm, return best score */
static int32_t viterbi_score(const hmm_model_t *hmm, const uint8_t *seq, int seq_len)
{
    int32_t best_score = SCORE_MIN;

    /* Initialize first row */
    for (int k = 0; k < HMM_MODEL_SIZE; k++) {
        dp_prev.m[k] = SCORE_MIN;
        dp_prev.i[k] = SCORE_MIN;
        dp_prev.d[k] = SCORE_MIN;
    }

    /* Process each position in sequence */
    for (int i = 0; i < seq_len; i++) {
        int sym = seq[i] % HMM_ALPHABET_SIZE;

        /* Initialize current row */
        for (int k = 0; k < HMM_MODEL_SIZE; k++) {
            dp_curr.m[k] = SCORE_MIN;
            dp_curr.i[k] = SCORE_MIN;
            dp_curr.d[k] = SCORE_MIN;
        }

        /* First state: only begin transition */
        dp_curr.m[0] = hmm->begin[0] + hmm->match_emit[0][sym];

        /* Fill DP matrix */
        for (int k = 1; k < HMM_MODEL_SIZE; k++) {
            /* Match state: from M, I, D of previous position, or begin */
            int32_t m_score = score_max3(
                dp_prev.m[k-1] + hmm->trans_mm[k-1],
                dp_prev.i[k-1] + hmm->trans_im[k-1],
                dp_prev.d[k-1] + hmm->trans_dm[k-1]
            );

            /* Also consider begin transition */
            if (hmm->begin[k] > m_score) {
                m_score = hmm->begin[k];
            }

            dp_curr.m[k] = m_score + hmm->match_emit[k][sym];

            /* Insert state: from M or I of current position */
            int32_t i_score = score_max3(
                dp_curr.m[k] + hmm->trans_mi[k],
                dp_curr.i[k] + hmm->trans_ii[k],
                SCORE_MIN
            );
            dp_curr.i[k] = i_score + hmm->insert_emit[k][sym];

            /* Delete state: from M or D of previous position (no emission) */
            dp_curr.d[k] = score_max3(
                dp_prev.m[k] + hmm->trans_md[k],
                dp_prev.d[k] + hmm->trans_dd[k],
                SCORE_MIN
            );
        }

        /* Check end transitions */
        for (int k = 0; k < HMM_MODEL_SIZE; k++) {
            int32_t end_score = dp_curr.m[k] + hmm->end[k];
            if (end_score > best_score) {
                best_score = end_score;
            }
        }

        /* Swap rows */
        dp_row_t tmp = dp_prev;
        dp_prev = dp_curr;
        dp_curr = tmp;
    }

    return best_score;
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_model(hmm_model_t *hmm, uint32_t seed)
{
    uint32_t x = seed;

    /* Helper to get pseudo-random value */
    #define RAND_NEXT() (x ^= x << 13, x ^= x >> 17, x ^= x << 5, x)

    /* Generate emission scores */
    for (int k = 0; k < HMM_MODEL_SIZE; k++) {
        for (int a = 0; a < HMM_ALPHABET_SIZE; a++) {
            /* Log probabilities (negative, scaled) */
            hmm->match_emit[k][a] = -(int32_t)(RAND_NEXT() % 5000);
            hmm->insert_emit[k][a] = -(int32_t)(RAND_NEXT() % 5000);
        }

        /* Make one emission favorable per state */
        int best_sym = k % HMM_ALPHABET_SIZE;
        hmm->match_emit[k][best_sym] = 0;
    }

    /* Generate transition scores */
    for (int k = 0; k < HMM_MODEL_SIZE; k++) {
        /* Prefer match-to-match */
        hmm->trans_mm[k] = -(int32_t)(RAND_NEXT() % 1000);
        hmm->trans_mi[k] = -(int32_t)(2000 + RAND_NEXT() % 2000);
        hmm->trans_md[k] = -(int32_t)(2000 + RAND_NEXT() % 2000);
        hmm->trans_im[k] = -(int32_t)(1000 + RAND_NEXT() % 1000);
        hmm->trans_ii[k] = -(int32_t)(500 + RAND_NEXT() % 1000);
        hmm->trans_dm[k] = -(int32_t)(1000 + RAND_NEXT() % 1000);
        hmm->trans_dd[k] = -(int32_t)(500 + RAND_NEXT() % 1000);

        /* Begin/end transitions */
        hmm->begin[k] = (k == 0) ? 0 : -(int32_t)(3000 + k * 100);
        hmm->end[k] = (k == HMM_MODEL_SIZE - 1) ? 0 : -(int32_t)(3000 + (HMM_MODEL_SIZE - k) * 100);
    }

    #undef RAND_NEXT
}

static void generate_sequence(uint8_t *seq, int len, uint32_t seed)
{
    uint32_t x = seed;

    for (int i = 0; i < len; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        seq[i] = x % HMM_ALPHABET_SIZE;
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    /* Generate HMM model */
    generate_model(&model, 0xABCDEF01);

    /* Generate test sequence */
    generate_sequence(sequence, HMM_SEQ_LENGTH, 0x13579BDF);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };

    /* Start timing */
    BENCH_START();

    /* Run Viterbi algorithm */
    int32_t score = viterbi_score(&model, sequence, HMM_SEQ_LENGTH);

    /* End timing */
    BENCH_END();

    /* Prevent optimization */
    BENCH_VOLATILE(score);

    /* Checksum */
    uint32_t csum = checksum_init();
    csum = checksum_update(csum, (uint32_t)score);
    csum = checksum_update(csum, HMM_SEQ_LENGTH);
    csum = checksum_update(csum, HMM_MODEL_SIZE);

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
    viterbi_hmm,
    "Viterbi HMM scoring",
    "456.hmmer",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    1
);

KERNEL_REGISTER(viterbi_hmm)
