// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// HFP0 BSZ import BitPay adaptive block size patch (entire file)
#include "chainparams.h"
#include "blocksizecalculator.h"

using namespace BlockSizeCalculator;
using namespace std;

static std::vector<unsigned int> blocksizes;
static bool sorted = false;
// HFP0 BSZ begin
// track the last height, in order to reset or not insert redundantly
static unsigned int last_height_seen = 0;
// cache last result if called on same height
static unsigned int last_result_returned = 0;
// HFP0 BSZ end

unsigned int BlockSizeCalculator::ComputeBlockSize(CBlockIndex *pblockindex, unsigned int pastblocks) {

	unsigned int proposedMaxBlockSize = 0;
	unsigned int result = OLD_MAX_BLOCK_SIZE;
    const Consensus::Params& params = Params().GetConsensus();

	if (pblockindex->nHeight < (int)last_height_seen) {
	    ::ClearBlockSizes();
    }
    else if (pblockindex->nHeight == (int)last_height_seen) {
        return last_result_returned;
    }

	LOCK(cs_main);

	proposedMaxBlockSize = ::GetMedianBlockSize(pblockindex, pastblocks);
#if HFP0_DEBUG_BSZ
    // HFP0 DBG begin
    LogPrintf("HFP0 BSZ: in ComputeBlockSize median = %u\n", proposedMaxBlockSize);
    // HFP0 DBG end
#endif

	if (proposedMaxBlockSize > 0) {
		// multiply the median by the chosen factor
		result = proposedMaxBlockSize * MAX_BLOCK_SIZE_INCREASE_MULTIPLE;
        // check if multiplication result overflowed the 4 byte unsigned int
        // if so, limit the result to the maximum value of 4-byte unsigned integer
		result = result < proposedMaxBlockSize ?
				std::numeric_limits<unsigned int>::max() :
				result;
        // the old max block size serves as a lower limit
		if (result < OLD_MAX_BLOCK_SIZE) {
#if HFP0_DEBUG_BSZ
            // HFP0 DBG begin
            LogPrintf("HFP0 BSZ: in ComputeBlockSize limiting result (%u) to historic block size limit (%u)\n", result, OLD_MAX_BLOCK_SIZE);
            // HFP0 DBG end
#endif
			result = OLD_MAX_BLOCK_SIZE;
		}
	}

    // HFP0 BSZ begin
    // since the adaptive block size code is called only after fork,
    // let's set 2MB as lower block size limit from there on.
	if (result < 2 * OLD_MAX_BLOCK_SIZE && pblockindex->nHeight >= params.nHFP0ActivateSizeForkHeight)
			result = 2 * OLD_MAX_BLOCK_SIZE;

    // limit to post-fork upper block size limit (ceiling for adaptive size)
    if (result > MAX_BLOCK_SIZE)
        result = MAX_BLOCK_SIZE;
    // HFP0 BSZ end

#if HFP0_DEBUG_BSZ
    // HFP0 DBG begin
    LogPrintf("HFP0 BSZ: ComputeBlockSize = %u\n", result);
    // HFP0 DBG end
#endif

    last_height_seen = pblockindex->nHeight;
    last_result_returned = result;
	return result;

}

inline unsigned int BlockSizeCalculator::GetMedianBlockSize(
		CBlockIndex *pblockindex, unsigned int pastblocks) {

	blocksizes = ::GetBlockSizes(pblockindex, pastblocks);

	if(!sorted) {
		std::sort(blocksizes.begin(), blocksizes.end());
		sorted = true;
	}

	unsigned int vsize = blocksizes.size();
#if HFP0_DEBUG_BSZ
    // HFP0 DBG begin
    LogPrintf("HFP0 BSZ: vsize = %u\n", vsize);
    // HFP0 DBG end
#endif
	if (vsize == pastblocks) {
		double median = 0;
		if ((vsize % 2) == 0) {
			median = (blocksizes[vsize / 2] + blocksizes[(vsize / 2) - 1]) / 2.0;
		} else {
			median = blocksizes[vsize / 2];
		}
#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: GetMedianBlockSize = %u\n", static_cast<unsigned int>(floor(median)));
        // HFP0 DBG end
#endif
		return static_cast<unsigned int>(floor(median));
	} else {
		return 0;
	}

}

void BlockSizeCalculator::ClearBlockSizes() {
    blocksizes.clear();
    sorted = false;
}

inline std::vector<unsigned int> BlockSizeCalculator::GetBlockSizes(
		CBlockIndex *pblockindex, unsigned int pastblocks) {

	if (pblockindex->nHeight < (int)pastblocks) {
#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: GetBlockSizes: nHeight (%u) < pastblocks, returning blocksizes unchanged\n", pblockindex->nHeight);
        // HFP0 DBG end
#endif
		return blocksizes;
	}

	int firstBlock = pblockindex->nHeight - pastblocks;

#if HFP0_DEBUG_BSZ
    // HFP0 DBG begin
    LogPrintf("HFP0 BSZ: GetBlockSizes: firstBlock = %u\n", firstBlock);
    // HFP0 DBG end
#endif

	if (blocksizes.size() > 0) {

#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: GetBlockSizes: blocksizes.size() > 0 (%u)\n", blocksizes.size());
        // HFP0 DBG end
#endif

		int latestBlockSize = ::GetBlockSize(pblockindex);

		if (latestBlockSize != -1) {

			CBlockIndex *firstBlockIndex = chainActive[firstBlock];
			int oldestBlockSize = ::GetBlockSize(firstBlockIndex);
			if (oldestBlockSize != -1) {
				std::vector<unsigned int>::iterator it;
				it = std::find(blocksizes.begin(), blocksizes.end(),
						oldestBlockSize);
				if (it != blocksizes.end()) {
					blocksizes.erase(it);
					it = std::lower_bound(blocksizes.begin(), blocksizes.end(),
							latestBlockSize);
					blocksizes.insert(it, latestBlockSize);
#if HFP0_DEBUG_BSZ
                    // HFP0 DBG begin
                    LogPrintf("HFP0 BSZ: GetBlockSizes: inserting latest size %u\n", latestBlockSize);
                    // HFP0 DBG end
#endif
				}
#if HFP0_DEBUG_BSZ
                else {
                    // HFP0 DBG begin
                    LogPrintf("HFP0 BSZ: GetBlockSizes: find yielded end of vector, not inserting latest size %u\n", latestBlockSize);
                    // HFP0 DBG end
                }
#endif
			}

		}

	} else {

#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: GetBlockSizes: blocksizes.size() == 0, pushing back\n");
        // HFP0 DBG end
#endif

		while (pblockindex != NULL && pblockindex->nHeight > firstBlock) {

			int blocksize = ::GetBlockSize(pblockindex);
			if (blocksize != -1) {

				blocksizes.push_back(blocksize);

#if HFP0_DEBUG_BSZ
                // HFP0 DBG begin
                LogPrintf("HFP0 BSZ: pushback: %u\n", blocksize);
                // HFP0 DBG end
#endif
			}
#if HFP0_DEBUG_BSZ
            else {
                // HFP0 DBG begin
                LogPrintf("HFP0 BSZ: GetBlockSizes: not pushing back block at height %u because GetBlockSize = -1 (strange)\n", pblockindex->nHeight);
                // HFP0 DBG end
            }
#endif

			pblockindex = pblockindex->pprev;
		}

	}

	return blocksizes;

}

inline int BlockSizeCalculator::GetBlockSize(CBlockIndex *pblockindex) {

	if (pblockindex == NULL) {
#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: GetBlockSize: returning -1 because pblockindex is NULL\n");
        // HFP0 DBG end
#endif
		return -1;
	}

	const CDiskBlockPos& pos = pblockindex->GetBlockPos();

	CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);

	if (filein.IsNull()) {
#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: GetBlockSize: returning -1 because filein.IsNull\n");
        // HFP0 DBG end
#endif
		return -1;
	}

	FILE* blockFile = filein.release();
	long int filePos = ftell(blockFile);
	fseek(blockFile, filePos - sizeof(uint32_t), SEEK_SET);

	uint32_t size = 0;
    // HFP0 BSZ, CLN begin
    // added evaluation of fread() return value
    size_t items_read = 0;
	items_read = fread(&size, sizeof(uint32_t), 1, blockFile);
    fclose(blockFile);
    if (items_read != 1) {
#if HFP0_DEBUG_BSZ
        // HFP0 DBG begin
        LogPrintf("HFP0 BSZ: GetBlockSize: returning -1 because items_read != 1\n");
        // HFP0 DBG end
#endif
        return -1;
    }
    else {
	    return (unsigned int) size;
    }
    // HFP0 BSZ, CLN end

}

// HFP0 BSZ added function to scale soft limit. Currently stubbed
// to return the configured soft limit without actually scaling.
uint32_t BlockSizeCalculator::ComputeScaledBlockMaxSize(uint32_t nBlockSizeMax, unsigned int maxBlockSize)
{
    uint32_t scaledSoftLimit = nBlockSizeMax;

    // HFP0 TODO: perform actual scaling calculation, not just return the unscaled soft value
    return scaledSoftLimit;
}
