/*********************************************************************
* Filename:   sha256.h
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Defines the API for the corresponding SHA1 implementation.
*********************************************************************/

#ifndef SHA256_H
#define SHA256_H

#include "jasmine.h"
/*************************** HEADER FILES ***************************/
#include <stddef.h>

#define HASH_BLOCK_SIZE (512/8)
#define HASHED_OUTPUT SHA256_BLOCK_SIZE
//#define DATA_BUFFERLEN (4096+512) //8KB가 온다고 가정합니다. 근데 사실 더 크겠죠? (파라미터떔시 일단은 일케 가정ㅎ마)
#define KEY_SIZE (16) // key size : 128bit 


/****************************** MACROS ******************************/
#define SHA256_BLOCK_SIZE 32            // SHA256 outputs a 32 byte digest

/**************************** DATA TYPES ****************************/
typedef unsigned char BYTE;             // 8-bit byte
typedef unsigned int  WORD;             // 32-bit word, change to "long" for 16-bit machines

typedef struct {
	BYTE data[64];
	WORD datalen;
	unsigned long long bitlen;
	WORD state[8];
} SHA256_CTX;

/*********************** FUNCTION DECLARATIONS **********************/
void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const BYTE data[], size_t len);
void sha256_final(SHA256_CTX *ctx, BYTE hash[]);
void SHA_test();

void Hash(const unsigned char data[],const int data_len, unsigned char h_mac[], SHA256_CTX* ctx);
void HMAC(const unsigned char key[], unsigned char h_mac[], const unsigned char text[], const int text_size);
void hmac_test();

#endif   // SHA256_H
