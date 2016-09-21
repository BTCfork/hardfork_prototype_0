// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// HFP0 BSZ import file from BitPay adaptive block size patch
#ifndef blocksizecalculator_h
#define blocksizecalculator_h

#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include "streams.h"
#include "main.h"
#include "util.h"
#include "consensus/consensus.h"
#include "chain.h"
#include "clientversion.h"

using namespace std;

namespace BlockSizeCalculator {
    // HFP0 BSZ begin
    // change from BitPay: got rid of default values, they interfere with unit test
    // where the lookback window needs to be set smaller sometimes.
    unsigned int ComputeBlockSize(CBlockIndex*, unsigned int pastblocks );
    inline unsigned int GetMedianBlockSize(CBlockIndex*, unsigned int pastblocks );
    inline std::vector<unsigned int> GetBlockSizes(CBlockIndex*, unsigned int pastblocks );
    // HFP0 BSZ end
    inline int GetBlockSize(CBlockIndex*);
    // HFP0 BSZ begin: added
    uint32_t ComputeScaledBlockMaxSize(uint32_t nBlockSizeMax, unsigned int maxBlockSize);
    void ClearBlockSizes(); // added for unit tests - have to clear out window sometimes
    // HFP0 BSZ end
}
#endif
