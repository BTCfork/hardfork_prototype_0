
HFP0 - an early prototype of a Bitcoin hard fork
================================================


What is Bitcoin?
----------------

Bitcoin is peer to peer electronic cash for the Internet. It operates with no central authority: managing transactions and
issuing money are carried out collectively by the network.

Read more about why we want to fork Bitcoin: https://bitcoinforks.org


What is this hard fork prototype?
---------------------------------

HFP0 is a prototype of a Bitcoin hard fork to a bigger block size limit
using the method described by Satoshi Nakamoto:

    "It can be phased in, like:
        if (blocknumber > 115000) maxblocksize = largerlimit"

\- Source: https://bitcointalk.org/index.php?topic=1347.msg15366#msg15366

HFP0 is inspired by [satoshisbitcoin](https://github.com/satoshisbitcoin/satoshisbitcoin), but derived from a more recent version of Bitcoin Classic (0.12).
For more information on the Bitcoin Classic software, see https://bitcoinclassic.com.

Development of this hard fork code was done based on generous advice and
encouragement from many members of the Bitcoin community.
Thanks to all of you who made me feel welcome, excited and happy to work
on Bitcoin. Hopefully this can be a contribution to the ongoing effort of
developing a safe hard fork to scale Bitcoin.


Disclaimer
----------

This prototype is not ready for deployment, and should not be used to
handle actual money.  Read more about the shortcomings and future
fork development directions at https://ftrader.github.io .

If you use it, you do so at your own risk, so take all the necessary
precautions you should be taking anyway when running experiment
software.


HFP0 features
-------------

- block height trigger (mainnet / testnet trigger heights in branch name)
- proof-of-work: SHA256 or modified scrypt, depending on --disable-newpow configuration
- difficulty reset at fork trigger
- per-block difficulty retargeting after fork height: MIDAS algorithm
- adaptive block size cap (BitPay with floor of 2MB and ceiling of 4MB)
- Xtreme Thinblocks (from Bitcoin Unlimited)
- disabled alert system, RBF
- working network separation using DoS score banning
- active BIPs retrofitted: BIP9, BIP65, BIP68, BIP112, BIP113


Building
--------

Build instructions for Classic 0.12.0 should work.

HFP0 adds a new configuration option: `--disable-newpow` .

Unless you specify this option, the client is built to switch to the new POW at the trigger height.

If you specify `--disable-newpow`, the fork keeps using Bitcoin's existing SHA256 after it triggers.


HFP0 shortcomings
-----------------

There are quite a few, including some that would make it unviable in my view (e.g. lack of replay attack protection, empty DNS seed lists etc.). That's why it's called an early prototype, and is explicity released without intention to fork using this codebase.

See my blog posts at https://ftrader.github.io about the details on the prototype's shortcomings, and join us on
[Reddit](https://www.reddit.com/r/btcfork) or the BTCfork Slack (see links below) to discuss about further hard fork developments.


License
-------

The Bitcoin software is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.


Development Process
-------------------

This is a snapshot repository, no active development takes place here.

The BTCfork community (links below) is working to develop improved
forks, starting from minimum viable fork (MVF) implementations.
If you want to get involved, get in touch on any of the channels below.


BTCfork community
-----------------

- Primary Website: https://bitcoinforks.org/
- Slack: https://btcforks.signup.team/
- Reddit: https://www.reddit.com/r/btcfork/
- GitHub: https://github.com/BTCfork
- ConsiderIt (issue voting): https://btcforks.consider.it/
- Twitter: https://twitter.com/btcfork


Change markers
--------------

The HFP0 changes have been committed in a monolithic commit. This makes it a bit difficult to understand what's going on.
To make it easier, where possible changes have been marked with tags that identify the functionality to which they are related.

The following table briefly explains the meaning of the changer marker tags:

| Tag | Meaning |
| --- | ------- |
| REN | cosmetic renames (in docs, titles, output strings, GUI menus etc.)
| FRK | forking actions at triggering height
| DIF | difficulty algorithm update (MIDAS)
| BSZ | Adaptive block size algorithm based on BitPay implementation
| SED | DNS seeds and static IPs (emptied out / dummy values)
| PER | Peer handling during fork (rudimentary)
| CLI | Client version update
| ALR | Alert key disabling
| POW | fork to new POW code (overriden by config switch --disable-newpow)
| XTB | Xtreme Thin Blocks implementation
| DBG | fork related debugging traces (removable)
| CLN | Cleanup related to removal of obsolete code
| TST | Additional tests (unit, system) or fixes to tests
| CLT | BIP65 OP_CHECKLOCKTIMEVERIFY
| RLT | BIP68 Relative Lock-time using consensus-enforced sequence number
| CSV | BIP112 CHECKSEQUENCEVERIFY
| MTP | BIP113 Median time-past as endpoint for lock-time calculations
| TMP | temporary settings for testing only
| CRY | Cherry-picked misc. fixes from other clients which otherwise trouble testing


HFP0 Test Status
----------------

The C++ unit tests (`src/test/test_bitcoin`) should all pass.

Regression/integration tests can be run with `qa/pull-tester/rpc-tests.py` .

The tests `bip68-sequence.py` and `pruning.py` currently fail.
For bip68-sequence, it is presumed to be a policy code issue
that has been fixed in later versions of other clients (a similar issue
was present on Classic 0.12 builds, but has been fixed in Classic 1.1).
I have not identified the exact change that could fix it, but the
test failure is likely unrelated to changes made by HFP0.

The pruning test failure might be due to a presumed bug in how the
fork handles re-orgs across its trigger, but it could also be
something else.

Rudimentary hardfork tests are implemented, but they are lacking in some
test cases (re-orgs across the fork trigger height, and fine-grained
testing around the trigger height).

Adaptive block size tests are limited to the blocksizecalculator_tests
suite, and would need to be extended with more test cases and integration
tests.

There are no unit tests / regression tests for MIDAS yet.

