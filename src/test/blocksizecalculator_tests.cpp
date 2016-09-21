// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// HFP0 BSZ import initial file from BitPay adaptive block size patch
#include "blocksizecalculator.h"
#include "test/test_bitcoin.h"
#include "script/sign.h"
#include "keystore.h"
#include <boost/test/unit_test.hpp>


// HFP0 TST begin
// change from Bitpay's minimum of 1MB to 2MB
#define BSZ_MINIMUM_BLOCK_SIZE       (2*OLD_MAX_BLOCK_SIZE)
// use a vector of 10 blocks for median in this test, instead of NUM_BLOCKS_FOR_MEDIAN_BLOCK
#define NUM_BLOCKS_FOR_MEDIAN_TEST   5
// HFP0 TST end

/** things to test
 * 1. max block size should never be less than BSZ_MINIMUM_BLOCK_SIZE
 * 2. the median block size should be calculated correctly for a few examples
 */

extern unsigned int median_block_lookback; //= NUM_BLOCKS_FOR_MEDIAN_BLOCK;

static int nOutputs = 10000;

struct TestChainForComputingMediansSetup : public TestChain100Setup {
	bool GenerateRandomTransaction(CMutableTransaction&);
	void BuildIncreasingBlocks(unsigned int numblocks, unsigned int outputs_incr);
	void BuildSmallBlocks(unsigned int numblocks);
    void AdvanceToBeforeFork();

};

bool TestChainForComputingMediansSetup::GenerateRandomTransaction(CMutableTransaction& txNew)
{
	CAmount amountToSend = 5000;
	std::vector<CMutableTransaction> res;

	CKey key;
	key.MakeNewKey(true);
	CScript scriptPubKey = CScript() << ToByteVector(key.GetPubKey())
			<< OP_CHECKSIG;

	CBasicKeyStore keystore;
	keystore.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

	CTransaction utxo = coinbaseTxns[0];
	coinbaseTxns.erase(coinbaseTxns.begin());

	txNew.nLockTime = chainActive.Height();
	txNew.vin.clear();
	txNew.vout.clear();

	for (int j = 0; j < nOutputs; ++j) {
		CTxOut txout(amountToSend, scriptPubKey);
		txNew.vout.push_back(txout);
	}

	//vin
	CTxIn vin = CTxIn(utxo.GetHash(), 0, CScript(),
			std::numeric_limits<unsigned int>::max() - 1);
	txNew.vin.push_back(vin);

	//previous tx's script pub key that we need to sign
	CScript& scriptSigRes = txNew.vin[0].scriptSig;
	CTransaction txNewConst(txNew);
	ProduceSignature(TransactionSignatureCreator(&keystore, &txNewConst, 0),
			utxo.vout[0].scriptPubKey, scriptSigRes);
	res.push_back(txNew);

	return true;
}

void TestChainForComputingMediansSetup::BuildSmallBlocks(unsigned int numblocks)
{
    unsigned int nBlockSize = 0;

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    // generate enough blocks so we end up a block before fork trigger height
    // (TestChain100Setup already generated 100 blocks)
    for (unsigned int i = 0; i < numblocks; ++i) {
        std::vector<CMutableTransaction> noTxns;
        CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
        nBlockSize = ::GetSerializeSize(b, SER_NETWORK, PROTOCOL_VERSION);
        assert(nBlockSize > 0);
        coinbaseTxns.push_back(b.vtx[0]);
        // do computation only if not past fork height, otherwise it is done automatically
        if ((unsigned int)chainActive.Height() < SIZE_FORK_HEIGHT_REGTEST)
            BlockSizeCalculator::ComputeBlockSize(chainActive.Tip(), NUM_BLOCKS_FOR_MEDIAN_TEST);
    }
}

void TestChainForComputingMediansSetup::AdvanceToBeforeFork()
{
    unsigned int nBlockSize = 0;

    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    // generate enough blocks so we end up a block before fork trigger height
    // (TestChain100Setup already generated 100 blocks)
    unsigned int start_height = chainActive.Height();
    for (unsigned int i = 0; i < (SIZE_FORK_HEIGHT_REGTEST - start_height - 1); ++i) {
        std::vector<CMutableTransaction> noTxns;
        CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
        nBlockSize = ::GetSerializeSize(b, SER_NETWORK, PROTOCOL_VERSION);
        assert(nBlockSize > 0);

        coinbaseTxns.push_back(b.vtx[0]);
    }
}

void TestChainForComputingMediansSetup::BuildIncreasingBlocks(unsigned int numblocks, unsigned int outputs_incr)
{
    unsigned int nBlockSize = 0;
    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    // generate numblocks blocks on current chain tip
    for (unsigned int i = 0; i < numblocks; ++i) {
        // it takes about a second to validate a large block such as this
        std::vector<CMutableTransaction> txs;
        CMutableTransaction tx;

        if(GenerateRandomTransaction(tx)) {
            txs.push_back(tx);
            CBlock b = CreateAndProcessBlock(txs, scriptPubKey);
            nBlockSize = ::GetSerializeSize(b, SER_NETWORK, PROTOCOL_VERSION);
            assert(nBlockSize > 0);
            coinbaseTxns.push_back(b.vtx[0]);
        }
        nOutputs += outputs_incr;
    }
}

// HFP0 TST begin: rename test suite to conform to convention
BOOST_FIXTURE_TEST_SUITE(blocksizecalculator_tests, TestChainForComputingMediansSetup)
// HFP0 TST end

BOOST_AUTO_TEST_CASE(ComputeBlockSizeForShortChainPreFork)
{
    // While not forked the block size should not exceed 1MB
    unsigned int size = BlockSizeCalculator::ComputeBlockSize(chainActive.Tip(), NUM_BLOCKS_FOR_MEDIAN_TEST);
    BOOST_CHECK_EQUAL(size, OLD_MAX_BLOCK_SIZE);
}

BOOST_AUTO_TEST_CASE(ComputeBlockSizeWithSmallBlocksAfterFork)
{
    median_block_lookback = NUM_BLOCKS_FOR_MEDIAN_TEST;
    TestChainForComputingMediansSetup::AdvanceToBeforeFork();
    unsigned int size = BlockSizeCalculator::ComputeBlockSize(chainActive.Tip(), NUM_BLOCKS_FOR_MEDIAN_TEST);
    BOOST_CHECK_EQUAL(size, OLD_MAX_BLOCK_SIZE);
    // trigger the fork
    TestChainForComputingMediansSetup::BuildSmallBlocks(1);
    size = BlockSizeCalculator::ComputeBlockSize(chainActive.Tip(), NUM_BLOCKS_FOR_MEDIAN_TEST);
    BOOST_CHECK_EQUAL(size, BSZ_MINIMUM_BLOCK_SIZE);
}

BOOST_AUTO_TEST_CASE(ComputeBlockSizeWithEverIncreasingBlockSizes)
{
    //Testing that medians compute correctly, with upper limit of MAX_BLOCK_SIZE
    unsigned int size = 0;
    median_block_lookback = NUM_BLOCKS_FOR_MEDIAN_TEST;
    TestChainForComputingMediansSetup::AdvanceToBeforeFork();
    BlockSizeCalculator::ClearBlockSizes();
    // go over the 2MB lower limit
    TestChainForComputingMediansSetup::BuildIncreasingBlocks(10, 2000);
    size = BlockSizeCalculator::ComputeBlockSize(chainActive.Tip(), NUM_BLOCKS_FOR_MEDIAN_TEST);
    BOOST_CHECK(size == 2112614 || size == 2112612);   //the signatures could yield different lengths on different runs
    // hit the MAX_BLOCK_SIZE upper limit
    TestChainForComputingMediansSetup::BuildIncreasingBlocks(11, 2000);
    size = BlockSizeCalculator::ComputeBlockSize(chainActive.Tip(), NUM_BLOCKS_FOR_MEDIAN_TEST);
    BOOST_CHECK_EQUAL(size, MAX_BLOCK_SIZE);
}

BOOST_AUTO_TEST_SUITE_END()
