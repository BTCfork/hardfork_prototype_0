// Copyright (c) 2016 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/validation.h"
#include "consensus/consensus.h"
#include "main.h"
#include "miner.h"
#include "pubkey.h"
#include "random.h"
#include "uint256.h"
#include "util.h"
// HFP0 TST begin
#include "keystore.h"
#include "script/sign.h"
#include "blocksizecalculator.h"
// HFP0 TST end

#include "test/test_bitcoin.h"

#include <boost/atomic.hpp>
#include <boost/test/unit_test.hpp>

// HFP0 TST begin
// use the official number of blocks for median-based computation
#define NUM_BLOCKS_FOR_SIZE_TEST   NUM_BLOCKS_FOR_MEDIAN_BLOCK
// HFP0 TST end


struct TestChainForBlockSizeSetup : public TestChain100Setup {
    void FillBlock(CBlock& block, unsigned int nSize);
    // new
	void BuildSmallBlocks(unsigned int numblocks);
    void AdvanceToBeforeFork();
    void AdvanceToFork();
    void CreateSmallBlock();
    bool IsForkedBlock(CBlock& block);
    bool TestCheckBlock(CBlock& block, uint32_t nHeight, unsigned int nSize, bool check_is_forked_version);

};


// create a small (no txs) block
void TestChainForBlockSizeSetup::CreateSmallBlock()
{
    coinbaseKey.MakeNewKey(true);
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    std::vector<CMutableTransaction> noTxns;
    CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
    coinbaseTxns.push_back(b.vtx[0]);
    // do computation only if not past fork height, otherwise it is done automatically
    if ((unsigned int)chainActive.Height() < SIZE_FORK_HEIGHT_REGTEST)
        BlockSizeCalculator::ComputeBlockSize(chainActive.Tip(), NUM_BLOCKS_FOR_SIZE_TEST);
}

// create a certain number of small blocks
void TestChainForBlockSizeSetup::BuildSmallBlocks(unsigned int numblocks)
{
	for (unsigned int i = 0; i < numblocks; ++i) {
        CreateSmallBlock();
    }
}


void TestChainForBlockSizeSetup::AdvanceToBeforeFork()
{
    // generate enough blocks so we end up a block before fork trigger height
    // (TestChain100Setup already generated 100 blocks)
    unsigned int start_height = chainActive.Height();
    TestChainForBlockSizeSetup::BuildSmallBlocks(SIZE_FORK_HEIGHT_REGTEST - start_height - 2);
}


void TestChainForBlockSizeSetup::AdvanceToFork()
{
    // generate enough blocks so we end up a block before fork trigger height
    // (TestChain100Setup already generated 100 blocks)
    unsigned int start_height = chainActive.Height();
    TestChainForBlockSizeSetup::BuildSmallBlocks(SIZE_FORK_HEIGHT_REGTEST - start_height - 1);

}


// Fill block with dummy transactions until it's serialized size is exactly nSize
// HFP0 TST : remove 'static' qualifier
void TestChainForBlockSizeSetup::FillBlock(CBlock& block, unsigned int nSize)
{
    assert(block.vtx.size() > 0); // Start with at least a coinbase

    unsigned int nBlockSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    if (nBlockSize > nSize) {
        block.vtx.resize(1); // passed in block is too big, start with just coinbase
        nBlockSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    }

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_11;
    tx.vin[0].prevout.hash = block.vtx[0].GetHash(); // passes CheckBlock, would fail if we checked inputs.
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 1LL;
    tx.vout[0].scriptPubKey = block.vtx[0].vout[0].scriptPubKey;

    unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    block.vtx.reserve(1+nSize/nTxSize);

    // ... add copies of tx to the block to get close to nSize:
    while (nBlockSize+nTxSize < nSize) {
        block.vtx.push_back(tx);
        nBlockSize += nTxSize;
        tx.vin[0].prevout.hash = GetRandHash(); // Just to make each tx unique
    }
    // Make the last transaction exactly the right size by making the scriptSig bigger.
    block.vtx.pop_back();
    nBlockSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    unsigned int nFill = nSize - nBlockSize - nTxSize;
    for (unsigned int i = 0; i < nFill; i++)
        tx.vin[0].scriptSig << OP_11;
    block.vtx.push_back(tx);
    nBlockSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    assert(nBlockSize == nSize);
}


// check if block version is in the range (min..max) of forked block version numbers
bool TestChainForBlockSizeSetup::IsForkedBlock(CBlock& block)
{
    return ((uint32_t)block.nVersion >= (BASE_VERSION + FULL_FORK_VERSION_MIN) && (uint32_t)block.nVersion <= (BASE_VERSION + FULL_FORK_VERSION_MAX));
}


// HFP0 TST : remove 'static' qualifier
bool TestChainForBlockSizeSetup::TestCheckBlock(CBlock& block, uint32_t nHeight, unsigned int nSize, bool is_forked_version)
{
    uint64_t t = GetTime();
    block.nTime = t;
    FillBlock(block, nSize);
    CValidationState validationState;
    bool fResult = CheckBlock(block, validationState, false, false) && validationState.IsValid();
    return fResult && (IsForkedBlock(block) == is_forked_version);
}


// HFP0 TST begin: rename test suite to conform to convention
BOOST_FIXTURE_TEST_SUITE(block_size_tests, TestChainForBlockSizeSetup)
// HFP0 TST end

//
// Unit test CheckBlock() for conditions around the block size hard fork
//
BOOST_AUTO_TEST_CASE(BigBlockFork_AroundForkHeight)
{
    CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    const CChainParams& chainparams = Params(CBaseChainParams::REGTEST);
    CBlockTemplate *pblocktemplate;
    CBlock *pblock = NULL;
    uint64_t preforkSize = OLD_MAX_BLOCK_SIZE;
    uint64_t postforkSize = 2 * OLD_MAX_BLOCK_SIZE;

    LOCK(cs_main);

    // After setup (block 100), way before fork height...
    BOOST_CHECK(pblocktemplate = CreateNewBlock(chainparams, scriptPubKey));
    pblock = &pblocktemplate->block;
    BOOST_CHECK(TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST-1, preforkSize, false)); // 1MB : valid
    BOOST_CHECK(!TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST-1, preforkSize+1, false)); // >1MB : invalid
    BOOST_CHECK(!TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST-1, postforkSize, false)); // big : invalid

    // Before fork height...
    TestChainForBlockSizeSetup::AdvanceToBeforeFork();
    delete pblocktemplate;
    BOOST_CHECK(pblocktemplate = CreateNewBlock(chainparams, scriptPubKey));
    pblock = &pblocktemplate->block;
    BOOST_CHECK(TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST-1, preforkSize, false)); // 1MB : valid
    BOOST_CHECK(!TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST-1, preforkSize+1, false)); // >1MB : invalid
    BOOST_CHECK(!TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST-1, postforkSize, false)); // big : invalid

    // Exactly at fork height... new version, but bigger block size is not yet in effect - only at next block.
    TestChainForBlockSizeSetup::AdvanceToFork();
    delete pblocktemplate;
    BOOST_CHECK(pblocktemplate = CreateNewBlock(chainparams, scriptPubKey));
    pblock = &pblocktemplate->block;
    BOOST_CHECK(TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST, preforkSize, true)); // 1MB : valid
    BOOST_CHECK(!TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST, postforkSize, true)); // big : invalid
    BOOST_CHECK(!TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST, postforkSize+1, true)); // big+1 : invalid

    // Past fork height...
    TestChainForBlockSizeSetup::BuildSmallBlocks(1);
    delete pblocktemplate;
    BOOST_CHECK(pblocktemplate = CreateNewBlock(chainparams, scriptPubKey));
    pblock = &pblocktemplate->block;
    BOOST_CHECK(TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST, preforkSize, true)); // 1MB : valid
    BOOST_CHECK(TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST, postforkSize, true)); // big : valid
    BOOST_CHECK(!TestCheckBlock(*pblock, SIZE_FORK_HEIGHT_REGTEST, postforkSize+1, true)); // big+1 : invalid
}

// HFP0 TST removed obsolete test cases dealing with grace period/election

BOOST_AUTO_TEST_SUITE_END()
