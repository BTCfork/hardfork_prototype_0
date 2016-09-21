/* HFP0 POW begin: added file from satoshisbitcoin */
/*-
 * Copyright 2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */

#include <stdint.h>
#include <string.h>

#include "../uint256.h"
#include "../util.h"

//#include "sha256.h"
#include "sysendian.h"

#include "modified_scrypt_sha256.h"
#include "modified_scrypt_smix.h"

inline void blkcpy(void *, const void *, size_t);
inline void blkxor(void *, const void *, size_t);
inline void salsa20_8(uint32_t[16]);
inline void blockmix_salsa8(const uint32_t *, uint32_t *, uint32_t *);
inline uint64_t integerify(const void *);


inline void
blkcpy(void * dest, const void * src, size_t len)
{
	size_t * D = (size_t *)dest;
	const size_t * S = (const size_t *)src;
	size_t L = len / sizeof(size_t);
	size_t i;

	for (i = 0; i < L; i++)
		D[i] = S[i];
}

inline void
blkxor(void * dest, const void * src, size_t len)
{
	size_t * D = (size_t *)dest;
	const size_t * S = (const size_t *)src;
	size_t L = len / sizeof(size_t);
	size_t i;

	for (i = 0; i < L; i++)
		D[i] ^= S[i];
}

/**
 * salsa20_8(B):
 * Apply the salsa20/8 core to the provided block.
 */
inline void
salsa20_8(uint32_t B[16])
{
	uint32_t x[16];
	size_t i;

	blkcpy(x, B, 64);
	for (i = 0; i < 8; i += 2) {
#define R(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
		/* Operate on columns. */
		x[ 4] ^= R(x[ 0]+x[12], 7);  x[ 8] ^= R(x[ 4]+x[ 0], 9);
		x[12] ^= R(x[ 8]+x[ 4],13);  x[ 0] ^= R(x[12]+x[ 8],18);

		x[ 9] ^= R(x[ 5]+x[ 1], 7);  x[13] ^= R(x[ 9]+x[ 5], 9);
		x[ 1] ^= R(x[13]+x[ 9],13);  x[ 5] ^= R(x[ 1]+x[13],18);

		x[14] ^= R(x[10]+x[ 6], 7);  x[ 2] ^= R(x[14]+x[10], 9);
		x[ 6] ^= R(x[ 2]+x[14],13);  x[10] ^= R(x[ 6]+x[ 2],18);

		x[ 3] ^= R(x[15]+x[11], 7);  x[ 7] ^= R(x[ 3]+x[15], 9);
		x[11] ^= R(x[ 7]+x[ 3],13);  x[15] ^= R(x[11]+x[ 7],18);

		/* Operate on rows. */
		x[ 1] ^= R(x[ 0]+x[ 3], 7);  x[ 2] ^= R(x[ 1]+x[ 0], 9);
		x[ 3] ^= R(x[ 2]+x[ 1],13);  x[ 0] ^= R(x[ 3]+x[ 2],18);

		x[ 6] ^= R(x[ 5]+x[ 4], 7);  x[ 7] ^= R(x[ 6]+x[ 5], 9);
		x[ 4] ^= R(x[ 7]+x[ 6],13);  x[ 5] ^= R(x[ 4]+x[ 7],18);

		x[11] ^= R(x[10]+x[ 9], 7);  x[ 8] ^= R(x[11]+x[10], 9);
		x[ 9] ^= R(x[ 8]+x[11],13);  x[10] ^= R(x[ 9]+x[ 8],18);

		x[12] ^= R(x[15]+x[14], 7);  x[13] ^= R(x[12]+x[15], 9);
		x[14] ^= R(x[13]+x[12],13);  x[15] ^= R(x[14]+x[13],18);
#undef R
	}
	for (i = 0; i < 16; i++)
		B[i] += x[i];
}

/**
 * blockmix_salsa8(Bin, Bout, X):
 * Compute Bout = BlockMix_{salsa20/8}(Bin).  The input Bin must be 128
 * bytes in length; the output Bout must also be the same size.  The
 * temporary space X must be 64 bytes.
 */
inline void
blockmix_salsa8(const uint32_t * Bin, uint32_t * Bout, uint32_t * X)
{
	size_t i;
	size_t r = 1; // Using r=1, let the compiler optimize it out rather than doing by hand

	/* 1: X <-- B_{2r - 1} */
	blkcpy(X, &Bin[(2 * r - 1) * 16], 64);

	/* 2: for i = 0 to 2r - 1 do */
	for (i = 0; i < 2 * r; i += 2) {
		/* 3: X <-- H(X \xor B_i) */
		blkxor(X, &Bin[i * 16], 64);
		salsa20_8(X);

		/* 4: Y_i <-- X */
		/* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
		blkcpy(&Bout[i * 8], X, 64);

		/* 3: X <-- H(X \xor B_i) */
		blkxor(X, &Bin[i * 16 + 16], 64);
		salsa20_8(X);

		/* 4: Y_i <-- X */
		/* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
		blkcpy(&Bout[i * 8 + r * 16], X, 64);
	}
}

/**
 * integerify(B, r):
 * Return the result of parsing B_{2r-1} as a little-endian integer.
 */
inline uint64_t
integerify(const void * B)
{
        size_t r = 1; // Using r=1, let the compiler optimize it out, rather than by hand
	const uint32_t * X = (const uint32_t *)((const void *)((uintptr_t)(B) + (2 * r - 1) * 64));

	return (((uint64_t)(X[1]) << 32) + X[0]);
}

/**
 * crypto_scrypt_smix(B, N, V, XY):
 * Compute B = SMix_r(B, N).  The input B must be 128 bytes in length;
 * the temporary storage V must be 128N bytes in length; the temporary
 * storage XY must be 256 + 64 bytes in length.  The value N must be a
 * power of 2 greater than 1.  The arrays B, V, and XY must be aligned to a
 * multiple of 64 bytes.
 */
void
crypto_scrypt_smix(uint8_t * B, uint64_t N, void * _V, void * XY)
{
	size_t r = 1; // Using r=1, let the compiler optimize r out rather than by hand

	uint32_t * X = (uint32_t *)XY;
	uint32_t * Y = (uint32_t *)((void *)((uint8_t *)(XY) + 128 * r));
	uint32_t * Z = (uint32_t *)((void *)((uint8_t *)(XY) + 256 * r));
	uint32_t * V = (uint32_t *)_V;
	uint64_t i;
	uint64_t j;
	size_t k;
	//uint256  Random_CachelineLow;
	//uint256  Random_CachelineHigh;
	uint64_t Random_Cacheline_Position;
	uint64_t Random_DataMixer[8];

	/* 1: X <-- B */
	for (k = 0; k < 32 * r; k++)
		X[k] = le32dec(&B[4 * k]);

	// Explaination of scrypt modifications
	//
	// Script tries to be ASIC & GPU resistant by using a memory size that cannot fit onto a single CMOS chip.
	//   However even though memory reads are performed off-chip the reads themselves are sequential, this enables
	//   a hardware implementation to create a data pipeline where on each clock cycle the next memory address is
	//   read and data is pipelined through an ASIC/FPGA implementation.
	//
	// This version of scrypt adds a sequence of reads that are random, unpredicable and dynamically change with
	//   each hash. As a result it is not possible to create a data pipeline where data is processed on each clock
	//   cycle. Instead computation must follow the following steps:
	//
	//   a) Compute next address to read from
	//   b) Send read request to memory address
	//   c) Wait 100s of clock cycles
	//   d) Receive single data packet
	//   e) Perform a very simple computation in a few (<10) clock cycles
	//   f) Write result back to memory and use as next address to read from
	//   g) Send next read request to next memory address
	//   h) Wait 100s of clock cycles
	//   --- Repeat billions of times ---
	//
	// The end result is a computation that cannot be parallelized (the steps must be performed sequentially)
	//   and which has very limited gains when implemented in an ASIC/FPGA. An ASIC/FPGA implementation will
	//   likely only be marginally faster than a simple CPU core and likely not worth the engineering overhead
	//   cost. GPU implementations are also prevented since there is no parallelization available. Additionally
	//   the benefits of modern CPU advances such as Out-of-Order execution are also reduced or eliminated.
	//   Here the out-of-order execution engine in a high-end CPU will be unable to perform additional
	//   optimizations since the critical path is physical data movement between a simple computation that takes
	//   very few clock cycles and waiting on memory for 100s of clock cycles.
	//
	// If the value of mining this algorithm is high enough it will obviously be optimized as much as possible.
	//   However the optimial design will likely be a standard light-weight CPU core connected to DRAM. This
	//   is a common non-speciallized configuration and should be easily available. Additionally unlike SHA256
	//   mining which is a concentrated logic core and dominated by electricity costs, this modified scrypt
	//   algorithm will use much less electricty as a component of the total cost of operation since most
	//   hardware will usually be idle and waiting for data without consuming electricty.
	//
	// The standard scrypt algorithm consists of 2 sequential passes over the memory data set:
	//   1) The first pass initializes memory sequentially
	//   2) The second pass performs a moderately complex operation on each data chunk randomly
	//
	// This modified version consists of 2 passes as follows:
	//   1)   The first pass initializes memory sequentially (same as above, needed for initialization)
	//   2-3) The next 2 passes perform simple computation on random address regions, as described above
	//   4)  The tenth pass performs the same operation as in pass 2 in standard scrypt
	//
	// The end result is a computation that scans an off-chip memory data set 2 times, with 25% of the scans
	//   happening sequentially (and thus are pipeline-able) and 75% of the scans are fully random and
	//   unpredicable. As a result creating a hardware pipeline for the first initialization pass can only
	//   improve performance by 25% at the limit, and likely much less. A GPU could in theory work on pass
	//   1 in parallel, but again the speed-up is bounded.
	//
	// The configuration selected is 128MB and 2 added random passes for 3-out-of-4 passes being random, which
	//   requies 1-2 seconds to compute per hash. It turns out the bitcoin client internally computes the hash
	//   several times for each block found and received throughout the code as part of it's checking. This means
	//   new blocks require 10-20 seconds to process today, which is too much. Rather than weaken the Hash algorithm
	//   the code should be improved to reduce the number of times the block hash is re-computed to check validity.
	//
	// If over time the configuration is optimized to a GPU or ASIC, this branch will change the PoW by doing one
	//   or both of the following: a) Increase the memory size, b) Increase the number of light-weight random
	//   passes (i.e. increase from 2 passes to 8). With a large enough memory footprint the middle set of passes
	//   should keep a light-weight CPU core as the most cost effective implementation.

	/*** Pass 1 - Same as standard scrypt Pass 1 ***/
	/* 2: for i = 0 to N - 1 do */
	for (i = 0; i < N; i += 2) {
		/* 3: V_i <-- X */
		blkcpy(&V[i * (32 * r)], X, 128 * r);

		/* 4: X <-- H(X) */
		blockmix_salsa8(X, Y, Z);

		/* 3: V_i <-- X */
		blkcpy(&V[(i + 1) * (32 * r)], Y, 128 * r);

		/* 4: X <-- H(X) */
		blockmix_salsa8(Y, X, Z);
	}

	/*** Passes 2-3 - New random and simple operations over the dataset 2 times ***/

	//Random_CachelineLow  = ((uint256 *)Z)[0]; // uint256 has no ^ operator.... using uint64_t instead
	//Random_CachelineHigh = ((uint256 *)Z)[1];

	Random_Cacheline_Position = *((uint64_t *)X);
	Random_DataMixer[0]       = *((uint64_t *)(&Y[0]));
	Random_DataMixer[1]       = *((uint64_t *)(&Y[1*2]));
	Random_DataMixer[2]       = *((uint64_t *)(&Y[2*2]));
	Random_DataMixer[3]       = *((uint64_t *)(&Y[3*2]));
	Random_DataMixer[4]       = *((uint64_t *)(&Y[4*2]));
	Random_DataMixer[5]       = *((uint64_t *)(&Y[5*2]));
	Random_DataMixer[6]       = *((uint64_t *)(&Y[6*2]));
	Random_DataMixer[7]       = *((uint64_t *)(&Y[7*2]));

	for( i = 0; i < 2; i++ ) {  // 0 to 7 (there are 8 full passes)

	    for( j = 0; j < N*2; j++ ) {  // 0 to 2*N-1  (there are 2*N cachelines)

	        //Random_Cacheline_Position = (*((uint64_t *)(&Random_CachelineLow))) & 0x000fffff;
		//Random_CachelineLow  ^= *((uint256 *)(&V[Random_Cacheline_Position*16]));
		//Random_CachelineHigh ^= *((uint256 *)(&V[Random_Cacheline_Position*16 + 8]));
		//*((uint256 *)(&V[Random_Cacheline_Position*16]))      = Random_CachelineLow;
		//*((uint256 *)(&V[Random_Cacheline_Position*16 + 8])) = Random_CachelineHigh;

	        Random_Cacheline_Position ^= Random_DataMixer[0];
		Random_Cacheline_Position  = Random_Cacheline_Position & 0x00000000001fffffULL;  // 0 to 2*N-1  (there are 2*N cachelines)

		Random_DataMixer[0] ^= *((uint64_t *)(&V[Random_Cacheline_Position*16]));
		Random_DataMixer[1] ^= *((uint64_t *)(&V[Random_Cacheline_Position*16 + 1*2]));
		Random_DataMixer[2] ^= *((uint64_t *)(&V[Random_Cacheline_Position*16 + 2*2]));
		Random_DataMixer[3] ^= *((uint64_t *)(&V[Random_Cacheline_Position*16 + 3*2]));
		Random_DataMixer[4] ^= *((uint64_t *)(&V[Random_Cacheline_Position*16 + 4*2]));
		Random_DataMixer[5] ^= *((uint64_t *)(&V[Random_Cacheline_Position*16 + 5*2]));
		Random_DataMixer[6] ^= *((uint64_t *)(&V[Random_Cacheline_Position*16 + 6*2]));
		Random_DataMixer[7] ^= *((uint64_t *)(&V[Random_Cacheline_Position*16 + 7*2]));

	        *((uint64_t *)(&V[Random_Cacheline_Position*16]))       = Random_DataMixer[7];
	        *((uint64_t *)(&V[Random_Cacheline_Position*16 + 1*2])) = Random_DataMixer[6];
	        *((uint64_t *)(&V[Random_Cacheline_Position*16 + 2*2])) = Random_DataMixer[5];
	        *((uint64_t *)(&V[Random_Cacheline_Position*16 + 3*2])) = Random_DataMixer[4];
	        *((uint64_t *)(&V[Random_Cacheline_Position*16 + 4*2])) = Random_DataMixer[3];
	        *((uint64_t *)(&V[Random_Cacheline_Position*16 + 5*2])) = Random_DataMixer[2];
	        *((uint64_t *)(&V[Random_Cacheline_Position*16 + 6*2])) = Random_DataMixer[1];
	        *((uint64_t *)(&V[Random_Cacheline_Position*16 + 7*2])) = Random_DataMixer[0];

	    }

	}

	/*** Pass 4 - Same as standard scrypt Pass 2 ***/
	/* 6: for i = 0 to N - 1 do */
	for (i = 0; i < N; i += 2) {
		/* 7: j <-- Integerify(X) mod N */
		j = integerify(X) & (N - 1);

		/* 8: X <-- H(X \xor V_j) */
		blkxor(X, &V[j * (32 * r)], 128 * r);
		blockmix_salsa8(X, Y, Z);

		/* 7: j <-- Integerify(X) mod N */
		j = integerify(Y) & (N - 1);

		/* 8: X <-- H(X \xor V_j) */
		blkxor(Y, &V[j * (32 * r)], 128 * r);
		blockmix_salsa8(Y, X, Z);
	}

	/* 10: B' <-- X */
	for (k = 0; k < 32 * r; k++)
		le32enc(&B[4 * k], X[k]);
}

int
crypto_1M_1_1_256_scrypt(const uint8_t * passwd, size_t passwdlen,
              void * V0, uint8_t * buf, size_t buflen )
{
	uint8_t * B;
	uint32_t * V;
	uint32_t * XY;
	size_t r = 1, p = 1;  // Not using r and p in this version, let compiler optimize out
	uint64_t N = 1024*1024; // 1024*1024=1M count * 128 = 128MB memory space
	uint32_t i;

	/* Align to 64-byte cachelines */
	char B0[128 + 63];
	B = (uint8_t *)(((uintptr_t)(B0) + 63) & ~ (uintptr_t)(63));
	char XY0[256 + 64 + 63];
	XY = (uint32_t *)(((uintptr_t)(XY0) + 63) & ~ (uintptr_t)(63));

	V = (uint32_t *)(((uintptr_t)(V0) + 63) & ~ (uintptr_t)(63));


	/* 1: (B_0 ... B_{p-1}) <-- PBKDF2(P, S, 1, p * MFLen) */
	PBKDF2_SHA256(passwd, passwdlen, passwd, passwdlen, 1, B, p * 128 * r);

	/* 2: for i = 0 to p - 1 do */
	for (i = 0; i < p; i++) {
		/* 3: B_i <-- MF(B_i, N) */
		crypto_scrypt_smix(&B[i * 128 * r], N, V, XY);
	}

	/* 5: DK <-- PBKDF2(P, B, 1, dkLen) */
	PBKDF2_SHA256(passwd, passwdlen, B, p * 128 * r, 1, buf, buflen);

	/* Success! */
	return (0);

}
/* HFP0 POW satoshisbitcoin */
