/**
 * rlwe_core.h — Core types and constants for RLWE key generation
 * Ring: Zq[x] / (x^256 + 1),  q = 256
 */

#ifndef RLWE_CORE_H
#define RLWE_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef RLWE_FULL_MASKED_DECRYPT_ENABLE
#define RLWE_FULL_MASKED_DECRYPT_ENABLE 1
#endif

#if ((RLWE_FULL_MASKED_DECRYPT_ENABLE != 0) && \
     (RLWE_FULL_MASKED_DECRYPT_ENABLE != 1))
#error "RLWE_FULL_MASKED_DECRYPT_ENABLE must be 0 or 1"
#endif

/* ── Core parameters ──────────────────────────────────────────────── */
#define RLWE_PARAM_N256_Q256  1
#define RLWE_PARAM_N256_Q128  2
#define RLWE_PARAM_N512_Q256  3

#ifndef RLWE_PARAMETER_SET
#define RLWE_PARAMETER_SET RLWE_PARAM_N256_Q256
#endif

#if RLWE_PARAMETER_SET == RLWE_PARAM_N256_Q256
#define RLWE_N          256u
#define RLWE_Q          256u
#define RLWE_LOGQ       8u
#define RLWE_PARAMETER_SET_NAME "N256_Q256"
#elif RLWE_PARAMETER_SET == RLWE_PARAM_N256_Q128
#define RLWE_N          256u
#define RLWE_Q          128u
#define RLWE_LOGQ       7u
#define RLWE_PARAMETER_SET_NAME "N256_Q128"
#elif RLWE_PARAMETER_SET == RLWE_PARAM_N512_Q256
#define RLWE_N          512u
#define RLWE_Q          256u
#define RLWE_LOGQ       8u
#define RLWE_PARAMETER_SET_NAME "N512_Q256"
#else
#error "Unsupported RLWE_PARAMETER_SET"
#endif

#define RLWE_QHALF      (RLWE_Q / 2u)
#define RLWE_QQUARTER   (RLWE_Q / 4u)
#define RLWE_Q_MASK     (RLWE_Q - 1u)
#define RLWE_MSG_BITS   RLWE_N
#define RLWE_MSG_BYTES  (RLWE_MSG_BITS / 8u)

#if (RLWE_Q != (1u << RLWE_LOGQ)) || (RLWE_Q > 256u)
#error "RLWE coefficients require a power-of-two q no greater than 256"
#endif

#if RLWE_MSG_BITS > RLWE_N
#error "The selected ring is too small for the configured message"
#endif

/* ── Types ──────────────────────────────────────────────────────── */

/** Coefficient in Z_256. Stored compactly; arithmetic widens when needed. */
typedef uint8_t rlwe_coeff_t;

/** Polynomial in Zq[x]/(x^n+1). Coefficients in [0, q). */
typedef rlwe_coeff_t rlwe_poly[RLWE_N];

/** Paper ciphertext (c1,c2), retained as u,v for API compatibility. */
typedef struct {
    rlwe_poly u;    /* c1 = a*e1 + e2 */
    rlwe_poly v;    /* c2 = p*e1 + e3 + encode(m) */
} rlwe_ciphertext_t;

/** Explicit public parameter a and paper public key p = r1 - a*r2. */
typedef struct {
    rlwe_poly a;    /* Uniform random polynomial */
    rlwe_poly p;    /* p = r1 - a*r2 */
} rlwe_pubkey_t;

/** Paper secret r2. Its representation is selected at compile time. */
typedef struct {
#if RLWE_FULL_MASKED_DECRYPT_ENABLE
    /* Arithmetic sharing: s = share[0] + share[1] (mod q). */
    rlwe_poly share[2];
#else
    rlwe_poly s;
#endif
} rlwe_seckey_t;

#endif /* RLWE_CORE_H */
