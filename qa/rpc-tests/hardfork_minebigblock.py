#!/usr/bin/env python2
#
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.comptool import wait_until
import time

'''
Test mining of a bigger (>1MB) block under adaptive block size.
'''

# TestNode: bare-bones "peer".  Used mostly as a conduit for a test to sending
# p2p messages to a node, generating the messages in the main testing logic.
class TestNode(NodeConnCB):
    def __init__(self):
        NodeConnCB.__init__(self)
        self.connection = None
        self.ping_counter = 1
        self.last_pong = msg_pong()

    def on_inv(self, conn, message):
        pass

    # Spin until verack message is received from the node.
    # We use this to signal that our test can begin. This
    # is called from the testing thread, so it needs to acquire
    # the global lock.
    def wait_for_verack(self):
        def veracked():
            return self.verack_received
        return wait_until(veracked, timeout=10)

    def on_pong(self, conn, message):
        self.last_pong = message

    def on_close(self, conn):
        self.peer_disconnected = True

class MineBigBlockTest(BitcoinTestFramework):
    def __init__(self):
        self.utxo = []
        self.normal_txouts = gen_return_txouts()
        self.bigger_txouts = bigger_gen_return_txouts()

    def add_options(self, parser):
        parser.add_option("--testbinary", dest="testbinary",
                          default=os.getenv("BITCOIND", "bitcoind"),
                          help="bitcoind binary to test")

    def setup_chain(self):
        initialize_chain_clean(self.options.tmpdir, 1)

    def setup_network(self):
        # Start two nodes with blockmaxsize of 1.5 MB
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug", "-blockmaxsize=1500000"]))

    def mine_normal_full_block(self, node, address):
        # Want to create a nearly full 1MB block
        # We'll generate a 66k transaction below, and 14 of them is close to the 1MB block limit
        for j in xrange(14):
            if len(self.utxo) < 14:
                self.utxo = node.listunspent()
            inputs=[]
            outputs = {}
            t = self.utxo.pop()
            inputs.append({ "txid" : t["txid"], "vout" : t["vout"]})
            # HFP0 TST begin increase fee to avoid insufficient priority
            remchange = t["amount"] - Decimal("0.001014")
            # HFP0 TST end
            outputs[address]=remchange
            # Create a basic transaction that will send change back to ourself after account for a fee
            # And then insert the 128 generated transaction outs in the middle rawtx[92] is where the #
            # of txouts is stored and is the only thing we overwrite from the original transaction
            rawtx = node.createrawtransaction(inputs, outputs)
            newtx = rawtx[0:92]
            newtx = newtx + self.normal_txouts
            newtx = newtx + rawtx[94:]
            # Appears to be ever so slightly faster to sign with SIGHASH_NONE
            signresult = node.signrawtransaction(newtx,None,None,"NONE")
            txid = node.sendrawtransaction(signresult["hex"], True)
        # Mine a full sized block which will be these transactions we just created
        node.generate(1)

    def mine_big_full_block(self, node, address):
        # Want to create a bigger (>1MB) block
        # We'll generate a 66k*1.5 transaction below, and 14 of them is close to the 1.5MB block limit
        for j in xrange(14):
            if len(self.utxo) < 14:
                self.utxo = node.listunspent()
            inputs=[]
            outputs = {}
            t = self.utxo.pop()
            inputs.append({ "txid" : t["txid"], "vout" : t["vout"]})
            # HFP0 TST begin increase fee to avoid insufficient priority
            remchange = t["amount"] - Decimal("0.001014")
            # HFP0 TST end
            outputs[address]=remchange
            # Create a basic transaction that will send change back to ourself after account for a fee
            # And then insert the 128 generated transaction outs in the middle rawtx[92] is where the #
            # of txouts is stored and is the only thing we overwrite from the original transaction
            rawtx = node.createrawtransaction(inputs, outputs)
            newtx = rawtx[0:92]
            newtx = newtx + self.bigger_txouts
            newtx = newtx + rawtx[94:]
            # Appears to be ever so slightly faster to sign with SIGHASH_NONE
            signresult = node.signrawtransaction(newtx,None,None,"NONE")
            txid = node.sendrawtransaction(signresult["hex"], True)
        # Mine a full sized block which will be these transactions we just created
        node.generate(1)

    def run_test(self):
        # Generate some blocks to fork on regtest
        # (currently fork at block <SIZE_FORK_HEIGHT_REGTEST>)
        self.nodes[0].generate(SIZE_FORK_HEIGHT_REGTEST)

        # Test logic begins here

        # Now mine some normal full blocks (< 1MB)
        for i in range(144):
            self.mine_normal_full_block(self.nodes[0], self.nodes[0].getnewaddress())

        # Now mine a bigger block (> 1MB)
        self.mine_big_full_block(self.nodes[0], self.nodes[0].getnewaddress())

        # Store the hash; we'll request this later
        big_block = self.nodes[0].getbestblockhash()
        block_size = self.nodes[0].getblock(big_block, True)['size']
        big_block = int(big_block, 16)

        print "last block size: %d" % block_size
        assert(block_size > 1000000)  # test that the blog exceeds the 1MB limit


if __name__ == '__main__':
    MineBigBlockTest().main()
