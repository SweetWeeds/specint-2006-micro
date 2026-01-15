/*
 * SPECInt2006-micro: quantum_sim kernel
 * Captures quantum simulation from 462.libquantum
 *
 * Pattern: Quantum gate operations, complex number arithmetic, Shor's algorithm
 * Memory: Linear state vector manipulation
 * Branch: Regular (gate operations)
 */

#include "bench.h"

/* ============================================================================
 * Configuration (tune for 10K-100K cycles)
 * ============================================================================ */

#ifndef QUANTUM_NUM_QUBITS
#define QUANTUM_NUM_QUBITS      6       /* Number of qubits (2^6 = 64 states) */
#endif

#ifndef QUANTUM_NUM_GATES
#define QUANTUM_NUM_GATES       20      /* Number of gate operations */
#endif

#ifndef QUANTUM_FACTOR_N
#define QUANTUM_FACTOR_N        15      /* Number to factor (small for micro) */
#endif

/* Derived constants */
#define QUANTUM_NUM_STATES      (1 << QUANTUM_NUM_QUBITS)

/* Fixed-point arithmetic for complex numbers (avoid FP on bare-metal) */
#define QFIXED_SHIFT            16
#define QFIXED_ONE              (1 << QFIXED_SHIFT)
#define QFIXED_HALF             (1 << (QFIXED_SHIFT - 1))
#define QFIXED_SQRT2_INV        46341   /* 1/sqrt(2) * 2^16 ~= 0.7071 */

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Complex number in fixed-point */
typedef struct {
    int32_t real;       /* Fixed-point real part */
    int32_t imag;       /* Fixed-point imaginary part */
} qcomplex_t;

/* Quantum register */
typedef struct {
    qcomplex_t amplitude[QUANTUM_NUM_STATES];
    int num_qubits;
    int num_states;
} qreg_t;

/* Static storage */
static qreg_t qreg;
static uint32_t measurement_results[QUANTUM_NUM_GATES];

/* ============================================================================
 * Fixed-point Complex Arithmetic
 * ============================================================================ */

INLINE qcomplex_t qcomplex_zero(void)
{
    qcomplex_t c = {0, 0};
    return c;
}

INLINE qcomplex_t qcomplex_one(void)
{
    qcomplex_t c = {QFIXED_ONE, 0};
    return c;
}

INLINE qcomplex_t qcomplex_add(qcomplex_t a, qcomplex_t b)
{
    qcomplex_t c;
    c.real = a.real + b.real;
    c.imag = a.imag + b.imag;
    return c;
}

INLINE qcomplex_t qcomplex_sub(qcomplex_t a, qcomplex_t b)
{
    qcomplex_t c;
    c.real = a.real - b.real;
    c.imag = a.imag - b.imag;
    return c;
}

INLINE qcomplex_t qcomplex_mul(qcomplex_t a, qcomplex_t b)
{
    qcomplex_t c;
    /* (a + bi)(c + di) = (ac - bd) + (ad + bc)i */
    int64_t real = (int64_t)a.real * b.real - (int64_t)a.imag * b.imag;
    int64_t imag = (int64_t)a.real * b.imag + (int64_t)a.imag * b.real;
    c.real = (int32_t)(real >> QFIXED_SHIFT);
    c.imag = (int32_t)(imag >> QFIXED_SHIFT);
    return c;
}

INLINE qcomplex_t qcomplex_scale(qcomplex_t a, int32_t scale)
{
    qcomplex_t c;
    c.real = (int32_t)(((int64_t)a.real * scale) >> QFIXED_SHIFT);
    c.imag = (int32_t)(((int64_t)a.imag * scale) >> QFIXED_SHIFT);
    return c;
}

INLINE int32_t qcomplex_prob(qcomplex_t a)
{
    /* |a|^2 = real^2 + imag^2 */
    int64_t prob = (int64_t)a.real * a.real + (int64_t)a.imag * a.imag;
    return (int32_t)(prob >> QFIXED_SHIFT);
}

/* ============================================================================
 * Quantum Gate Operations
 * ============================================================================ */

/* Initialize register to |0...0> state */
static void qreg_init(qreg_t *reg)
{
    reg->num_qubits = QUANTUM_NUM_QUBITS;
    reg->num_states = QUANTUM_NUM_STATES;

    for (int i = 0; i < reg->num_states; i++) {
        reg->amplitude[i] = qcomplex_zero();
    }
    reg->amplitude[0] = qcomplex_one();
}

/* Hadamard gate on qubit q */
static void gate_hadamard(qreg_t *reg, int q)
{
    int mask = 1 << q;

    for (int i = 0; i < reg->num_states; i++) {
        if ((i & mask) == 0) {
            int j = i | mask;
            qcomplex_t a0 = reg->amplitude[i];
            qcomplex_t a1 = reg->amplitude[j];

            /* H|0> = (|0> + |1>)/sqrt(2) */
            /* H|1> = (|0> - |1>)/sqrt(2) */
            qcomplex_t sum = qcomplex_add(a0, a1);
            qcomplex_t diff = qcomplex_sub(a0, a1);

            reg->amplitude[i] = qcomplex_scale(sum, QFIXED_SQRT2_INV);
            reg->amplitude[j] = qcomplex_scale(diff, QFIXED_SQRT2_INV);
        }
    }
}

/* Pauli-X (NOT) gate on qubit q */
static void gate_pauli_x(qreg_t *reg, int q)
{
    int mask = 1 << q;

    for (int i = 0; i < reg->num_states; i++) {
        if ((i & mask) == 0) {
            int j = i | mask;
            qcomplex_t temp = reg->amplitude[i];
            reg->amplitude[i] = reg->amplitude[j];
            reg->amplitude[j] = temp;
        }
    }
}

/* Pauli-Z gate on qubit q */
static void gate_pauli_z(qreg_t *reg, int q)
{
    int mask = 1 << q;

    for (int i = 0; i < reg->num_states; i++) {
        if (i & mask) {
            /* Multiply by -1 */
            reg->amplitude[i].real = -reg->amplitude[i].real;
            reg->amplitude[i].imag = -reg->amplitude[i].imag;
        }
    }
}

/* Phase shift gate (S gate, 90 degree rotation) */
static void gate_phase(qreg_t *reg, int q)
{
    int mask = 1 << q;

    for (int i = 0; i < reg->num_states; i++) {
        if (i & mask) {
            /* Multiply by i: (a + bi) * i = -b + ai */
            int32_t temp = reg->amplitude[i].real;
            reg->amplitude[i].real = -reg->amplitude[i].imag;
            reg->amplitude[i].imag = temp;
        }
    }
}

/* CNOT (controlled-NOT) gate: control=c, target=t */
static void gate_cnot(qreg_t *reg, int c, int t)
{
    int cmask = 1 << c;
    int tmask = 1 << t;

    for (int i = 0; i < reg->num_states; i++) {
        /* If control qubit is 1 and target is 0, swap with target=1 state */
        if ((i & cmask) && !(i & tmask)) {
            int j = i | tmask;
            qcomplex_t temp = reg->amplitude[i];
            reg->amplitude[i] = reg->amplitude[j];
            reg->amplitude[j] = temp;
        }
    }
}

/* Toffoli (CCNOT) gate: controls c1,c2, target t */
static void gate_toffoli(qreg_t *reg, int c1, int c2, int t)
{
    int c1mask = 1 << c1;
    int c2mask = 1 << c2;
    int tmask = 1 << t;

    for (int i = 0; i < reg->num_states; i++) {
        /* If both controls are 1 and target is 0, swap */
        if ((i & c1mask) && (i & c2mask) && !(i & tmask)) {
            int j = i | tmask;
            qcomplex_t temp = reg->amplitude[i];
            reg->amplitude[i] = reg->amplitude[j];
            reg->amplitude[j] = temp;
        }
    }
}

/* ============================================================================
 * Quantum Fourier Transform (simplified)
 * ============================================================================ */

static void qft(qreg_t *reg)
{
    /* Simplified QFT: apply Hadamard + controlled phase rotations */
    for (int q = reg->num_qubits - 1; q >= 0; q--) {
        gate_hadamard(reg, q);

        /* Controlled phase rotations (simplified - just use CNOT patterns) */
        for (int j = q - 1; j >= 0; j--) {
            gate_cnot(reg, j, q);
            gate_phase(reg, q);
            gate_cnot(reg, j, q);
        }
    }
}

/* ============================================================================
 * Modular Arithmetic (for Shor's algorithm)
 * ============================================================================ */

/* Modular exponentiation: a^x mod n */
static uint32_t mod_exp(uint32_t a, uint32_t x, uint32_t n)
{
    uint64_t result = 1;
    uint64_t base = a % n;

    while (x > 0) {
        if (x & 1) {
            result = (result * base) % n;
        }
        x >>= 1;
        base = (base * base) % n;
    }

    return (uint32_t)result;
}

/* Controlled modular multiplication (simplified quantum version) */
static void controlled_mod_mul(qreg_t *reg, int ctrl, uint32_t a, uint32_t n)
{
    int cmask = 1 << ctrl;

    /* For states where control is |1>, apply modular operation */
    for (int i = 0; i < reg->num_states; i++) {
        if (i & cmask) {
            /* Get the value from non-control qubits and compute new value */
            int value = i & ~cmask;
            int new_value = (int)((uint64_t)value * a % n);

            /* Mix amplitudes (simplified - actual implementation is more complex) */
            if (new_value != value && new_value < reg->num_states) {
                int j = new_value | cmask;
                qcomplex_t sum = qcomplex_add(reg->amplitude[i], reg->amplitude[j]);
                reg->amplitude[i] = qcomplex_scale(sum, QFIXED_HALF);
                reg->amplitude[j] = qcomplex_scale(sum, QFIXED_HALF);
            }
        }
    }
}

/* ============================================================================
 * Measurement
 * ============================================================================ */

/* Measure register (returns most likely state) */
static int qreg_measure(qreg_t *reg, uint32_t rand_seed)
{
    /* Find state with highest probability */
    int32_t max_prob = 0;
    int max_state = 0;

    for (int i = 0; i < reg->num_states; i++) {
        int32_t prob = qcomplex_prob(reg->amplitude[i]);
        if (prob > max_prob) {
            max_prob = prob;
            max_state = i;
        }
    }

    /* Use random seed to add some variation */
    int32_t total_prob = 0;
    for (int i = 0; i < reg->num_states; i++) {
        total_prob += qcomplex_prob(reg->amplitude[i]);
    }

    if (total_prob > 0) {
        uint32_t x = rand_seed;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int32_t threshold = (int32_t)(x % (uint32_t)total_prob);

        int32_t cumulative = 0;
        for (int i = 0; i < reg->num_states; i++) {
            cumulative += qcomplex_prob(reg->amplitude[i]);
            if (cumulative > threshold) {
                return i;
            }
        }
    }

    return max_state;
}

/* ============================================================================
 * Shor's Algorithm Simulation (Order Finding)
 * ============================================================================ */

static uint32_t shor_order_finding(uint32_t n, uint32_t a, uint32_t seed)
{
    /* Initialize quantum register */
    qreg_init(&qreg);

    /* Step 1: Apply Hadamard to create superposition */
    for (int q = 0; q < qreg.num_qubits; q++) {
        gate_hadamard(&qreg, q);
    }

    /* Step 2: Controlled modular exponentiation */
    for (int q = 0; q < qreg.num_qubits; q++) {
        uint32_t exp = 1 << q;
        uint32_t a_exp = mod_exp(a, exp, n);
        controlled_mod_mul(&qreg, q, a_exp, n);
    }

    /* Step 3: Inverse QFT */
    qft(&qreg);

    /* Step 4: Measure */
    uint32_t result = qreg_measure(&qreg, seed);

    return result;
}

/* ============================================================================
 * Test Sequence Generation
 * ============================================================================ */

static void generate_gate_sequence(uint32_t seed)
{
    uint32_t x = seed;

    for (int i = 0; i < QUANTUM_NUM_GATES; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        measurement_results[i] = x;
    }
}

/* ============================================================================
 * Kernel Implementation
 * ============================================================================ */

static void kernel_init_func(void)
{
    generate_gate_sequence(0xABCDEF01);
    qreg_init(&qreg);
}

static bench_result_t kernel_run_func(void)
{
    bench_result_t result = { .status = BENCH_OK };
    uint32_t csum = checksum_init();

    BENCH_START();

    /* Test 1: Random gate sequence */
    qreg_init(&qreg);
    uint32_t x = 0x12345678;

    for (int i = 0; i < QUANTUM_NUM_GATES; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int gate_type = x % 6;
        int q1 = (x >> 8) % qreg.num_qubits;
        int q2 = ((x >> 16) % (qreg.num_qubits - 1));
        if (q2 >= q1) q2++;

        switch (gate_type) {
            case 0:
                gate_hadamard(&qreg, q1);
                break;
            case 1:
                gate_pauli_x(&qreg, q1);
                break;
            case 2:
                gate_pauli_z(&qreg, q1);
                break;
            case 3:
                gate_phase(&qreg, q1);
                break;
            case 4:
                gate_cnot(&qreg, q1, q2);
                break;
            case 5:
                if (qreg.num_qubits >= 3) {
                    int q3 = ((x >> 24) % (qreg.num_qubits - 2));
                    if (q3 >= q1) q3++;
                    if (q3 >= q2) q3++;
                    gate_toffoli(&qreg, q1, q2, q3);
                }
                break;
        }

        /* Checksum intermediate state */
        int32_t sum = 0;
        for (int j = 0; j < qreg.num_states; j++) {
            sum += qreg.amplitude[j].real + qreg.amplitude[j].imag;
        }
        csum = checksum_update(csum, (uint32_t)(int32_t)sum);
    }

    int measure1 = qreg_measure(&qreg, 0xDEADBEEF);
    csum = checksum_update(csum, (uint32_t)measure1);

    /* Test 2: QFT */
    qreg_init(&qreg);
    gate_hadamard(&qreg, 0);
    gate_hadamard(&qreg, 1);
    qft(&qreg);

    int measure2 = qreg_measure(&qreg, 0xCAFEBABE);
    csum = checksum_update(csum, (uint32_t)measure2);

    /* Test 3: Shor's order finding (simplified) */
    uint32_t order_result = shor_order_finding(QUANTUM_FACTOR_N, 7, 0x13579BDF);
    csum = checksum_update(csum, order_result);

    /* Test 4: Full state probability calculation */
    int32_t total_prob = 0;
    for (int i = 0; i < qreg.num_states; i++) {
        total_prob += qcomplex_prob(qreg.amplitude[i]);
    }
    csum = checksum_update(csum, (uint32_t)total_prob);

    BENCH_END();

    BENCH_VOLATILE(measure1);
    BENCH_VOLATILE(measure2);
    BENCH_VOLATILE(order_result);
    BENCH_VOLATILE(total_prob);

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
    quantum_sim,
    "Quantum gate simulation and Shor's algorithm",
    "462.libquantum",
    kernel_init_func,
    kernel_run_func,
    kernel_cleanup_func,
    0,
    QUANTUM_NUM_GATES
);

KERNEL_REGISTER(quantum_sim)
