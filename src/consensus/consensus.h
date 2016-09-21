// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include "uint256.h"    // HFP0 DIF added

/** HFP0 POW begin: add compile flag. By default POW is not changed - the compile process can override this. */
#ifndef HFP0_POW
#define HFP0_POW       0
#endif
/** HFP0 POW end */

/** HFP0 DBG begin: add flags to enable compilation of verbose debugging outputs */
#define HFP0_DEBUG_BSZ 0     // block size (adaptive)
#define HFP0_DEBUG_FRK 0     // fork logic
#define HFP0_DEBUG_DIF 0     // difficulty
#define HFP0_DEBUG_POW 0     // proof-of-work
#define HFP0_DEBUG_PER 0     // peer handling (disconnect, peers file management)
#define HFP0_DEBUG_SED 0     // static seeds (DNS and IP/onion)
#define HFP0_DEBUG_ALR 0     // alert system
#define HFP0_DEBUG_XTB 0     // Xtreme Thinblocks
#define HFP0_DEBUG_CLT 0     // BIP65 OP_CHECKLOCKTIMEVERIFY
#define HFP0_DEBUG_RLT 0     // BIP68 Relative Lock Time
#define HFP0_DEBUG_CSV 0     // BIP112 CHECKSEQUENCEVERIFY
#define HFP0_DEBUG_MTP 0     // BIP113 Median time-past
/** HFP0 DBG end */

/** Enforced block size limit, post-HFP0 fork.
    Dynamic algorithm will be capped here. */
static const unsigned int MAX_BLOCK_SIZE = 4000000;   // bytes
/** The old block size limit */
static const unsigned int OLD_MAX_BLOCK_SIZE = 1000000;
/** HFP0 FRK begin */
/** fork heights and difficulty limits after activation */
static const unsigned int SIZE_FORK_HEIGHT_MAINNET = 666666;
static const unsigned int SIZE_FORK_HEIGHT_TESTNET = 9999999;
/** fork height on regtest set to 1000 because some tests play around to just
    under that height, and have to use old block versions (2,3,4) */
static const unsigned int SIZE_FORK_HEIGHT_REGTEST = 1000;
/** HFP0 FRK end */
/** HFP0 DIF begin */
/** At fork time, the difficulties are reset to these limits.
    HFP0 DIF, POW TODO: POW- and non-POW variants might require different POW limits
                               at fork time to get similar avg. block times. */
static const uint256 POW_LIMIT_FORK_MAINNET = uint256S("00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
static const uint256 POW_LIMIT_FORK_TESTNET = uint256S("00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
static const uint256 POW_LIMIT_FORK_REGTEST = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
/** HFP0 DIF END */
/** limit on signature operations in a block */
// HFP0 BSZ, CLN begin: rename this to indicate how it is computed, introduce constant for divisor
static const unsigned int BLOCK_TO_SIGOPS_DIVISOR = 50;
static const unsigned int SIGOPS_TO_STANDARD_TX_DIVISOR = 5;
static const unsigned int MAX_BLOCK_SIGOPS = MAX_BLOCK_SIZE / BLOCK_TO_SIGOPS_DIVISOR;
static const unsigned int OLD_MAX_BLOCK_SIGOPS = OLD_MAX_BLOCK_SIZE / 50;
// HFP0 BSZ, CLN end
/** limit on number of bytes hashed to compute signatures in a block */
// HFP0 BSZ: decided not to raise this limit at this stage.
static const unsigned int MAX_BLOCK_SIGHASH = 1300 * 1000 * 1000; // 1.3 gigabytes
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

/** HFP0 BSZ begin */
/** The maximum allowed multiple for the computed block size */
static const unsigned int MAX_BLOCK_SIZE_INCREASE_MULTIPLE = 2;
/** The number of blocks to consider in the computation of median block size */
static const unsigned int NUM_BLOCKS_FOR_MEDIAN_BLOCK = 14 * 24 * 6;  // two weeks, 2016 blocks
// HFP0 BSZ: declared the new variables external here, and moved their definitions to main.cpp / bitcoin-tx.cpp
// This is to prevent conflicting instances of the global variables in code modules which include this header,
/** The maximum allowed size for a serialized block, in bytes (network rule) */
extern unsigned int maxBlockSize;
/** The maximum allowed number of signature check operations in a block (network rule) */
extern unsigned int maxBlockSigops;
/** The maximum number of sigops we're willing to relay/mine in a single tx */
extern unsigned int maxStandardTxSigops;
/** The number of blocks to look back. Static variable to allow unit testing user smaller lookback. */
#ifndef blocksizecalculator_h
extern unsigned int median_block_lookback;
#endif
/** HFP0 BSZ end */
/** HFP0 RLT (BIP68) begin */
/** Flags for nSequence and nLockTime locks */
enum {
    /* Interpret sequence numbers as relative lock-time constraints. */
    LOCKTIME_VERIFY_SEQUENCE = (1 << 0),
/** HFP0 RLT (BIP68) end */

    /* Use GetMedianTimePast() instead of nTime for end point timestamp. */
    LOCKTIME_MEDIAN_TIME_PAST = (1 << 1),
};

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
