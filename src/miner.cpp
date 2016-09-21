// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "hash.h"
#include "main.h"
#include "net.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#include "blocksizecalculator.h"   // HFP0 BSZ

#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>
#include <queue>

using namespace std;

//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;

class ScoreCompare
{
public:
    ScoreCompare() {}

    bool operator()(const CTxMemPool::txiter a, const CTxMemPool::txiter b)
    {
        return CompareTxMemPoolEntryByScore()(*b,*a); // Convert to less than
    }
};

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}


CBlockTemplate* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn)
{
    // Create new block
    auto_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if(!pblocktemplate.get())
        return NULL;
    CBlock *pblock = &pblocktemplate->block; // pointer for convenience
    ValidationCostTracker resourceTracker(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max());

    // Create coinbase tx
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;

    // Add dummy coinbase tx as first transaction
    pblock->vtx.push_back(CTransaction());
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    // Collect memory pool transactions into the block
    CTxMemPool::setEntries inBlock;
    CTxMemPool::setEntries waitSet;

    // This vector will be sorted into a priority queue:
    vector<TxCoinAgePriority> vecPriority;
    TxCoinAgePriorityCompare pricomparer;
    std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash> waitPriMap;
    typedef std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash>::iterator waitPriIter;
    double actualPriority = -1;

    std::priority_queue<CTxMemPool::txiter, std::vector<CTxMemPool::txiter>, ScoreCompare> clearedTxs;
    bool fPrintPriority = GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    uint64_t nBlockSize = 1000;
    uint64_t nBlockTx = 0;
    unsigned int nBlockSigOps = 100;
    int lastFewTxs = 0;
    CAmount nFees = 0;

    {
        LOCK2(cs_main, mempool.cs);
        CBlockIndex* pindexPrev = chainActive.Tip();
        const int nHeight = pindexPrev->nHeight + 1;
        pblock->nTime = GetAdjustedTime();
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

        pblock->nVersion = BASE_VERSION;

        // HFP0 CLN removed Classic 2MB fork bit setting and expiration

        // HFP0 FRK begin
        if (nHeight >= Params().GetConsensus().nHFP0ActivateSizeForkHeight) {
            pblock->nVersion |= FULL_FORK_VERSION_CUR;
            // disable 2MB voting bit after forked, for later repurposing
            pblock->nVersion &= ~FORK_BIT_2MB;

            // HFP0 BSZ begin
            UpdateAdaptiveBlockSizeVars(pindexPrev);
#if HFP0_DEBUG_BSZ
            // HFP0 DBG begin
            LogPrintf("HFP0 BSZ: CreateNewBlock raw: maxBlockSize = ComputeBlockSize() = %u\n", maxBlockSize);
            LogPrintf("HFP0 BSZ: CreateNewBlock raw: maxBlockSigops = %u at address %p\n", maxBlockSigops, &maxBlockSigops);
            LogPrintf("HFP0 BSZ: CreateNewBlock raw: maxStandardTxSigops = %u\n", maxStandardTxSigops);
            // HFP0 DBG end
#endif
            // HFP0 BSZ end
        }
        // HFP0 FRK end

        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        if (Params().MineBlocksOnDemand())
            pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

        UpdateTime(pblock, Params().GetConsensus(), pindexPrev);

        // HFP0 BSZ begin
        // consensus limit changed to depend on fork height, not fork activation time
        uint32_t nConsensusMaxSize = MaxBlockSize(nHeight);
        // Largest block you're willing to create, defaults to being the biggest possible.
        // Miners can adjust downwards (soft limit) if they wish to throttle their blocks, for instance,
        // to work around high orphan rates or other scaling problems.
        uint32_t nBlockMaxSize = (uint32_t) GetArg("-blockmaxsize", nConsensusMaxSize);
        unsigned int nMaxBlockSigops = maxBlockSigops;

#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: CreateNewBlock: nConsensusMaxSize from MaxBlockSize() = %u\n", nConsensusMaxSize);
        LogPrintf("HFP0 BSZ: CreateNewBlock: nBlockMaxSize (soft limit) from settings (default to nConsensusMaxSize if not set): %u\n", nBlockMaxSize);
        // HFP0 DBG end
#endif
        // HFP0 FRK begin
        if (nHeight >= Params().GetConsensus().nHFP0ActivateSizeForkHeight) {
            // Check if miner wants to scale the mined/created block size when the max block size changes
            unsigned int fScaleBlockSizeOptions = GetArg("-scaleblocksizeoptions", DEFAULT_SCALE_BLOCK_SIZE_OPTIONS);

            if (fScaleBlockSizeOptions) {
                // scale soft limit according to how much computed adaptive block size exceeds floor of 2MB
                // HFP0 BSZ TODO: implement proper scaling - the function below is still stubbed!
                nBlockMaxSize = BlockSizeCalculator::ComputeScaledBlockMaxSize(nBlockMaxSize, maxBlockSize);
#if HFP0_DEBUG_BSZ
                // HFP0 DBG begin
                LogPrintf("HFP0 BSZ: CreateNewBlock: fork,scale=%d: nBlockMaxSize after scaling = %u\n", fScaleBlockSizeOptions, nBlockMaxSize);
                // HFP0 DBG end
#endif
                maxBlockSize = std::max((unsigned int)1000, std::min((unsigned int)(nBlockMaxSize-1000), maxBlockSize));

#if HFP0_DEBUG_BSZ
                // HFP0 DBG begin
                LogPrintf("HFP0 BSZ: CreateNewBlock: fork,scale=%d: maxBlockSize after limiting = %u\n", fScaleBlockSizeOptions, maxBlockSize);
                // HFP0 DBG end
#endif
            }
            else {
                // fork but no scaling of soft limit
                maxBlockSize = std::max((unsigned int)1000, std::min((unsigned int)(nBlockMaxSize-1000), maxBlockSize));
            }
            // maxBlockSize has been set - now recompute others
            maxBlockSigops = maxBlockSize / BLOCK_TO_SIGOPS_DIVISOR;
            maxStandardTxSigops = maxBlockSigops / SIGOPS_TO_STANDARD_TX_DIVISOR;

#if HFP0_DEBUG_BSZ
            // HFP0 DBG begin
            LogPrintf("HFP0 BSZ: CreateNewBlock: fork,scale=%d: maxBlockSize = %u\n", fScaleBlockSizeOptions, maxBlockSize);
            LogPrintf("HFP0 BSZ: CreateNewBlock: fork,scale=%d: maxBlockSigops = %u\n", fScaleBlockSizeOptions, maxBlockSigops);
            LogPrintf("HFP0 BSZ: CreateNewBlock: fork,scale=%d: maxStandardTxSigops = %u\n", fScaleBlockSizeOptions, maxStandardTxSigops);
            // HFP0 DBG end
#endif

            // Limit to between 1K and (maxBlockSize-1K for sanity:
            nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(maxBlockSize-1000), nBlockMaxSize));
            nMaxBlockSigops = nBlockMaxSize / BLOCK_TO_SIGOPS_DIVISOR;

            // HFP0 BSZ TODO: check if maxStandardTxSigops is used downstream from this function, if not it might not need to be adjusted
            maxStandardTxSigops = nBlockMaxSize / 5;

#if HFP0_DEBUG_BSZ
            // HFP0 DBG begin
            LogPrintf("HFP0 BSZ: CreateNewBlock: fork,scale=%d: nBlockMaxSize after limiting = %u\n", fScaleBlockSizeOptions, nBlockMaxSize);
            LogPrintf("HFP0 BSZ: CreateNewBlock: fork,scale=%d: nMaxBlockSigops after limiting = %u\n", fScaleBlockSizeOptions, nMaxBlockSigops);
            // HFP0 DBG end
#endif
        }
        // HFP0 FRK end
        // HFP0 BSZ end

        // How much of the block should be dedicated to high-priority transactions,
        // included regardless of the fees they pay
        unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
        nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

        // Minimum block size you want to create; block will be filled with free transactions
        // until there are no more or the block reaches this size:
        unsigned int nBlockMinSize = GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
        nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

        int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                ? nMedianTimePast
                                : pblock->GetBlockTime();

        bool fPriorityBlock = nBlockPrioritySize > 0;
        if (fPriorityBlock) {
            vecPriority.reserve(mempool.mapTx.size());
            for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
                 mi != mempool.mapTx.end(); ++mi)
            {
                double dPriority = mi->GetPriority(nHeight);
                CAmount dummy;
                mempool.ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
                vecPriority.push_back(TxCoinAgePriority(dPriority, mi));
            }
            std::make_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
        }

        CTxMemPool::indexed_transaction_set::nth_index<3>::type::iterator mi = mempool.mapTx.get<3>().begin();
        CTxMemPool::txiter iter;

        // HFP0 BSZ, CLN removed obsolete nMaxLegacySigops evaluation

#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        // track loop exit code to assist in debugging
        unsigned int loop_exit_code = 0;  // 0 is natural end, all the breaks will set a unique code to identify the reason...
        // HFP0 DBG end
#endif
        while (mi != mempool.mapTx.get<3>().end() || !clearedTxs.empty())
        {
            bool priorityTx = false;
            if (fPriorityBlock && !vecPriority.empty()) { // add a tx from priority queue to fill the blockprioritysize
                priorityTx = true;
                iter = vecPriority.front().second;
                actualPriority = vecPriority.front().first;
                std::pop_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                vecPriority.pop_back();
            }
            else if (clearedTxs.empty()) { // add tx with next highest score
                iter = mempool.mapTx.project<0>(mi);
                mi++;
            }
            else {  // try to add a previously postponed child tx
                iter = clearedTxs.top();
                clearedTxs.pop();
            }

            if (inBlock.count(iter))
                continue; // could have been added to the priorityBlock

            const CTransaction& tx = iter->GetTx();

            bool fOrphan = false;
            BOOST_FOREACH(CTxMemPool::txiter parent, mempool.GetMemPoolParents(iter))
            {
                if (!inBlock.count(parent)) {
                    fOrphan = true;
#if HFP0_DEBUG_BSZ
                    // HFP0 DBG begin
                    loop_exit_code = 1;
                    // HFP0 DBG end
#endif
                    break;
                }
            }
            if (fOrphan) {
                if (priorityTx)
                    waitPriMap.insert(std::make_pair(iter,actualPriority));
                else
                    waitSet.insert(iter);
                continue;
            }

            unsigned int nTxSize = iter->GetTxSize();
            if (fPriorityBlock &&
                (nBlockSize + nTxSize >= nBlockPrioritySize || !AllowFree(actualPriority))) {
                fPriorityBlock = false;
                waitPriMap.clear();
            }
            if (!priorityTx &&
                (iter->GetModifiedFee() < ::minRelayTxFee.GetFee(nTxSize) && nBlockSize >= nBlockMinSize)) {
#if HFP0_DEBUG_BSZ
                // HFP0 DBG begin
                loop_exit_code = 2;
                // HFP0 DBG end
#endif
                break;
            }
            if (nBlockSize + nTxSize >= nBlockMaxSize) {
                if (nBlockSize >  nBlockMaxSize - 100 || lastFewTxs > 50) {
#if HFP0_DEBUG_BSZ
                    // HFP0 DBG begin
                    loop_exit_code = 3;
                    // HFP0 DBG end
#endif
                    break;
                }
                // Once we're within 1000 bytes of a full block, only look at 50 more txs
                // to try to fill the remaining space.
                if (nBlockSize > nBlockMaxSize - 1000) {
                    lastFewTxs++;
                }
                continue;
            }

            if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
                continue;

            unsigned int nTxSigOps = iter->GetSigOpCount();
            // HFP0 BSZ begin
            // (replaced nMaxLegacySigops by maxBlockSigops)

#if HFP0_DEBUG_BSZ
            // HFP0 DBG begin
            LogPrintf("HFP0 BSZ: CreateNewBlock: maxBlockSigops = %u at address %p\n", maxBlockSigops, &maxBlockSigops);
            LogPrintf("HFP0 BSZ: CreateNewBlock: nMaxBlockSigops = %u\n", nMaxBlockSigops);
            // HFP0 DBG end
#endif

            if (nBlockSigOps + nTxSigOps >= nMaxBlockSigops) {
                if (nBlockSigOps > nMaxBlockSigops - 2) {
#if HFP0_DEBUG_BSZ
                    // HFP0 DBG begin
                    loop_exit_code = 4;
                    // HFP0 DBG end
#endif
                    break;
                }
                continue;
            }
            // HFP0 BSZ end

            CAmount nTxFees = iter->GetFee();
            // Added
            pblock->vtx.push_back(tx);
            pblocktemplate->vTxFees.push_back(nTxFees);
            pblocktemplate->vTxSigOps.push_back(nTxSigOps);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;

            if (fPrintPriority)
            {
                double dPriority = iter->GetPriority(nHeight);
                CAmount dummy;
                mempool.ApplyDeltas(tx.GetHash(), dPriority, dummy);
                LogPrintf("priority %.1f fee %s txid %s\n",
                          dPriority , CFeeRate(iter->GetModifiedFee(), nTxSize).ToString(), tx.GetHash().ToString());
            }

            inBlock.insert(iter);

            // Add transactions that depend on this one to the priority queue
            BOOST_FOREACH(CTxMemPool::txiter child, mempool.GetMemPoolChildren(iter))
            {
                if (fPriorityBlock) {
                    waitPriIter wpiter = waitPriMap.find(child);
                    if (wpiter != waitPriMap.end()) {
                        vecPriority.push_back(TxCoinAgePriority(wpiter->second,child));
                        std::push_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                        waitPriMap.erase(wpiter);
                    }
                }
                else {
                    if (waitSet.count(child)) {
                        clearedTxs.push(child);
                        waitSet.erase(child);
                    }
                }
            }
        }
#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: CreateNewBlock() while loop exit code: %d\n", loop_exit_code);
        // HFP0 DBG end
#endif

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        LogPrintf("CreateNewBlock(): total size %u txs: %u fees: %ld sigops %d\n", nBlockSize, nBlockTx, nFees, nBlockSigOps);

        // Compute final coinbase transaction.
        txNew.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());
        txNew.vin[0].scriptSig = CScript() << nHeight << OP_0;
        pblock->vtx[0] = txNew;
        pblocktemplate->vTxFees[0] = -nFees;

        // Fill in header
        pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
        pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
        pblock->nNonce         = 0;
        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0]);

        CValidationState state;
        if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
#if HFP0_DEBUG_BSZ
            // HFP0 DBG begin
            LogPrintf("HFP0 BSZ: CreateNewBlock(): TestBlockValidity failed: %s\n", FormatStateMessage(state));
            // HFP0 DBG end
#endif
            throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
        }
    }

    return pblocktemplate.release();
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = txCoinbase;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//

//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
//
#if HFP0_POW
// HFP0 POW begin
bool static ScanHash(CBlockHeader *pblock, uint32_t& nNonce, uint256 *phash, arith_uint256 hashTarget)
{
    while (true) {
        nNonce++;
        pblock->nNonce = nNonce;
        *phash = pblock->GetHash(false);   // false means do not use cache
        // Return the nonce if it is below the hashTarget
        if (UintToArith256(*phash) <= hashTarget)
            return true;
        // If nothing found after trying for 16 hashes return failed, rebuild a new block and try again
        // Using smaller number of hashes to try at once due to longer hashing times
        if ( (nNonce & 0x0000000f) == 0)
            return false;
        if (shutdownAllMinerThreads)
            return false;

        // Allow thread to pass control (each hash takes ~1 sec)
        MilliSleep(0);
    }
}
// HFP0 POW end
#else
bool static ScanHash(const CBlockHeader *pblock, uint32_t& nNonce, uint256 *phash)
{
    // Write the first 76 bytes of the block header to a double-SHA256 state.
    CHash256 hasher;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *pblock;
    assert(ss.size() == 80);
    hasher.Write((unsigned char*)&ss[0], 76);

    while (true) {
        nNonce++;

        // Write the last 4 bytes of the block header (the nonce) to a copy of
        // the double-SHA256 state, and compute the result.
        CHash256(hasher).Write((unsigned char*)&nNonce, 4).Finalize((unsigned char*)phash);

        // Return the nonce if the hash has at least some zero bits,
        // caller will check if it has enough to reach the target
        if (((uint16_t*)phash)[15] == 0)
            return true;

        // If nothing found after trying for a while, return -1
        if ((nNonce & 0xfff) == 0)
            return false;
    }
}
#endif

static bool ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams)
{
    LogPrintf("%s\n", pblock->ToString());
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("BitcoinMiner: generated block is stale");
    }

    // Inform about the new block
    GetMainSignals().BlockFound(pblock->GetHash());

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!ProcessNewBlock(state, chainparams, NULL, pblock, true, NULL))
        return error("BitcoinMiner: ProcessNewBlock, block not accepted");

    return true;
}

void static BitcoinMiner(const CChainParams& chainparams)
{
    LogPrintf("BitcoinMiner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("bitcoin-miner");

    unsigned int nExtraNonce = 0;

    boost::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().ScriptForMining(coinbaseScript);

    try {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbaseScript || coinbaseScript->reserveScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

        while (true) {
            if (chainparams.MiningRequiresPeers()) {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                do {
                    bool fvNodesEmpty;
                    {
                        LOCK(cs_vNodes);
                        fvNodesEmpty = vNodes.empty();
                    }
                    if (!fvNodesEmpty && !IsInitialBlockDownload())
                        break;
                    MilliSleep(1000);
                } while (true);
            }
#if HFP0_POW
            // HFP0 POW begin
            if (shutdownAllMinerThreads)
                break;
            // HFP0 POW end
#endif
            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrev = chainActive.Tip();

            auto_ptr<CBlockTemplate> pblocktemplate(CreateNewBlock(chainparams, coinbaseScript->reserveScript));
            if (!pblocktemplate.get())
            {
                LogPrintf("Error in BitcoinMiner: Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
                return;
            }
            CBlock *pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

            LogPrintf("Running BitcoinMiner with %u transactions in block (%u bytes)\n", pblock->vtx.size(),
                ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

            //
            // Search
            //
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
            uint256 hash;
            uint32_t nNonce = 0;
            while (true) {
                // Check if something found
#if HFP0_POW
                if (ScanHash(pblock, nNonce, &hash, hashTarget))
#else
                if (ScanHash(pblock, nNonce, &hash))
#endif
                {
                    if (UintToArith256(hash) <= hashTarget)
                    {
                        // Found a solution
                        pblock->nNonce = nNonce;
#if HFP0_POW
                        // HFP0 POW changed order of SetThreadPriority/assert as per satoshisbitcoin
                        SetThreadPriority(THREAD_PRIORITY_NORMAL);
#if HFP0_DEBUG_POW
                        // HFP0 DBG begin
                        LogPrintf("HFP0 POW: ScanHash returned hash: %s  \nnonce: %d\n", hash.GetHex(), nNonce);
                        // HFP0 DBG end
#endif
                        assert(hash == pblock->GetHash());
#else
                        assert(hash == pblock->GetHash());
                        SetThreadPriority(THREAD_PRIORITY_NORMAL);
#endif
                        LogPrintf("BitcoinMiner:\n");
                        LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
                        ProcessBlockFound(pblock, chainparams);

#if HFP0_POW
                        // HFP0 POW removed SetThreadPriority as per satoshisbitcoin
#else
                        SetThreadPriority(THREAD_PRIORITY_LOWEST);
#endif
                        coinbaseScript->KeepScript();

                        // In regression test mode, stop mining after a block is found.
                        if (chainparams.MineBlocksOnDemand())
                            throw boost::thread_interrupted();

                        break;
                    }
                }

                // Check for stop or if block needs to be rebuilt
                boost::this_thread::interruption_point();
                // Regtest mode doesn't require peers
                if (vNodes.empty() && chainparams.MiningRequiresPeers())
                    break;
#if HFP0_POW
                // HFP0 POW begin: change threshold as per satoshisbitcoin
                if (nNonce >= 0x000000ff)
                // HFP0 POW end
#else
                if (nNonce >= 0xffff0000)
#endif
                    break;
                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
                    break;
                if (pindexPrev != chainActive.Tip())
                    break;
#if HFP0_POW
                // HFP0 POW begin
                if (shutdownAllMinerThreads)
                    break;
                // HFP0 POW end
#endif
                // Update nTime every few seconds
                if (UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
                    break; // Recreate the block if the clock has run backwards,
                           // so that we can use the correct time.
                if (chainparams.GetConsensus().fPowAllowMinDifficultyBlocks)
                {
                    // Changing pblock->nTime can change work required on testnet:
                    hashTarget.SetCompact(pblock->nBits);
                }
            }
        }
    }
    catch (const boost::thread_interrupted&)
    {
        LogPrintf("BitcoinMiner terminated\n");
        throw;
    }
    catch (const std::runtime_error &e)
    {
        LogPrintf("BitcoinMiner runtime error: %s\n", e.what());
        return;
    }
}

void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams& chainparams)
{
    static boost::thread_group* minerThreads = NULL;

    if (nThreads < 0)
        nThreads = GetNumCores();

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate)
        return;

#if HFP0_POW
    // HFP0 POW begin
    // Run one thread less than the number of hardware cores, needed due to long processing time of new hash
    if (nThreads > 1)
        nThreads--;
    // HFP0 POW end
#endif

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&BitcoinMiner, boost::cref(chainparams)));
}
