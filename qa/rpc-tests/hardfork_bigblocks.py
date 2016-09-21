#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test mining and broadcast of larger-than-1MB-blocks
# (adapted from Classic's bigblocks.py)
#
import shutil   # HFP0 TST

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

from decimal import Decimal

# HFP0 TST begin: rename cache dir to deconflict with Classic naming
CACHE_DIR = "cache_hf_bigblock"
# HFP0 TST end


class BigBlockTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)

        if not os.path.isdir(os.path.join(CACHE_DIR, "node0")):
            print("Creating initial chain")

            for i in range(4):
                initialize_datadir(CACHE_DIR, i) # Overwrite port/rpcport in bitcoin.conf

            # Node 0 tries to create as-big-as-possible blocks.
            # Node 1 creates really small blocks
            # Node 2 creates empty up-version blocks
            self.nodes = []
            # Use node0 to mine blocks for input splitting
            self.nodes.append(start_node(0, CACHE_DIR, ["-blockmaxsize=2000000", "-debug=net"]))
            self.nodes.append(start_node(1, CACHE_DIR, ["-blockmaxsize=50000", "-debug=net"]))
            self.nodes.append(start_node(2, CACHE_DIR, ["-blockmaxsize=2000000", "-debug=net"]))
            self.nodes.append(start_node(3, CACHE_DIR, ["-blockmaxsize=1000", "-debug=net"]))

            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 2, 3)
            connect_nodes_bi(self.nodes, 3, 0)

            self.is_network_split = False
            self.sync_all()

            #print("Generating %d blocks, alternating between node 0 and 1" % (SIZE_FORK_HEIGHT_REGTEST+1))
            # Have nodes 0,1 find blocks in order until 2 blocks before fork height
            # we want to test later on that the block before the fork is not big
            for i in range(0,SIZE_FORK_HEIGHT_REGTEST+1):
                miner = i%2
                b1hash = self.nodes[miner].generate(1)[0]
                b1 = self.nodes[miner].getblock(b1hash, True)
                # TODO: check full version later, for now check base+fork bit
                assert(b1['version'] & (BASE_VERSION+FORK_BIT_2MB))
                assert(self.sync_blocks(self.nodes[0:2]))

            # Generate 1200 addresses
            #print("Generating 1200 addresses on node 3")
            addresses = [ self.nodes[3].getnewaddress() for i in range(0,1200) ]

            amount = Decimal("0.00125")

            send_to = { }
            for address in addresses:
                send_to[address] = amount

            tx_file = open(os.path.join(CACHE_DIR, "txdata"), "w")

            #print("Creating 4MB worth of tx's to be mined")
            # Create four megabytes worth of transactions ready to be mined:
            print("Creating 100 40K transactions (4MB)")
            for node in (0,1):
                for i in range(0,50):
                    txid = self.nodes[node].sendmany("", send_to, 1)
                    txdata = self.nodes[node].getrawtransaction(txid)
                    #print "created tx(%d,%d):" % (node,i), txid, (len(txdata)-1)/2, "bytes"
                    tx_file.write(txdata+"\n")
            tx_file.close()

            stop_nodes(self.nodes)
            wait_bitcoinds()
            self.nodes = []
            for i in range(4):
                os.remove(log_filename(CACHE_DIR, i, "debug.log"))
                os.remove(log_filename(CACHE_DIR, i, "db.log"))
                os.remove(log_filename(CACHE_DIR, i, "peers.dat"))
                os.remove(log_filename(CACHE_DIR, i, "fee_estimates.dat"))


        for i in range(4):
            from_dir = os.path.join(CACHE_DIR, "node"+str(i))
            to_dir = os.path.join(self.options.tmpdir,  "node"+str(i))
            shutil.copytree(from_dir, to_dir)
            initialize_datadir(self.options.tmpdir, i) # Overwrite port/rpcport in bitcoin.conf

    def sync_blocks(self, rpc_connections, wait=0.1, max_wait=30):
        """
        Wait until everybody has the same block count
        """
        for i in range(0,max_wait):
            if i > 0: time.sleep(wait)
            counts = [ x.getblockcount() for x in rpc_connections ]
            if counts == [ counts[0] ]*len(counts):
                return True
        return False

    def setup_network(self):
        print("enter setup_network")
        self.nodes = []

        #print("starting nodes")
        self.nodes.append(start_node(0, self.options.tmpdir, ["-blockmaxsize=2000000", "-debug=net"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-blockmaxsize=50000", "-debug=net"]))
        self.nodes.append(start_node(2, self.options.tmpdir, ["-blockmaxsize=2000000", "-debug=net"]))
        self.nodes.append(start_node(3, self.options.tmpdir, ["-blockmaxsize=1000", "-debug=net"]))

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 0)

        # Populate node0's mempool with cached pre-created transactions:
        #print("populate node0 mempool with txs")
        i=0
        s=0
        with open(os.path.join(CACHE_DIR, "txdata"), "r") as f:
            for line in f:
                #print "inserting transaction in node0 : %d" % i
                self.nodes[0].sendrawtransaction(line.rstrip())
                i += 1
                s += len(line.rstrip())

        #print "%d transactions (%s bytes) sent to mempool of node0" % (i, s)
        #print("setup_network done")

    def copy_mempool(self, from_node, to_node):
        txids = from_node.getrawmempool()
        for txid in txids:
            txdata = from_node.getrawtransaction(txid)
            to_node.sendrawtransaction(txdata)

    def TestMineBig(self, expect_big, expect_version=None):
      # Test if node0 will mine big blocks.
        b1hash = self.nodes[0].generate(1)[0]
        b1 = self.nodes[0].getblock(b1hash, True)
        assert(self.sync_blocks(self.nodes))

        if expect_version:
            print "TestMineBig: b1 version = %s" % b1['version']
            assert b1['version'] & (BASE_VERSION+FORK_BIT_2MB)

        if expect_big:
            print "TestMineBig: b1 size = %s" % b1['size']
            assert(b1['size'] > 1000*1000)

            # Have node1 mine on top of the block,
            # to make sure it goes along with the fork
            b2hash = self.nodes[1].generate(1)[0]
            b2 = self.nodes[1].getblock(b2hash, True)
            assert(b2['previousblockhash'] == b1hash)
            assert(self.sync_blocks(self.nodes))

        else:
            assert(b1['size'] <= 1000*1000)

        # Reset chain to before b1hash:
        for node in self.nodes:
            node.invalidateblock(b1hash)
        assert(self.sync_blocks(self.nodes))

    def run_test(self):
        # nodes 0 and 1 have 50 mature 50-BTC coinbase transactions.
        # Spend them with 50 transactions, each that has
        # 1,200 outputs (so they're about 41K big).

        print("Testing HFP0 big blocks")
        assert(self.sync_blocks(self.nodes))
        mempool_size_0 = len(self.nodes[0].getrawmempool())
        mempool_size_1 = len(self.nodes[1].getrawmempool())
        mempool_size_2 = len(self.nodes[2].getrawmempool())
        mempool_size_3 = len(self.nodes[3].getrawmempool())
        #print "mempool sizes:", mempool_size_0, mempool_size_1, mempool_size_2, mempool_size_3
        blocks = []

        # Fork is controlled by block height
        # large blocks may only be created after fork

        # HFP0 TST TODO: re-enable the precondition test steps!
        # check the comments below re: chain height for accuracy

        ## At this point the chain is SIZE_FORK_HEIGHT_REGTEST-2 blocks long

        ## No big blocks at SIZE_FORK_HEIGHT_REGTEST-1
        #self.TestMineBig(expect_big=False, expect_version=True)

        ## mine block SIZE_FORK_HEIGHT_REGTEST-1
        #blocks.append(self.nodes[0].generate(1)[0])
        assert(self.sync_blocks(self.nodes))

        # big blocks at height SIZE_FORK_HEIGHT_REGTEST+1
        print "mining first block after restart - should be at height=%d+" % (SIZE_FORK_HEIGHT_REGTEST+1)
        self.TestMineBig(expect_big=True, expect_version=True)
        print "past first block"

        # mine block SIZE_FORK_HEIGHT_REGTEST for real
        blocks.append(self.nodes[0].generate(1)[0])
        assert(self.sync_blocks(self.nodes))

        # Shutdown then restart node[0], it should
        # remember fork state and produce a big block.
        stop_node(self.nodes[0], 0)
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-blockmaxsize=2000000", "-debug=net", ])
        self.copy_mempool(self.nodes[1], self.nodes[0])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        print "mining second block after restart"
        self.TestMineBig(expect_big=True, expect_version=True)
        print "past mining second block"

        # HFP0 TODO: Test re-orgs


if __name__ == '__main__':
    print("Be patient, these tests can take several minutes to run.")

    BigBlockTest().main()

    # HFP0 TST begin
    # this test fails if re-run with the cache dir left, remove it for now.
    # uncomment the printouts below if you disable the removal.
    #print("Cached test chain and transactions left in %s"%(CACHE_DIR))
    #print(" (remove that directory if you will not run this test again)")

    shutil.rmtree(CACHE_DIR)
    # HFP0 TST end
