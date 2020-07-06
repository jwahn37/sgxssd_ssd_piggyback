#include "ecdsa_sgx.h"

//#include <string.h>

#define NUM_ECC_DIGITS (ECC_BYTES/8)
#define MAX_TRIES 16

typedef unsigned int uint;
/*
#if defined(__SIZEOF_INT128__) || ((__clang_major__ * 100 + __clang_minor__) >= 302)
    #define SUPPORTS_INT128 1
#else
    #define SUPPORTS_INT128 0
#endif
*/
#define SUPPORTS_INT128 0

#if SUPPORTS_INT128
typedef unsigned __int128 uint128_t;
#else
typedef struct
{
    uint64_t m_low;
    uint64_t m_high;
} uint128_t;
#endif

typedef struct EccPoint
{
    uint64_t x[NUM_ECC_DIGITS];
    uint64_t y[NUM_ECC_DIGITS];
} EccPoint;

#define CONCAT1(a, b) a##b
#define CONCAT(a, b) CONCAT1(a, b)

//#define Curve_P_16 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFDFFFFFFFF}
//#define Curve_P_24 {0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFEull, 0xFFFFFFFFFFFFFFFFull}
#define Curve_P_32 {0xFFFFFFFFFFFFFFFFull, 0x00000000FFFFFFFFull, 0x0000000000000000ull, 0xFFFFFFFF00000001ull}
//#define Curve_P_48 {0x00000000FFFFFFFF, 0xFFFFFFFF00000000, 0xFFFFFFFFFFFFFFFE, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF}

//#define Curve_B_16 {0xD824993C2CEE5ED3, 0xE87579C11079F43D}
//#define Curve_B_24 {0xFEB8DEECC146B9B1ull, 0x0FA7E9AB72243049ull, 0x64210519E59C80E7ull}
#define Curve_B_32 {0x3BCE3C3E27D2604Bull, 0x651D06B0CC53B0F6ull, 0xB3EBBD55769886BCull, 0x5AC635D8AA3A93E7ull}
//#define Curve_B_48 {0x2A85C8EDD3EC2AEF, 0xC656398D8A2ED19D, 0x0314088F5013875A, 0x181D9C6EFE814112, 0x988E056BE3F82D19, 0xB3312FA7E23EE7E4}

//#define Curve_G_16 { \
    {0x0C28607CA52C5B86, 0x161FF7528B899B2D}, \
    {0xC02DA292DDED7A83, 0xCF5AC8395BAFEB13}}

//#define Curve_G_24 { \
    {0xF4FF0AFD82FF1012ull, 0x7CBF20EB43A18800ull, 0x188DA80EB03090F6ull}, \
    {0x73F977A11E794811ull, 0x631011ED6B24CDD5ull, 0x07192B95FFC8DA78ull}}
    
#define Curve_G_32 { \
    {0xF4A13945D898C296ull, 0x77037D812DEB33A0ull, 0xF8BCE6E563A440F2ull, 0x6B17D1F2E12C4247ull}, \
    {0xCBB6406837BF51F5ull, 0x2BCE33576B315ECEull, 0x8EE7EB4A7C0F9E16ull, 0x4FE342E2FE1A7F9Bull}}

//#define Curve_G_48 { \
    {0x3A545E3872760AB7, 0x5502F25DBF55296C, 0x59F741E082542A38, 0x6E1D3B628BA79B98, 0x8EB1C71EF320AD74, 0xAA87CA22BE8B0537}, \
    {0x7A431D7C90EA0E5F, 0x0A60B1CE1D7E819D, 0xE9DA3113B5F0B8C0, 0xF8F41DBD289A147C, 0x5D9E98BF9292DC29, 0x3617DE4A96262C6F}}

//#define Curve_N_16 {0x75A30D1B9038A115, 0xFFFFFFFE00000000}
//#define Curve_N_24 {0x146BC9B1B4D22831ull, 0xFFFFFFFF99DEF836ull, 0xFFFFFFFFFFFFFFFFull}
#define Curve_N_32 {0xF3B9CAC2FC632551ull, 0xBCE6FAADA7179E84ull, 0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFF00000000ull}
//#define Curve_N_48 {0xECEC196ACCC52973, 0x581A0DB248B0A77A, 0xC7634D81F4372DDF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF}

static uint64_t curve_p[NUM_ECC_DIGITS] = CONCAT(Curve_P_, ECC_CURVE);
static uint64_t curve_b[NUM_ECC_DIGITS] = CONCAT(Curve_B_, ECC_CURVE);
static EccPoint curve_G = CONCAT(Curve_G_, ECC_CURVE);
static uint64_t curve_n[NUM_ECC_DIGITS] = CONCAT(Curve_N_, ECC_CURVE);

/*
//#if (defined(_WIN32) || defined(_WIN64))
// Windows 

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

static int getRandomNumber(uint64_t *p_vli)
{
    HCRYPTPROV l_prov;
    if(!CryptAcquireContext(&l_prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        return 0;
    }

    CryptGenRandom(l_prov, ECC_BYTES, (BYTE *)p_vli);
    CryptReleaseContext(l_prov, 0);
    
    return 1;
}

//#else  _WIN32 

// Assume that we are using a POSIX-like system with /dev/urandom or /dev/random. 
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_CLOEXEC
    #define O_CLOEXEC 0
#endif

static int getRandomNumber(uint64_t *p_vli)
{
    int l_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if(l_fd == -1)
    {
        l_fd = open("/dev/random", O_RDONLY | O_CLOEXEC);
        if(l_fd == -1)
        {
            return 0;
        }
    }
    
    char *l_ptr = (char *)p_vli;
    size_t l_left = ECC_BYTES;
    while(l_left > 0)
    {
        int l_read = read(l_fd, l_ptr, l_left);
        if(l_read <= 0)
        { // read failed
            close(l_fd);
            return 0;
        }
        l_left -= l_read;
        l_ptr += l_read;
    }
    g
    close(l_fd);
    return 1;
}

//#endif // _WIN32 
*/
//SSD mode
/*
static int getRandomNumber(uint64_t *p_vli)
{
    char *l_ptr = (char *)p_vli;
    uint64_t l_left = ECC_BYTES;
    int i;

    for(i=0; i<ECC_BYTES; i++)
    {
        p_vli[i] = 0x10203040;  //here, should put random number //SSD has timer! 
    }
    return 1;
    
}
*/
//verification
static void vli_clear(uint64_t *p_vli)
{
    uint i;
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        p_vli[i] = 0;
    }
}
//verification
/* Returns 1 if p_vli == 0, 0 otherwise. */
static int vli_isZero(uint64_t *p_vli)
{
    uint i;
    for(i = 0; i < NUM_ECC_DIGITS; ++i)
    {
        if(p_vli[i])
        {
            return 0;
        }
    }
    return 1;
}
//verification
/* Returns nonzero if bit p_bit of p_vli is set. */
static uint64_t vli_testBit(uint64_t *p_vli, uint p_bit)
{
    return (p_vli[p_bit/64] & ((uint64_t)1 << (p_bit % 64)));
}
//verification
/* Counts the number of 64-bit "digits" in p_vli. */
static uint vli_numDigits(uint64_t *p_vli)
{
    int i;
    /* Search from the end until we find a non-zero digit.
       We do it in reverse because we expect that most digits will be nonzero. */
    for(i = NUM_ECC_DIGITS - 1; i >= 0 && p_vli[i] == 0; --i)
    {
    }

    return (i + 1);
}
//verification
/* Counts the number of bits required for p_vli. */
static uint vli_numBits(uint64_t *p_vli)
{
    uint i;
    uint64_t l_digit;
    
    uint l_numDigits = vli_numDigits(p_vli);
    if(l_numDigits == 0)
    {
        return 0;
    }

    l_digit = p_vli[l_numDigits - 1];
    for(i=0; l_digit; ++i)
    {
        l_digit >>= 1;
    }
    
    return ((l_numDigits - 1) * 64 + i);
}
//verification
/* Sets p_dest = p_src. */
static void vli_set(uint64_t *p_dest, uint64_t *p_src)
{
    uint i;
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        p_dest[i] = p_src[i];
    }
}
//verification
/* Returns sign of p_left - p_right. */
static int vli_cmp(uint64_t *p_left, uint64_t *p_right)
{
    int i;
    for(i = NUM_ECC_DIGITS-1; i >= 0; --i)
    {
        if(p_left[i] > p_right[i])
        {
            return 1;
        }
        else if(p_left[i] < p_right[i])
        {
            return -1;
        }
    }
    return 0;
}
//verify
/* Computes p_result = p_in << c, returning carry. Can modify in place (if p_result == p_in). 0 < p_shift < 64. */
static uint64_t vli_lshift(uint64_t *p_result, uint64_t *p_in, uint p_shift)
{
    uint64_t l_carry = 0;
    uint i;
    for(i = 0; i < NUM_ECC_DIGITS; ++i)
    {
        uint64_t l_temp = p_in[i];
        p_result[i] = (l_temp << p_shift) | l_carry;
        l_carry = l_temp >> (64 - p_shift);
    }
    
    return l_carry;
}
//verify
/* Computes p_vli = p_vli >> 1. */
static void vli_rshift1(uint64_t *p_vli)
{
    uint64_t *l_end = p_vli;
    uint64_t l_carry = 0;
    
    p_vli += NUM_ECC_DIGITS;
    while(p_vli-- > l_end)
    {
        uint64_t l_temp = *p_vli;
        *p_vli = (l_temp >> 1) | l_carry;
        l_carry = l_temp << 63;
    }
}
//verify
/* Computes p_result = p_left + p_right, returning carry. Can modify in place. */
static uint64_t vli_add(uint64_t *p_result, uint64_t *p_left, uint64_t *p_right)
{
    uint64_t l_carry = 0;
    uint i;
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        uint64_t l_sum = p_left[i] + p_right[i] + l_carry;
        if(l_sum != p_left[i])
        {
            l_carry = (l_sum < p_left[i]);
        }
        p_result[i] = l_sum;
    }
    return l_carry;
}
//verification
/* Computes p_result = p_left - p_right, returning borrow. Can modify in place. */
static uint64_t vli_sub(uint64_t *p_result, uint64_t *p_left, uint64_t *p_right)
{
    uint64_t l_borrow = 0;
    uint i;
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        uint64_t l_diff = p_left[i] - p_right[i] - l_borrow;
        if(l_diff != p_left[i])
        {
            l_borrow = (l_diff > p_left[i]);
        }
        p_result[i] = l_diff;
    }
    return l_borrow;
}

#if SUPPORTS_INT128

/* Computes p_result = p_left * p_right. */
static void vli_mult(uint64_t *p_result, uint64_t *p_left, uint64_t *p_right)
{
    uint128_t r01 = 0;
    uint64_t r2 = 0;
    
    uint i, k;
    
    /* Compute each digit of p_result in sequence, maintaining the carries. */
    for(k=0; k < NUM_ECC_DIGITS*2 - 1; ++k)
    {
        uint l_min = (k < NUM_ECC_DIGITS ? 0 : (k + 1) - NUM_ECC_DIGITS);
        for(i=l_min; i<=k && i<NUM_ECC_DIGITS; ++i)
        {
            uint128_t l_product = (uint128_t)p_left[i] * p_right[k-i];
            r01 += l_product;
            r2 += (r01 < l_product);
        }
        p_result[k] = (uint64_t)r01;
        r01 = (r01 >> 64) | (((uint128_t)r2) << 64);
        r2 = 0;
    }
    
    p_result[NUM_ECC_DIGITS*2 - 1] = (uint64_t)r01;
}

/* Computes p_result = p_left^2. */
static void vli_square(uint64_t *p_result, uint64_t *p_left)
{
    uint128_t r01 = 0;
    uint64_t r2 = 0;
    
    uint i, k;
    for(k=0; k < NUM_ECC_DIGITS*2 - 1; ++k)
    {
        uint l_min = (k < NUM_ECC_DIGITS ? 0 : (k + 1) - NUM_ECC_DIGITS);
        for(i=l_min; i<=k && i<=k-i; ++i)
        {
            uint128_t l_product = (uint128_t)p_left[i] * p_left[k-i];
            if(i < k-i)
            {
                r2 += l_product >> 127;
                l_product *= 2;
            }
            r01 += l_product;
            r2 += (r01 < l_product);
        }
        p_result[k] = (uint64_t)r01;
        r01 = (r01 >> 64) | (((uint128_t)r2) << 64);
        r2 = 0;
    }
    
    p_result[NUM_ECC_DIGITS*2 - 1] = (uint64_t)r01;
}

#else /* #if SUPPORTS_INT128 */

//verify
static uint128_t mul_64_64(uint64_t p_left, uint64_t p_right)
{
    uint128_t l_result;
    
    uint64_t a0 = p_left & 0xffffffffull;
    uint64_t a1 = p_left >> 32;
    uint64_t b0 = p_right & 0xffffffffull;
    uint64_t b1 = p_right >> 32;
    
    uint64_t m0 = a0 * b0;
    uint64_t m1 = a0 * b1;
    uint64_t m2 = a1 * b0;
    uint64_t m3 = a1 * b1;
    
    m2 += (m0 >> 32);
    m2 += m1;
    if(m2 < m1)
    { // overflow
        m3 += 0x100000000ull;
    }
    
    l_result.m_low = (m0 & 0xffffffffull) | (m2 << 32);
    l_result.m_high = m3 + (m2 >> 32);
    
    return l_result;
}
//verify
static uint128_t add_128_128(uint128_t a, uint128_t b)
{
    uint128_t l_result;
    l_result.m_low = a.m_low + b.m_low;
    l_result.m_high = a.m_high + b.m_high + (l_result.m_low < a.m_low);
    return l_result;
}
//verify
static void vli_mult(uint64_t *p_result, uint64_t *p_left, uint64_t *p_right)
{
    uint128_t r01 = {0, 0};
    uint64_t r2 = 0;
    
    uint i, k;
    
    /* Compute each digit of p_result in sequence, maintaining the carries. */
    for(k=0; k < NUM_ECC_DIGITS*2 - 1; ++k)
    {
        uint l_min = (k < NUM_ECC_DIGITS ? 0 : (k + 1) - NUM_ECC_DIGITS);
        for(i=l_min; i<=k && i<NUM_ECC_DIGITS; ++i)
        {
            uint128_t l_product = mul_64_64(p_left[i], p_right[k-i]);
            r01 = add_128_128(r01, l_product);
            r2 += (r01.m_high < l_product.m_high);
        }
        p_result[k] = r01.m_low;
        r01.m_low = r01.m_high;
        r01.m_high = r2;
        r2 = 0;
    }
    
    p_result[NUM_ECC_DIGITS*2 - 1] = r01.m_low;
}
//erase

static void vli_square(uint64_t *p_result, uint64_t *p_left)
{
    uint128_t r01 = {0, 0};
    uint64_t r2 = 0;
    
    uint i, k;
    for(k=0; k < NUM_ECC_DIGITS*2 - 1; ++k)
    {
        uint l_min = (k < NUM_ECC_DIGITS ? 0 : (k + 1) - NUM_ECC_DIGITS);
        for(i=l_min; i<=k && i<=k-i; ++i)
        {
            uint128_t l_product = mul_64_64(p_left[i], p_left[k-i]);
            if(i < k-i)
            {
                r2 += l_product.m_high >> 63;
                l_product.m_high = (l_product.m_high << 1) | (l_product.m_low >> 63);
                l_product.m_low <<= 1;
            }
            r01 = add_128_128(r01, l_product);
            r2 += (r01.m_high < l_product.m_high);
        }
        p_result[k] = r01.m_low;
        r01.m_low = r01.m_high;
        r01.m_high = r2;
        r2 = 0;
    }
    
    p_result[NUM_ECC_DIGITS*2 - 1] = r01.m_low;
}

#endif /* SUPPORTS_INT128 */

//verify
/* Computes p_result = (p_left + p_right) % p_mod.
   Assumes that p_left < p_mod and p_right < p_mod, p_result != p_mod. */
static void vli_modAdd(uint64_t *p_result, uint64_t *p_left, uint64_t *p_right, uint64_t *p_mod)
{
    uint64_t l_carry = vli_add(p_result, p_left, p_right);
    if(l_carry || vli_cmp(p_result, p_mod) >= 0)
    { /* p_result > p_mod (p_result = p_mod + remainder), so subtract p_mod to get remainder. */
        vli_sub(p_result, p_result, p_mod);
    }
}
//verification
/* Computes p_result = (p_left - p_right) % p_mod.
   Assumes that p_left < p_mod and p_right < p_mod, p_result != p_mod. */
static void vli_modSub(uint64_t *p_result, uint64_t *p_left, uint64_t *p_right, uint64_t *p_mod)
{
    uint64_t l_borrow = vli_sub(p_result, p_left, p_right);
    if(l_borrow)
    { /* In this case, p_result == -diff == (max int) - diff.
         Since -x % d == d - x, we can get the correct result from p_result + p_mod (with overflow). */
        vli_add(p_result, p_result, p_mod);
    }
}

#if ECC_CURVE == secp128r1

/* Computes p_result = p_product % curve_p.
   See algorithm 5 and 6 from http://www.isys.uni-klu.ac.at/PDF/2001-0126-MT.pdf */
static void vli_mmod_fast(uint64_t *p_result, uint64_t *p_product)
{
    uint64_t l_tmp[NUM_ECC_DIGITS];
    int l_carry;
    
    vli_set(p_result, p_product);
    
    l_tmp[0] = p_product[2];
    l_tmp[1] = (p_product[3] & 0x1FFFFFFFFull) | (p_product[2] << 33);
    l_carry = vli_add(p_result, p_result, l_tmp);
    
    l_tmp[0] = (p_product[2] >> 31) | (p_product[3] << 33);
    l_tmp[1] = (p_product[3] >> 31) | ((p_product[2] & 0xFFFFFFFF80000000ull) << 2);
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    l_tmp[0] = (p_product[2] >> 62) | (p_product[3] << 2);
    l_tmp[1] = (p_product[3] >> 62) | ((p_product[2] & 0xC000000000000000ull) >> 29) | (p_product[3] << 35);
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    l_tmp[0] = (p_product[3] >> 29);
    l_tmp[1] = ((p_product[3] & 0xFFFFFFFFE0000000ull) << 4);
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    l_tmp[0] = (p_product[3] >> 60);
    l_tmp[1] = (p_product[3] & 0xFFFFFFFE00000000ull);
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    l_tmp[0] = 0;
    l_tmp[1] = ((p_product[3] & 0xF000000000000000ull) >> 27);
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    while(l_carry || vli_cmp(curve_p, p_result) != 1)
    {
        l_carry -= vli_sub(p_result, p_result, curve_p);
    }
}

#elif ECC_CURVE == secp192r1

/* Computes p_result = p_product % curve_p.
   See algorithm 5 and 6 from http://www.isys.uni-klu.ac.at/PDF/2001-0126-MT.pdf */
static void vli_mmod_fast(uint64_t *p_result, uint64_t *p_product)
{
    uint64_t l_tmp[NUM_ECC_DIGITS];
    int l_carry;
    
    vli_set(p_result, p_product);
    
    vli_set(l_tmp, &p_product[3]);
    l_carry = vli_add(p_result, p_result, l_tmp);
    
    l_tmp[0] = 0;
    l_tmp[1] = p_product[3];
    l_tmp[2] = p_product[4];
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    l_tmp[0] = l_tmp[1] = p_product[5];
    l_tmp[2] = 0;
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    while(l_carry || vli_cmp(curve_p, p_result) != 1)
    {
        l_carry -= vli_sub(p_result, p_result, curve_p);
    }
}

#elif ECC_CURVE == secp256r1
//veriify
/* Computes p_result = p_product % curve_p
   from http://www.nsa.gov/ia/_files/nist-routines.pdf */
static void vli_mmod_fast(uint64_t *p_result, uint64_t *p_product)
{
    uint64_t l_tmp[NUM_ECC_DIGITS];
    int l_carry;
    
    /* t */
    vli_set(p_result, p_product);
    
    /* s1 */
    l_tmp[0] = 0;
    l_tmp[1] = p_product[5] & 0xffffffff00000000ull;
    l_tmp[2] = p_product[6];
    l_tmp[3] = p_product[7];
    l_carry = vli_lshift(l_tmp, l_tmp, 1);
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    /* s2 */
    l_tmp[1] = p_product[6] << 32;
    l_tmp[2] = (p_product[6] >> 32) | (p_product[7] << 32);
    l_tmp[3] = p_product[7] >> 32;
    l_carry += vli_lshift(l_tmp, l_tmp, 1);
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    /* s3 */
    l_tmp[0] = p_product[4];
    l_tmp[1] = p_product[5] & 0xffffffff;
    l_tmp[2] = 0;
    l_tmp[3] = p_product[7];
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    /* s4 */
    l_tmp[0] = (p_product[4] >> 32) | (p_product[5] << 32);
    l_tmp[1] = (p_product[5] >> 32) | (p_product[6] & 0xffffffff00000000ull);
    l_tmp[2] = p_product[7];
    l_tmp[3] = (p_product[6] >> 32) | (p_product[4] << 32);
    l_carry += vli_add(p_result, p_result, l_tmp);
    
    /* d1 */
    l_tmp[0] = (p_product[5] >> 32) | (p_product[6] << 32);
    l_tmp[1] = (p_product[6] >> 32);
    l_tmp[2] = 0;
    l_tmp[3] = (p_product[4] & 0xffffffff) | (p_product[5] << 32);
    l_carry -= vli_sub(p_result, p_result, l_tmp);
    
    /* d2 */
    l_tmp[0] = p_product[6];
    l_tmp[1] = p_product[7];
    l_tmp[2] = 0;
    l_tmp[3] = (p_product[4] >> 32) | (p_product[5] & 0xffffffff00000000ull);
    l_carry -= vli_sub(p_result, p_result, l_tmp);
    
    /* d3 */
    l_tmp[0] = (p_product[6] >> 32) | (p_product[7] << 32);
    l_tmp[1] = (p_product[7] >> 32) | (p_product[4] << 32);
    l_tmp[2] = (p_product[4] >> 32) | (p_product[5] << 32);
    l_tmp[3] = (p_product[6] << 32);
    l_carry -= vli_sub(p_result, p_result, l_tmp);
    
    /* d4 */
    l_tmp[0] = p_product[7];
    l_tmp[1] = p_product[4] & 0xffffffff00000000ull;
    l_tmp[2] = p_product[5];
    l_tmp[3] = p_product[6] & 0xffffffff00000000ull;
    l_carry -= vli_sub(p_result, p_result, l_tmp);
    
    if(l_carry < 0)
    {
        do
        {
            l_carry += vli_add(p_result, p_result, curve_p);
        } while(l_carry < 0);
    }
    else
    {
        while(l_carry || vli_cmp(curve_p, p_result) != 1)
        {
            l_carry -= vli_sub(p_result, p_result, curve_p);
        }
    }
}

#elif ECC_CURVE == secp384r1

static void omega_mult(uint64_t *p_result, uint64_t *p_right)
{
    uint64_t l_tmp[NUM_ECC_DIGITS];
    uint64_t l_carry, l_diff;
    
    /* Multiply by (2^128 + 2^96 - 2^32 + 1). */
    vli_set(p_result, p_right); /* 1 */
    l_carry = vli_lshift(l_tmp, p_right, 32);
    p_result[1 + NUM_ECC_DIGITS] = l_carry + vli_add(p_result + 1, p_result + 1, l_tmp); /* 2^96 + 1 */
    p_result[2 + NUM_ECC_DIGITS] = vli_add(p_result + 2, p_result + 2, p_right); /* 2^128 + 2^96 + 1 */
    l_carry += vli_sub(p_result, p_result, l_tmp); /* 2^128 + 2^96 - 2^32 + 1 */
    l_diff = p_result[NUM_ECC_DIGITS] - l_carry;
    if(l_diff > p_result[NUM_ECC_DIGITS])
    { /* Propagate borrow if necessary. */
        uint i;
        for(i = 1 + NUM_ECC_DIGITS; ; ++i)
        {
            --p_result[i];
            if(p_result[i] != (uint64_t)-1)
            {
                break;
            }
        }
    }
    p_result[NUM_ECC_DIGITS] = l_diff;
}

/* Computes p_result = p_product % curve_p
    see PDF "Comparing Elliptic Curve Cryptography and RSA on 8-bit CPUs"
    section "Curve-Specific Optimizations" */
static void vli_mmod_fast(uint64_t *p_result, uint64_t *p_product)
{
    uint64_t l_tmp[2*NUM_ECC_DIGITS];
     
    while(!vli_isZero(p_product + NUM_ECC_DIGITS)) /* While c1 != 0 */
    {
        uint64_t l_carry = 0;
        uint i;
        
        vli_clear(l_tmp);
        vli_clear(l_tmp + NUM_ECC_DIGITS);
        omega_mult(l_tmp, p_product + NUM_ECC_DIGITS); /* tmp = w * c1 */
        vli_clear(p_product + NUM_ECC_DIGITS); /* p = c0 */
        
        /* (c1, c0) = c0 + w * c1 */
        for(i=0; i<NUM_ECC_DIGITS+3; ++i)
        {
            uint64_t l_sum = p_product[i] + l_tmp[i] + l_carry;
            if(l_sum != p_product[i])
            {
                l_carry = (l_sum < p_product[i]);
            }
            p_product[i] = l_sum;
        }
    }
    
    while(vli_cmp(p_product, curve_p) > 0)
    {
        vli_sub(p_product, p_product, curve_p);
    }
    vli_set(p_result, p_product);
}

#endif
//verification
/* Computes p_result = (p_left * p_right) % curve_p. */
static void vli_modMult_fast(uint64_t *p_result, uint64_t *p_left, uint64_t *p_right)
{
    uint64_t l_product[2 * NUM_ECC_DIGITS];
    vli_mult(l_product, p_left, p_right);
    vli_mmod_fast(p_result, l_product);
}
//verify
/* Computes p_result = p_left^2 % curve_p. */
static void vli_modSquare_fast(uint64_t *p_result, uint64_t *p_left)
{
    uint64_t l_product[2 * NUM_ECC_DIGITS];
    vli_square(l_product, p_left);
    vli_mmod_fast(p_result, l_product);
}
//verify
#define EVEN(vli) (!(vli[0] & 1))
/* Computes p_result = (1 / p_input) % p_mod. All VLIs are the same size.
   See "From Euclid's GCD to Montgomery Multiplication to the Great Divide"
   https://labs.oracle.com/techrep/2001/smli_tr-2001-95.pdf */
 //verification  
static void vli_modInv(uint64_t *p_result, uint64_t *p_input, uint64_t *p_mod)
{
    uint64_t a[NUM_ECC_DIGITS], b[NUM_ECC_DIGITS], u[NUM_ECC_DIGITS], v[NUM_ECC_DIGITS];
    uint64_t l_carry;
    int l_cmpResult;
    
    if(vli_isZero(p_input))
    {
        vli_clear(p_result);
        return;
    }

    vli_set(a, p_input);
    vli_set(b, p_mod);
    vli_clear(u);
    u[0] = 1;
    vli_clear(v);
    
    while((l_cmpResult = vli_cmp(a, b)) != 0)
    {
        l_carry = 0;
        if(EVEN(a))
        {
            vli_rshift1(a);
            if(!EVEN(u))
            {
                l_carry = vli_add(u, u, p_mod);
            }
            vli_rshift1(u);
            if(l_carry)
            {
                u[NUM_ECC_DIGITS-1] |= 0x8000000000000000ull;
            }
        }
        else if(EVEN(b))
        {
            vli_rshift1(b);
            if(!EVEN(v))
            {
                l_carry = vli_add(v, v, p_mod);
            }
            vli_rshift1(v);
            if(l_carry)
            {
                v[NUM_ECC_DIGITS-1] |= 0x8000000000000000ull;
            }
        }
        else if(l_cmpResult > 0)
        {
            vli_sub(a, a, b);
            vli_rshift1(a);
            if(vli_cmp(u, v) < 0)
            {
                vli_add(u, u, p_mod);
            }
            vli_sub(u, u, v);
            if(!EVEN(u))
            {
                l_carry = vli_add(u, u, p_mod);
            }
            vli_rshift1(u);
            if(l_carry)
            {
                u[NUM_ECC_DIGITS-1] |= 0x8000000000000000ull;
            }
        }
        else
        {
            vli_sub(b, b, a);
            vli_rshift1(b);
            if(vli_cmp(v, u) < 0)
            {
                vli_add(v, v, p_mod);
            }
            vli_sub(v, v, u);
            if(!EVEN(v))
            {
                l_carry = vli_add(v, v, p_mod);
            }
            vli_rshift1(v);
            if(l_carry)
            {
                v[NUM_ECC_DIGITS-1] |= 0x8000000000000000ull;
            }
        }
    }
    
    vli_set(p_result, u);
}

/* ------ Point operations ------ */
//erase
/* Returns 1 if p_point is the point at infinity, 0 otherwise. */
/*
static int EccPoint_isZero(EccPoint *p_point)
{
    return (vli_isZero(p_point->x) && vli_isZero(p_point->y));
}
*/
/* Point multiplication algorithm using Montgomery's ladder with co-Z coordinates.
From http://eprint.iacr.org/2011/338.pdf
*/
//verification
/* Double in place */
static void EccPoint_double_jacobian(uint64_t *X1, uint64_t *Y1, uint64_t *Z1)
{
    /* t1 = X, t2 = Y, t3 = Z */
    uint64_t t4[NUM_ECC_DIGITS];
    uint64_t t5[NUM_ECC_DIGITS];
    
    if(vli_isZero(Z1))
    {
        return;
    }
    
    vli_modSquare_fast(t4, Y1);   /* t4 = y1^2 */
    vli_modMult_fast(t5, X1, t4); /* t5 = x1*y1^2 = A */
    vli_modSquare_fast(t4, t4);   /* t4 = y1^4 */
    vli_modMult_fast(Y1, Y1, Z1); /* t2 = y1*z1 = z3 */
    vli_modSquare_fast(Z1, Z1);   /* t3 = z1^2 */
    
    vli_modAdd(X1, X1, Z1, curve_p); /* t1 = x1 + z1^2 */
    vli_modAdd(Z1, Z1, Z1, curve_p); /* t3 = 2*z1^2 */
    vli_modSub(Z1, X1, Z1, curve_p); /* t3 = x1 - z1^2 */
    vli_modMult_fast(X1, X1, Z1);    /* t1 = x1^2 - z1^4 */
    
    vli_modAdd(Z1, X1, X1, curve_p); /* t3 = 2*(x1^2 - z1^4) */
    vli_modAdd(X1, X1, Z1, curve_p); /* t1 = 3*(x1^2 - z1^4) */
    if(vli_testBit(X1, 0))
    {
        uint64_t l_carry = vli_add(X1, X1, curve_p);
        vli_rshift1(X1);
        X1[NUM_ECC_DIGITS-1] |= l_carry << 63;
    }
    else
    {
        vli_rshift1(X1);
    }
    /* t1 = 3/2*(x1^2 - z1^4) = B */
    
    vli_modSquare_fast(Z1, X1);      /* t3 = B^2 */
    vli_modSub(Z1, Z1, t5, curve_p); /* t3 = B^2 - A */
    vli_modSub(Z1, Z1, t5, curve_p); /* t3 = B^2 - 2A = x3 */
    vli_modSub(t5, t5, Z1, curve_p); /* t5 = A - x3 */
    vli_modMult_fast(X1, X1, t5);    /* t1 = B * (A - x3) */
    vli_modSub(t4, X1, t4, curve_p); /* t4 = B * (A - x3) - y1^4 = y3 */
    
    vli_set(X1, Z1);
    vli_set(Z1, Y1);
    vli_set(Y1, t4);
}
//verification
/* Modify (x1, y1) => (x1 * z^2, y1 * z^3) */
static void apply_z(uint64_t *X1, uint64_t *Y1, uint64_t *Z)
{
    uint64_t t1[NUM_ECC_DIGITS];

    vli_modSquare_fast(t1, Z);    /* z^2 */
    vli_modMult_fast(X1, X1, t1); /* x1 * z^2 */
    vli_modMult_fast(t1, t1, Z);  /* z^3 */
    vli_modMult_fast(Y1, Y1, t1); /* y1 * z^3 */
}
//erase
/* P = (x1, y1) => 2P, (x2, y2) => P' */
/*
static void XYcZ_initial_double(uint64_t *X1, uint64_t *Y1, uint64_t *X2, uint64_t *Y2, uint64_t *p_initialZ)
{
    uint64_t z[NUM_ECC_DIGITS];
    
    vli_set(X2, X1);
    vli_set(Y2, Y1);
    
    vli_clear(z);
    z[0] = 1;
    if(p_initialZ)
    {
        vli_set(z, p_initialZ);
    }

    apply_z(X1, Y1, z);
    
    EccPoint_double_jacobian(X1, Y1, z);
    
    apply_z(X2, Y2, z);
}
*/
/* Input P = (x1, y1, Z), Q = (x2, y2, Z)
   Output P' = (x1', y1', Z3), P + Q = (x3, y3, Z3)
   or P => P', Q => P + Q
*/
//verification
static void XYcZ_add(uint64_t *X1, uint64_t *Y1, uint64_t *X2, uint64_t *Y2)
{
    /* t1 = X1, t2 = Y1, t3 = X2, t4 = Y2 */
    uint64_t t5[NUM_ECC_DIGITS];
    
    vli_modSub(t5, X2, X1, curve_p); /* t5 = x2 - x1 */
    vli_modSquare_fast(t5, t5);      /* t5 = (x2 - x1)^2 = A */
    vli_modMult_fast(X1, X1, t5);    /* t1 = x1*A = B */
    vli_modMult_fast(X2, X2, t5);    /* t3 = x2*A = C */
    vli_modSub(Y2, Y2, Y1, curve_p); /* t4 = y2 - y1 */
    vli_modSquare_fast(t5, Y2);      /* t5 = (y2 - y1)^2 = D */
    
    vli_modSub(t5, t5, X1, curve_p); /* t5 = D - B */
    vli_modSub(t5, t5, X2, curve_p); /* t5 = D - B - C = x3 */
    vli_modSub(X2, X2, X1, curve_p); /* t3 = C - B */
    vli_modMult_fast(Y1, Y1, X2);    /* t2 = y1*(C - B) */
    vli_modSub(X2, X1, t5, curve_p); /* t3 = B - x3 */
    vli_modMult_fast(Y2, Y2, X2);    /* t4 = (y2 - y1)*(B - x3) */
    vli_modSub(Y2, Y2, Y1, curve_p); /* t4 = y3 */
    
    vli_set(X2, t5);
}

/* Input P = (x1, y1, Z), Q = (x2, y2, Z)
   Output P + Q = (x3, y3, Z3), P - Q = (x3', y3', Z3)
   or P => P - Q, Q => P + Q
*/
//erase
/*
static void XYcZ_addC(uint64_t *X1, uint64_t *Y1, uint64_t *X2, uint64_t *Y2)
{
    // t1 = X1, t2 = Y1, t3 = X2, t4 = Y2 
    uint64_t t5[NUM_ECC_DIGITS];
    uint64_t t6[NUM_ECC_DIGITS];
    uint64_t t7[NUM_ECC_DIGITS];
    
    vli_modSub(t5, X2, X1, curve_p); // t5 = x2 - x1 
    vli_modSquare_fast(t5, t5);      // t5 = (x2 - x1)^2 = A 
    vli_modMult_fast(X1, X1, t5);    // t1 = x1*A = B 
    vli_modMult_fast(X2, X2, t5);    // t3 = x2*A = C 
    vli_modAdd(t5, Y2, Y1, curve_p); // t4 = y2 + y1 
    vli_modSub(Y2, Y2, Y1, curve_p); // t4 = y2 - y1 

    vli_modSub(t6, X2, X1, curve_p); // t6 = C - B 
    vli_modMult_fast(Y1, Y1, t6);    // t2 = y1 * (C - B) 
    vli_modAdd(t6, X1, X2, curve_p); // t6 = B + C 
    vli_modSquare_fast(X2, Y2);      // t3 = (y2 - y1)^2 
    vli_modSub(X2, X2, t6, curve_p); // t3 = x3 
    
    vli_modSub(t7, X1, X2, curve_p); // t7 = B - x3 
    vli_modMult_fast(Y2, Y2, t7);    // t4 = (y2 - y1)*(B - x3) 
    vli_modSub(Y2, Y2, Y1, curve_p); // t4 = y3 
    
    vli_modSquare_fast(t7, t5);      // t7 = (y2 + y1)^2 = F 
    vli_modSub(t7, t7, t6, curve_p); // t7 = x3' 
    vli_modSub(t6, t7, X1, curve_p); // t6 = x3' - B 
    vli_modMult_fast(t6, t6, t5);    // t6 = (y2 + y1)*(x3' - B) 
    vli_modSub(Y1, t6, Y1, curve_p); // t2 = y3' 
    
    vli_set(X1, t7);
}
*/
//erase
/*
static void EccPoint_mult(EccPoint *p_result, EccPoint *p_point, uint64_t *p_scalar, uint64_t *p_initialZ)
{
    // R0 and R1 
    uint64_t Rx[2][NUM_ECC_DIGITS];
    uint64_t Ry[2][NUM_ECC_DIGITS];
    uint64_t z[NUM_ECC_DIGITS];
    
    int i, nb;
    
    vli_set(Rx[1], p_point->x);
    vli_set(Ry[1], p_point->y);

    XYcZ_initial_double(Rx[1], Ry[1], Rx[0], Ry[0], p_initialZ);

    for(i = vli_numBits(p_scalar) - 2; i > 0; --i)
    {
        nb = !vli_testBit(p_scalar, i);
        XYcZ_addC(Rx[1-nb], Ry[1-nb], Rx[nb], Ry[nb]);
        XYcZ_add(Rx[nb], Ry[nb], Rx[1-nb], Ry[1-nb]);
    }

    nb = !vli_testBit(p_scalar, 0);
    XYcZ_addC(Rx[1-nb], Ry[1-nb], Rx[nb], Ry[nb]);
    
    // Find final 1/Z value. 
    vli_modSub(z, Rx[1], Rx[0], curve_p); // X1 - X0 
    vli_modMult_fast(z, z, Ry[1-nb]);     // Yb * (X1 - X0) 
    vli_modMult_fast(z, z, p_point->x);   // xP * Yb * (X1 - X0) 
    vli_modInv(z, z, curve_p);            // 1 / (xP * Yb * (X1 - X0)) 
    vli_modMult_fast(z, z, p_point->y);   // yP / (xP * Yb * (X1 - X0)) 
    vli_modMult_fast(z, z, Rx[1-nb]);     // Xb * yP / (xP * Yb * (X1 - X0)) 
    // End 1/Z calculation 

    XYcZ_add(Rx[nb], Ry[nb], Rx[1-nb], Ry[1-nb]);
    
    apply_z(Rx[0], Ry[0], z);
    
    vli_set(p_result->x, Rx[0]);
    vli_set(p_result->y, Ry[0]);
}
*/
//erase
/*
static void ecc_bytes2native(uint64_t p_native[NUM_ECC_DIGITS], const uint8_t p_bytes[ECC_BYTES])
{
    unsigned i;
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_bytes + 8 * (NUM_ECC_DIGITS - 1 - i);
        p_native[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }
}


//erase
static void ecc_native2bytes(uint8_t p_bytes[ECC_BYTES], const uint64_t p_native[NUM_ECC_DIGITS])
{
    unsigned i;
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        uint8_t *p_digit = p_bytes + 8 * (NUM_ECC_DIGITS - 1 - i);
        p_digit[0] = p_native[i] >> 56;
        p_digit[1] = p_native[i] >> 48;
        p_digit[2] = p_native[i] >> 40;
        p_digit[3] = p_native[i] >> 32;
        p_digit[4] = p_native[i] >> 24;
        p_digit[5] = p_native[i] >> 16;
        p_digit[6] = p_native[i] >> 8;
        p_digit[7] = p_native[i];
    }
}
*/
//erase
/* Compute a = sqrt(a) (mod curve_p). */
/*
static void mod_sqrt(uint64_t a[NUM_ECC_DIGITS])
{
    unsigned i;
    uint64_t p1[NUM_ECC_DIGITS] = {1};
    uint64_t l_result[NUM_ECC_DIGITS] = {1};
    
    // Since curve_p == 3 (mod 4) for all supported curves, we can
       compute sqrt(a) = a^((curve_p + 1) / 4) (mod curve_p). 
    vli_add(p1, curve_p, p1); // p1 = curve_p + 1 
    for(i = vli_numBits(p1) - 1; i > 1; --i)
    {
        vli_modSquare_fast(l_result, l_result);
        if(vli_testBit(p1, i))
        {
            vli_modMult_fast(l_result, l_result, a);
        }
    }
    vli_set(a, l_result);
}
*/
//erase
/*
static void ecc_point_decompress(EccPoint *p_point, const uint8_t p_compressed[ECC_BYTES+1])
{
    uint64_t _3[NUM_ECC_DIGITS] = {3}; // -a = 3 
    ecc_bytes2native(p_point->x, p_compressed+1);
    
    vli_modSquare_fast(p_point->y, p_point->x); // y = x^2 
    vli_modSub(p_point->y, p_point->y, _3, curve_p); // y = x^2 - 3 
    vli_modMult_fast(p_point->y, p_point->y, p_point->x); // y = x^3 - 3x 
    vli_modAdd(p_point->y, p_point->y, curve_b, curve_p); // y = x^3 - 3x + b 
    
    mod_sqrt(p_point->y);
    
    if((p_point->y[0] & 0x01) != (p_compressed[0] & 0x01))
    {
        vli_sub(p_point->y, curve_p, p_point->y);
    }
}
*/
/*
int ecc_make_key(uint8_t p_publicKey[ECC_BYTES+1], uint8_t p_privateKey[ECC_BYTES])
{
    uint64_t l_private[NUM_ECC_DIGITS];
    EccPoint l_public;
    unsigned l_tries = 0;
    
    do
    {
        if(!getRandomNumber(l_private) || (l_tries++ >= MAX_TRIES))
        {
            return 0;
        }
        if(vli_isZero(l_private))
        {
            continue;
        }
    
        // Make sure the private key is in the range [1, n-1].
         //  For the supported curves, n is always large enough that we only need to subtract once at most. 
        if(vli_cmp(curve_n, l_private) != 1)
        {
            vli_sub(l_private, l_private, curve_n);
        }

        EccPoint_mult(&l_public, &curve_G, l_private, NULL);
    } while(EccPoint_isZero(&l_public));
    
    ecc_native2bytes(p_privateKey, l_private);
    ecc_native2bytes(p_publicKey + 1, l_public.x);
    p_publicKey[0] = 2 + (l_public.y[0] & 0x01);
    return 1;
}
*/
//erase
/*
int ecdh_shared_secret(const uint8_t p_publicKey[ECC_BYTES+1], const uint8_t p_privateKey[ECC_BYTES], uint8_t p_secret[ECC_BYTES])
{
    EccPoint l_public;
    uint64_t l_private[NUM_ECC_DIGITS];
    uint64_t l_random[NUM_ECC_DIGITS];
    
    if(!getRandomNumber(l_random))
    {
        return 0;
    }
    
    ecc_point_decompress(&l_public, p_publicKey);
    ecc_bytes2native(l_private, p_privateKey);
    
    EccPoint l_product;
    EccPoint_mult(&l_product, &l_public, l_private, l_random);
    
    ecc_native2bytes(p_secret, l_product.x);
    
    return !EccPoint_isZero(&l_product);
}
*/
/* -------- ECDSA code -------- */
//verification
/* Computes p_result = (p_left * p_right) % p_mod. */
static void vli_modMult(uint64_t *p_result, uint64_t *p_left, uint64_t *p_right, uint64_t *p_mod)
{
    uint64_t l_product[2 * NUM_ECC_DIGITS];
    uint64_t l_modMultiple[2 * NUM_ECC_DIGITS];
    uint l_digitShift, l_bitShift;
    uint l_productBits;
    uint l_modBits = vli_numBits(p_mod);
    
    vli_mult(l_product, p_left, p_right);
    l_productBits = vli_numBits(l_product + NUM_ECC_DIGITS);
    if(l_productBits)
    {
        l_productBits += NUM_ECC_DIGITS * 64;
    }
    else
    {
        l_productBits = vli_numBits(l_product);
    }
    
    if(l_productBits < l_modBits)
    { /* l_product < p_mod. */
        vli_set(p_result, l_product);
        return;
    }
    
    /* Shift p_mod by (l_leftBits - l_modBits). This multiplies p_mod by the largest
       power of two possible while still resulting in a number less than p_left. */
    vli_clear(l_modMultiple);
    vli_clear(l_modMultiple + NUM_ECC_DIGITS);
    l_digitShift = (l_productBits - l_modBits) / 64;
    l_bitShift = (l_productBits - l_modBits) % 64;
    if(l_bitShift)
    {
        l_modMultiple[l_digitShift + NUM_ECC_DIGITS] = vli_lshift(l_modMultiple + l_digitShift, p_mod, l_bitShift);
    }
    else
    {
        vli_set(l_modMultiple + l_digitShift, p_mod);
    }

    /* Subtract all multiples of p_mod to get the remainder. */
    vli_clear(p_result);
    p_result[0] = 1; /* Use p_result as a temp var to store 1 (for subtraction) */
    while(l_productBits > NUM_ECC_DIGITS * 64 || vli_cmp(l_modMultiple, p_mod) >= 0)
    {
        int l_cmp = vli_cmp(l_modMultiple + NUM_ECC_DIGITS, l_product + NUM_ECC_DIGITS);
        if(l_cmp < 0 || (l_cmp == 0 && vli_cmp(l_modMultiple, l_product) <= 0))
        {
            if(vli_sub(l_product, l_product, l_modMultiple))
            { /* borrow */
                vli_sub(l_product + NUM_ECC_DIGITS, l_product + NUM_ECC_DIGITS, p_result);
            }
            vli_sub(l_product + NUM_ECC_DIGITS, l_product + NUM_ECC_DIGITS, l_modMultiple + NUM_ECC_DIGITS);
        }
        uint64_t l_carry = (l_modMultiple[NUM_ECC_DIGITS] & 0x01) << 63;
        vli_rshift1(l_modMultiple + NUM_ECC_DIGITS);
        vli_rshift1(l_modMultiple);
        l_modMultiple[NUM_ECC_DIGITS-1] |= l_carry;
        
        --l_productBits;
    }
    vli_set(p_result, l_product);
}
//verification
static uint umax(uint a, uint b)
{
    return (a > b ? a : b);
}
/*
int ecdsa_sign(const uint8_t p_privateKey[ECC_BYTES], const uint8_t p_hash[ECC_BYTES], uint8_t p_signature[ECC_BYTES*2])
{
    uint64_t k[NUM_ECC_DIGITS];
    uint64_t l_tmp[NUM_ECC_DIGITS];
    uint64_t l_s[NUM_ECC_DIGITS];
    EccPoint p;
    unsigned l_tries = 0;
    
    do
    {
        if(!getRandomNumber(k) || (l_tries++ >= MAX_TRIES))
        {
            return 0;
        }
        if(vli_isZero(k))
        {
            continue;
        }
    
        if(vli_cmp(curve_n, k) != 1)
        {
            vli_sub(k, k, curve_n);
        }
    
        // tmp = k * G 
        EccPoint_mult(&p, &curve_G, k, NULL);
    
        // r = x1 (mod n) 
        if(vli_cmp(curve_n, p.x) != 1)
        {
            vli_sub(p.x, p.x, curve_n);
        }
    } while(vli_isZero(p.x));

    ecc_native2bytes(p_signature, p.x);
    
    ecc_bytes2native(l_tmp, p_privateKey);
    vli_modMult(l_s, p.x, l_tmp, curve_n); // s = r*d 
    ecc_bytes2native(l_tmp, p_hash);
    vli_modAdd(l_s, l_tmp, l_s, curve_n); // s = e + r*d 
    vli_modInv(k, k, curve_n); // k = 1 / k 
    vli_modMult(l_s, l_s, k, curve_n); // s = (e + r*d) / k 
    ecc_native2bytes(p_signature + ECC_BYTES, l_s);
    
    return 1;
}
*/
//u1 = converted hash
//l_r, l_s = signature x, y in sgx
//l_public = public key 
//verification

static void sgx_convert_bytes2native(uint64_t u1[NUM_ECC_DIGITS], uint64_t l_r[NUM_ECC_DIGITS], \
uint64_t l_s[NUM_ECC_DIGITS], EccPoint *l_public, const sgx_ec256_public_t p_publicKey,\
const sgx_sha256_hash_t p_hash, const sgx_ec256_signature_t p_signature)
{
    unsigned i;
    //public key conversion
     for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_publicKey.gx + 8 * (NUM_ECC_DIGITS - 1 - i);
        l_public->x[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }
    //public key conversion
     for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_publicKey.gy + 8 * (NUM_ECC_DIGITS - 1 - i);
        l_public->y[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }
    //hash conversion
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_hash + 8 * (NUM_ECC_DIGITS - 1 - i);
        u1[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }

    //signature conversion
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint32_t *p_digit = p_signature.x + 2 * (NUM_ECC_DIGITS - 1 - i);
        l_r[i] = ((uint64_t)p_digit[0] << 32) | ((uint64_t)p_digit[1]);
    }
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint32_t *p_digit = p_signature.y + 2 * (NUM_ECC_DIGITS - 1 - i);
        l_s[i] = ((uint64_t)p_digit[0] << 32) | ((uint64_t)p_digit[1]);
    }
}
/*
//u1 = converted hash
//l_r, l_s = signature x, y in sgx
//l_public = public key 
static void sgx_convert_bytes2native2(uint64_t u1[NUM_ECC_DIGITS], uint64_t l_r[NUM_ECC_DIGITS], \
uint64_t l_s[NUM_ECC_DIGITS], EccPoint *l_public, const sgx_ec256_public_t p_publicKey,\
const sgx_sha256_hash_t p_hash, const sgx_ec256_signature_t p_signature)
{
    unsigned i;
    //public key conversion
     for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_publicKey.gx + 8 * (i);
        l_public->x[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }
    //public key conversion
     for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_publicKey.gy + 8 * (i);
        l_public->y[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }
    //hash conversion
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_hash + 8 * (i);
        u1[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }

    //signature conversion
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint32_t *p_digit = p_signature.x + 2 * (i);
        l_r[i] = ((uint64_t)p_digit[0] << 32) | ((uint64_t)p_digit[1]);
    }
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint32_t *p_digit = p_signature.y + 2 * (i);
        l_s[i] = ((uint64_t)p_digit[0] << 32) | ((uint64_t)p_digit[1]);
    }
}

//u1 = converted hash
//l_r, l_s = signature x, y in sgx
//l_public = public key 
static void sgx_convert_bytes2native3(uint64_t u1[NUM_ECC_DIGITS], uint64_t l_r[NUM_ECC_DIGITS], \
uint64_t l_s[NUM_ECC_DIGITS], EccPoint *l_public, const sgx_ec256_public_t p_publicKey,\
const sgx_sha256_hash_t p_hash, const sgx_ec256_signature_t p_signature)
{
    unsigned i;
    //public key conversion
     for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_publicKey.gx + 8 * (NUM_ECC_DIGITS - 1 - i);
        l_public->x[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }
    //public key conversion
     for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_publicKey.gy + 8 * (NUM_ECC_DIGITS - 1 - i);
        l_public->y[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }
    //hash conversion
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint8_t *p_digit = p_hash + 8 * (i);
        u1[i] = ((uint64_t)p_digit[0] << 56) | ((uint64_t)p_digit[1] << 48) | ((uint64_t)p_digit[2] << 40) | ((uint64_t)p_digit[3] << 32) |
            ((uint64_t)p_digit[4] << 24) | ((uint64_t)p_digit[5] << 16) | ((uint64_t)p_digit[6] << 8) | (uint64_t)p_digit[7];
    }

    //signature conversion
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint32_t *p_digit = p_signature.x + 2 * (i);
        l_r[i] = ((uint64_t)p_digit[0] << 32) | ((uint64_t)p_digit[1]);
    }
    for(i=0; i<NUM_ECC_DIGITS; ++i)
    {
        const uint32_t *p_digit = p_signature.y + 2 * (i);
        l_s[i] = ((uint64_t)p_digit[0] << 32) | ((uint64_t)p_digit[1]);
    }
}
*/
int ecdsa_verify(sgx_ec256_public_t p_publicKey, sgx_sha256_hash_t p_hash, sgx_ec256_signature_t p_signature)
//int ecdsa_verify(const uint8_t p_publicKey[ECC_BYTES+1], const uint8_t p_hash[ECC_BYTES], const uint8_t p_signature[ECC_BYTES*2])
{
    uint64_t u1[NUM_ECC_DIGITS], u2[NUM_ECC_DIGITS];
    uint64_t z[NUM_ECC_DIGITS];
    EccPoint l_public, l_sum;
    uint64_t rx[NUM_ECC_DIGITS];
    uint64_t ry[NUM_ECC_DIGITS];
    uint64_t tx[NUM_ECC_DIGITS];
    uint64_t ty[NUM_ECC_DIGITS];
    uint64_t tz[NUM_ECC_DIGITS];
    
    uint64_t l_r[NUM_ECC_DIGITS], l_s[NUM_ECC_DIGITS];

    int i,j;
    //ecc_point_decompress(&l_public, p_publicKey);
    //ecc_bytes2native(l_r, p_signature);
    //ecc_bytes2native(l_s, p_signature + ECC_BYTES);
    
    //reverse all key (not hash!-why? i don't now..)
    int temp;
    uint32_t tp;
    for (i=0; i<SGX_ECP256_KEY_SIZE/2; i++)
    {
        temp = p_publicKey.gx[i];
        p_publicKey.gx[i] = p_publicKey.gx[SGX_ECP256_KEY_SIZE-1-i];
        p_publicKey.gx[SGX_ECP256_KEY_SIZE-1-i] = temp;
    }
    for (i=0; i<SGX_ECP256_KEY_SIZE/2; i++)
    {
        temp = p_publicKey.gy[i];
        p_publicKey.gy[i] = p_publicKey.gy[SGX_ECP256_KEY_SIZE-1-i];
        p_publicKey.gy[SGX_ECP256_KEY_SIZE-1-i] = temp;
    }

    /* 주석이 옳아!!! comment is right!!!!!!!!!!!
    for (i=0; i<SGX_SHA256_HASH_SIZE/2; i++)
    {
        temp = p_hash[i];
        p_hash[i] = p_hash[SGX_SHA256_HASH_SIZE-1-i];
        p_hash[SGX_SHA256_HASH_SIZE-1-i] = temp;
    }
    */
    for (i=0; i<SGX_NISTP_ECP256_KEY_SIZE/2; i++)
    {
        temp = p_signature.x[i];
        p_signature.x[i] = p_signature.x[SGX_NISTP_ECP256_KEY_SIZE-1-i];
        p_signature.x[SGX_NISTP_ECP256_KEY_SIZE-1-i] = temp;
        
        
    }    

    for (i=0; i<SGX_NISTP_ECP256_KEY_SIZE/2; i++)
    {
        temp = p_signature.y[i];
        p_signature.y[i] = p_signature.y[SGX_NISTP_ECP256_KEY_SIZE-1-i];
        p_signature.y[SGX_NISTP_ECP256_KEY_SIZE-1-i] = temp;
    }
    
    /* 이런짓 안하는게 맞다는걸 알게됨. comment is right
        printf("signature x : \n");
    for(i=0; i<SGX_NISTP_ECP256_KEY_SIZE; i++)
    {
        tp = ((p_signature.x[i] & 0xff000000)>>24) | \
              ((p_signature.x[i] & 0x00ff0000)>>8) | \
              ((p_signature.x[i] & 0x0000ff00)<<8) | \
              ((p_signature.x[i] & 0x000000ff)<<24);  
        p_signature.x[i] = tp;
        printf("0x%x, ",p_signature.x[i]);    
    }
    printf("\n");
    for(i=0; i<SGX_NISTP_ECP256_KEY_SIZE; i++)
    {
        tp = ((p_signature.y[i] & 0xff000000)>>24) | \
              ((p_signature.y[i] & 0x00ff0000)>>8) | \
              ((p_signature.y[i] & 0x0000ff00)<<8) | \
              ((p_signature.y[i] & 0x000000ff)<<24);  
        p_signature.y[i] = tp;
    }
*/

    //sgx code (convert datas)
    sgx_convert_bytes2native(u1, l_r, l_s, &l_public, p_publicKey, p_hash, p_signature);
   /*
    printf("hash\n");
    for(i=0; i<NUM_ECC_DIGITS; i++)
        printf("0x%llx, ", u1[i]);
    printf("\npublic key\nx : ");
    for(i=0; i<NUM_ECC_DIGITS; i++)
        printf("0x%llx, ", l_public.x[i]);
    printf("\ny : ");
    for(i=0; i<NUM_ECC_DIGITS; i++)
        printf("0x%llx, ", l_public.y[i]);
    printf("\nsignature\nx: ");    
    for(i=0; i<NUM_ECC_DIGITS; i++)
        printf("0x%llx, ", l_r[i]);
    printf("\ny : ");
    for(i=0; i<NUM_ECC_DIGITS; i++)
        printf("0x%llx, ", l_s[i]);
    printf("\n");
    */
    if(vli_isZero(l_r) || vli_isZero(l_s))
    { /* r, s must not be 0. */
       // printf("error1\n");
        return 0;
    }
    
    if(vli_cmp(curve_n, l_r) != 1 || vli_cmp(curve_n, l_s) != 1)
    { /* r, s must be < n. */
       // printf("error2\n");
        return 0;
    }

    /* Calculate u1 and u2. */
    vli_modInv(z, l_s, curve_n); /* Z = s^-1 */
    //ecc_bytes2native(u1, p_hash);
    vli_modMult(u1, u1, z, curve_n); /* u1 = e/s */
    vli_modMult(u2, l_r, z, curve_n); /* u2 = r/s */
    
    /* Calculate l_sum = G + Q. */
    vli_set(l_sum.x, l_public.x);
    vli_set(l_sum.y, l_public.y);
    vli_set(tx, curve_G.x);
    vli_set(ty, curve_G.y);
    vli_modSub(z, l_sum.x, tx, curve_p); /* Z = x2 - x1 */
    XYcZ_add(tx, ty, l_sum.x, l_sum.y);
    vli_modInv(z, z, curve_p); /* Z = 1/Z */
    apply_z(l_sum.x, l_sum.y, z);
    
    /* Use Shamir's trick to calculate u1*G + u2*Q */
    EccPoint *l_points[4] = {NULL, &curve_G, &l_public, &l_sum};
    uint l_numBits = umax(vli_numBits(u1), vli_numBits(u2));
    
    EccPoint *l_point = l_points[(!!vli_testBit(u1, l_numBits-1)) | ((!!vli_testBit(u2, l_numBits-1)) << 1)];
    vli_set(rx, l_point->x);
    vli_set(ry, l_point->y);
    vli_clear(z);
    z[0] = 1;

    
    for(i = l_numBits - 2; i >= 0; --i)
    {
        EccPoint_double_jacobian(rx, ry, z);
        
        int l_index = (!!vli_testBit(u1, i)) | ((!!vli_testBit(u2, i)) << 1);
        EccPoint *l_point = l_points[l_index];
        if(l_point)
        {
            vli_set(tx, l_point->x);
            vli_set(ty, l_point->y);
            apply_z(tx, ty, z);
            vli_modSub(tz, rx, tx, curve_p); /* Z = x2 - x1 */
            XYcZ_add(tx, ty, rx, ry);
            vli_modMult_fast(z, z, tz);
        }
    }

    vli_modInv(z, z, curve_p); /* Z = 1/Z */
    apply_z(rx, ry, z);
    
    /* v = x1 (mod n) */
    if(vli_cmp(curve_n, rx) != 1)
    {
        vli_sub(rx, rx, curve_n);
    }
    //printf("last\n");

    /* Accept only if v == r. */
    return (vli_cmp(rx, l_r) == 0);
}

void ecdsa_test()
{
    uint8_t text[] = "Hello world!!\n";
	//SHA256_CTX ctx;
    //int i;
    sgx_ec256_public_t p_publicKey;
    sgx_ec256_signature_t p_signature;
    sgx_ec256_private_t p_privateKey;
 
    sgx_sha256_hash_t p_hash;
    sgx_sha256_hash_t p_hash2 = {0x11, 0x7f, 0xfc, 0xa3, 0x60, 0x29, 0xeb, 0xdc, 0x8d, 0xed, 0x1e, 0x27, 0xed, 0xc4, 0x29, 0xf8, 0xea, 0x17, 0xc7, 0x51, 0x7a, 0xd7, 0x32, 0x91, 0x4b, 0x72, 0xbc, 0x66, 0x2e, 0xe9, 0x42, 0x4e};
   
  //  sgx_sha256_hash_t p_hash = {0x11, 0x7f, 0xfc, 0xa3, 0x60, 0x29, 0xeb, 0xdc, 0x8d, 0xed, 0x1e, 0x27, 0xed, 0xc4, 0x29, 0xf8, 0xea, 0x17, 0xc7, 0x51, 0x7a, 0xd7, 0x32, 0x91, 0x4b, 0x72, 0xbc, 0x66, 0x2e, 0xe9, 0x42, 0x4e};
    uint8_t gx[SGX_ECP256_KEY_SIZE] = {
        0xbd, 0x76, 0xb8, 0x22, 0x34, 0xd1, 0x69, 0xb, 0x57, 0xa1, 0x1, 0xb3, 0x8d, 0xa8, 0x19, 0x59, 0xb0, 0x83, 0x5f, 0xa8, 0x88, 0xdc, 0x83, 0xe9, 0x2f, 0x8b, 0x34, 0xa8, 0x47, 0x52, 0xc, 0xc6
    };
    uint8_t gy[SGX_ECP256_KEY_SIZE] = { 
      0x49, 0x30, 0x84, 0xe7, 0x7a, 0x31, 0x9c, 0x98, 0x7d, 0xae, 0xfb, 0xa4, 0xe5, 0x85, 0xaf, 0x8a, 0x70, 0xc5, 0x79, 0xde, 0x78, 0xff, 0xd0, 0xc4, 0xb4, 0x28, 0xba, 0x7a, 0x59, 0xd5, 0x6d, 0xce
    };
    uint32_t x[SGX_NISTP_ECP256_KEY_SIZE]={
        0xa26d5f7c, 0x714fa352, 0x5a97c23f, 0xa6528921, 0xca26e7c2, 0xdaf0df50, 0x1d81264f, 0xeefaeb4c
        };
    uint32_t y[SGX_NISTP_ECP256_KEY_SIZE]={
        0x29fecc67, 0xf04cd792, 0x4b0309b, 0xe9ce9746, 0x6309c23e, 0x16c9b694, 0xa50735ad, 0xaef180b8
        };
 
    uint8_t r[SGX_ECP256_KEY_SIZE] = {0xa0, 0xa4, 0xca, 0x42, 0x3c, 0xae, 0x5a, 0x34, 0x44, 0x22, 0xed, 0x27, 0x2c, 0x15, 0xa6, 0xec, 0xad, 0x9b, 0xa7, 0x94, 0xeb, 0x8e, 0x14, 0xe5, 0x10, 0xa5, 0x40, 0xfd, 0x7a, 0xdb, 0xc9, 0x7d};
    int i, temp;
    int verify;
    memcpy(p_publicKey.gx, gx, sizeof(gx));
    memcpy(p_publicKey.gy, gy, sizeof(gy));
    memcpy(p_signature.x, x, sizeof(x));
    memcpy(p_signature.y, y, sizeof(y));
    memcpy(p_privateKey.r, r, sizeof(r));
   // p_publickey.gx = gx;
   // p/_public
    sgx_ecdsa_verify(text, p_publicKey, p_signature, p_privateKey);
    
}

uint8_t sgx_ecdsa_verify(uint8_t *text, sgx_ec256_public_t p_publicKey,\
sgx_ec256_signature_t p_signature, sgx_ec256_private_t p_privateKey)
{
//ecdsa_sign(const uint8_t p_privateKey.r, const uint8_t p_hash[ECC_BYTES], uint8_t p_signature[ECC_BYTES*2])
    //for(i=0; i<)
    sgx_sha256_hash_t p_hash;
    SHA256_CTX ctx;
    int i;
    uint8_t verify;

 ptimer_start();
           
    sha256_init(&ctx);
    sha256_update(&ctx, text, 15);
	sha256_final(&ctx, p_hash);
ptimer_stop_and_uart_print();
    uart_printf("hash : ");
    for(i=0; i<SGX_ECP256_KEY_SIZE; i++)
    {
        uart_printf("0x%x, ",p_hash[i]);
    }
    uart_printf("\n");
   // printf("SHA-256 tests: %s\n",  !memcmp(p, buf, SHA256_BLOCK_SIZE) ? "SUCCEEDED" : "FAILED");
 
    for(i=0; i<10000; i++){

        uart_printf("%d : ",i);
       ptimer_start();
    
        verify = ecdsa_verify(p_publicKey, p_hash, p_signature);
        ptimer_stop_and_uart_print();
    }
    if(verify)
        uart_printf("Verification is SUCCESS!!!!\n");
    else
        uart_printf("Verfication is failed......\n");
  //  printf("verfiy? : %d\n", verify);
    return verify;
}