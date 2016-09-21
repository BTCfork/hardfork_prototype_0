// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"

// HFP0 POW begin: safety
#include "consensus/consensus.h"
#ifndef HFP0_POW
#error HFP0_POW not defined!
#endif
// HFP0 POW end
#if HFP0_POW
// HFP0 POW begin
#include "util.h"                              // for LogPrintf

static std::map<uint256,uint256> hashCache;    // A cache of past modified scrypt hashes performed

uint256 CBlockHeader::GetHash(bool useCache) const
{
    uint256 key, returnHash;
    uint32_t * keyBegin, * hashPrevBlockBegin, * hashMerkleRootBegin;

    // HFP0 FRK begin: check block version to decide which POW hash to apply
    if ((uint32_t)nVersion < (BASE_VERSION + FULL_FORK_VERSION_MIN) || (uint32_t)nVersion > (BASE_VERSION + FULL_FORK_VERSION_MAX)) {
        // if not a HFP0 block, return old hash type
        return SerializeHash(*this);
    }
    // HFP0 FRK end

    // Due to the long hash runtime and the fact that bitcoind requests the
    // same hash multiple times during a block validation, cache previous
    // hashes and return the cached result if available
    if ( useCache ) {
        // Build the search key
        keyBegin            = (uint32_t *)key.begin();
        hashPrevBlockBegin  = (uint32_t *)hashPrevBlock.begin();
        hashMerkleRootBegin = (uint32_t *)hashMerkleRoot.begin();
        for( int i = 0; i < 8; i++ )
            keyBegin[i] = hashPrevBlockBegin[i] ^ hashMerkleRootBegin[i];
        keyBegin[0] ^= nVersion;
        keyBegin[1] ^= nTime;
        keyBegin[2] ^= nBits;
        keyBegin[3] ^= nNonce;

        std::map<uint256,uint256>::iterator search = hashCache.find(key);
        if(search != hashCache.end()) {
#if HFP0_DEBUG_POW
            // HFP0 DBG begin
            LogPrintf("HFP0 POW GetHash(): Cache hit for %s\n", search->second.GetHex().c_str());
            // HFP0 DBG end
#endif
            return search->second;   // Cache hit
        }
    }
    // No cache hit, compute the hash
    returnHash = HashModifiedScrypt(this);

    // Store the hash in the cache
    if( useCache ) {
#if HFP0_DEBUG_POW
        // HFP0 DBG begin
        LogPrintf("HFP0 POW GetHash(): Cache miss - adding %s\n", returnHash.GetHex().c_str());
        // HFP0 DBG end
#endif
        hashCache.insert( std::pair<uint256,uint256>(key,returnHash) );
    }

    return returnHash;
}
// HFP0 POW end
#else
uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}
#endif

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    return s.str();
}
