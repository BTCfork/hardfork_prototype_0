/* HFP0 POW begin: added file from satoshisbitcoin */
#ifndef _CRYPTO_SCRYPT_SMIX_H_
#define _CRYPTO_SCRYPT_SMIX_H_

/**
 * crypto_scrypt_smix(B, r, N, V, XY):
 * Compute B = SMix_r(B, N).  The input B must be 128r bytes in length;
 * the temporary storage V must be 128rN bytes in length; the temporary
 * storage XY must be 256r + 64 bytes in length.  The value N must be a
 * power of 2 greater than 1.  The arrays B, V, and XY must be aligned to a
 * multiple of 64 bytes.
 * variable r was removed, used as r=1 only
 */
void crypto_scrypt_smix(uint8_t *, uint64_t, void *, void *);

/**
 * crypto_scrypt(passwd, passwdlen, salt, saltlen, N, r, p, buf, buflen):
 * Compute scrypt(passwd[0 .. passwdlen - 1], salt[0 .. saltlen - 1], N, r,
 * p, buflen) and write the result into buf.  The parameters r, p, and buflen
 * must satisfy r * p < 2^30 and buflen <= (2^32 - 1) * 32.  The parameter N
 * must be a power of 2 greater than 1.
 *
 * Return 0 on success; or -1 on error.
 */
int crypto_1M_1_1_256_scrypt(const uint8_t *, size_t, void *, uint8_t *, size_t);

#endif /* !_CRYPTO_SCRYPT_SMIX_H_ */
/* HFP0 POW end */
