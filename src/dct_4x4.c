/*
 * SPECInt2006-micro: dct_4x4 kernel
 * Captures H.264 4x4 DCT transform from 464.h264ref
 *
 * Pattern: Block-based transform with integer arithmetic
 * Memory: Regular 4x4 block access
 * Branch: Highly predictable (fixed block size)
 */

#include "bench.h"


/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef DCT_NUM_BLOCKS
#define DCT_NUM_BLOCKS      16      /* Number of 4x4 blocks to process */
#endif

#ifndef DCT_IMAGE_WIDTH
#define DCT_IMAGE_WIDTH     16      /* Image width (4 blocks) */
#endif

#ifndef DCT_IMAGE_HEIGHT
#define DCT_IMAGE_HEIGHT    16      /* Image height (4 blocks) */
#endif

/* Quantization parameter (0-51 in H.264) */
#ifndef DCT_QP
#define DCT_QP              20
#endif

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* 4x4 block type */
typedef int16_t block_4x4_t[4][4];

/* Static storage */
static uint8_t original[DCT_IMAGE_HEIGHT][DCT_IMAGE_WIDTH];
static uint8_t predicted[DCT_IMAGE_HEIGHT][DCT_IMAGE_WIDTH];
static int16_t residual[DCT_IMAGE_HEIGHT][DCT_IMAGE_WIDTH];
static block_4x4_t coef_blocks[DCT_NUM_BLOCKS];
static block_4x4_t reconstructed_blocks[DCT_NUM_BLOCKS];

/* ============================================================================
 * H.264 Integer DCT (4x4)
 *
 * The H.264 forward transform uses integer arithmetic:
 * Cf = C * X * C^T
 * where C is the transform matrix:
 *     | 1  1  1  1 |
 * C = | 2  1 -1 -2 |
 *     | 1 -1 -1  1 |
 *     | 1 -2  2 -1 |
 * ============================================================================ */

/* Forward 4x4 DCT transform */
static void dct_forward_4x4(const int16_t input[4][4], int16_t output[4][4])
{
    int16_t temp[4][4];

    /* Horizontal transform: temp = C * input */
    for (int i = 0; i < 4; i++) {
        int16_t a0 = input[i][0];
        int16_t a1 = input[i][1];
        int16_t a2 = input[i][2];
        int16_t a3 = input[i][3];

        int16_t p0 = a0 + a3;
        int16_t p1 = a1 + a2;
        int16_t p2 = a1 - a2;
        int16_t p3 = a0 - a3;

        temp[i][0] = p0 + p1;
        temp[i][1] = (p3 << 1) + p2;
        temp[i][2] = p0 - p1;
        temp[i][3] = p3 - (p2 << 1);
    }

    /* Vertical transform: output = temp * C^T */
    for (int j = 0; j < 4; j++) {
        int16_t a0 = temp[0][j];
        int16_t a1 = temp[1][j];
        int16_t a2 = temp[2][j];
        int16_t a3 = temp[3][j];

        int16_t p0 = a0 + a3;
        int16_t p1 = a1 + a2;
        int16_t p2 = a1 - a2;
        int16_t p3 = a0 - a3;

        output[0][j] = p0 + p1;
        output[1][j] = (p3 << 1) + p2;
        output[2][j] = p0 - p1;
        output[3][j] = p3 - (p2 << 1);
    }
}

/* Inverse 4x4 DCT transform */
static void dct_inverse_4x4(const int16_t input[4][4], int16_t output[4][4])
{
    int16_t temp[4][4];

    /* Horizontal inverse: temp = C^T * input */
    for (int i = 0; i < 4; i++) {
        int16_t a0 = input[i][0];
        int16_t a1 = input[i][1];
        int16_t a2 = input[i][2];
        int16_t a3 = input[i][3];

        int16_t p0 = a0 + a2;
        int16_t p1 = a0 - a2;
        int16_t p2 = (a1 >> 1) - a3;
        int16_t p3 = a1 + (a3 >> 1);

        temp[i][0] = p0 + p3;
        temp[i][1] = p1 + p2;
        temp[i][2] = p1 - p2;
        temp[i][3] = p0 - p3;
    }

    /* Vertical inverse: output = temp * C */
    for (int j = 0; j < 4; j++) {
        int16_t a0 = temp[0][j];
        int16_t a1 = temp[1][j];
        int16_t a2 = temp[2][j];
        int16_t a3 = temp[3][j];

        int16_t p0 = a0 + a2;
        int16_t p1 = a0 - a2;
        int16_t p2 = (a1 >> 1) - a3;
        int16_t p3 = a1 + (a3 >> 1);

        /* Add 32 for rounding, then divide by 64 */
        output[0][j] = (p0 + p3 + 32) >> 6;
        output[1][j] = (p1 + p2 + 32) >> 6;
        output[2][j] = (p1 - p2 + 32) >> 6;
        output[3][j] = (p0 - p3 + 32) >> 6;
    }
}

/* ============================================================================
 * Quantization
 * ============================================================================ */

/* Quantization matrices (simplified) */
static const int16_t quant_scale[6] = {13107, 11916, 10082, 9362, 8192, 7282};
static const int16_t dequant_scale[6] = {10, 11, 13, 14, 16, 18};

/* Forward quantization */
static void quant_4x4(int16_t block[4][4], int qp)
{
    int qp_rem = qp % 6;
    int qp_div = qp / 6;
    int16_t mf = quant_scale[qp_rem];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int32_t coef = block[i][j];
            int32_t sign = (coef >> 31);
            coef = (coef ^ sign) - sign;  /* abs */

            /* Quantize */
            coef = (coef * mf + (1 << (14 + qp_div))) >> (15 + qp_div);

            /* Restore sign */
            block[i][j] = (int16_t)((coef ^ sign) - sign);
        }
    }
}

/* Inverse quantization (dequantization) */
static void dequant_4x4(int16_t block[4][4], int qp)
{
    int qp_rem = qp % 6;
    int qp_div = qp / 6;
    int16_t scale = dequant_scale[qp_rem];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            block[i][j] = (block[i][j] * scale) << qp_div;
        }
    }
}

/* ============================================================================
 * Test Data Generation
 * ============================================================================ */

static void generate_test_image(uint8_t orig[][DCT_IMAGE_WIDTH],
                               uint8_t pred[][DCT_IMAGE_WIDTH],
                               uint32_t seed)
{
    uint32_t x = seed;

    for (int i = 0; i < DCT_IMAGE_HEIGHT; i++) {
        for (int j = 0; j < DCT_IMAGE_WIDTH; j++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;

            /* Original: smooth with some texture */
            orig[i][j] = (uint8_t)(128 + (i * 2) + (j * 2) + (x % 20) - 10);

            /* Predicted: close to original (good prediction) */
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            pred[i][j] = (uint8_t)(orig[i][j] + (x % 16) - 8);
        }
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    /* Generate test images */
    generate_test_image(original, predicted, 0x12345678);

    /* Compute residual */
    for (int i = 0; i < DCT_IMAGE_HEIGHT; i++) {
        for (int j = 0; j < DCT_IMAGE_WIDTH; j++) {
            residual[i][j] = (int16_t)original[i][j] - (int16_t)predicted[i][j];
        }
    }
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };

    /* Start timing */
    BENCH_START();

    /* Process each 4x4 block */
    int block_idx = 0;
    for (int by = 0; by < DCT_IMAGE_HEIGHT && block_idx < DCT_NUM_BLOCKS; by += 4) {
        for (int bx = 0; bx < DCT_IMAGE_WIDTH && block_idx < DCT_NUM_BLOCKS; bx += 4) {
            /* Extract block from residual */
            block_4x4_t input_block;
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    input_block[i][j] = residual[by + i][bx + j];
                }
            }

            /* Forward DCT */
            dct_forward_4x4(input_block, coef_blocks[block_idx]);

            /* Quantization */
            quant_4x4(coef_blocks[block_idx], DCT_QP);

            /* Dequantization */
            dequant_4x4(coef_blocks[block_idx], DCT_QP);

            /* Inverse DCT */
            dct_inverse_4x4(coef_blocks[block_idx], reconstructed_blocks[block_idx]);

            block_idx++;
        }
    }

    /* End timing */
    BENCH_END();

    /* Compute checksum of coefficients and reconstructed blocks */
    uint32_t csum = checksum_init();
    for (int b = 0; b < DCT_NUM_BLOCKS; b++) {
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                csum = checksum_update(csum, (uint32_t)(int32_t)coef_blocks[b][i][j]);
                csum = checksum_update(csum, (uint32_t)(int32_t)reconstructed_blocks[b][i][j]);
            }
        }
    }

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
    dct_4x4,
    "H.264 4x4 DCT transform",
    "464.h264ref",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    DCT_NUM_BLOCKS
);

KERNEL_REGISTER(dct_4x4)
