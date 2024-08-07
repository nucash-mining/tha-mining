// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_VARIANT_USE_RELAXED_GET_BY_DEFAULT
#include <boost/assign/list_of.hpp>

#include <pos.h>
#include <txdb.h>
#include <validation.h>
#include <arith_uint256.h>
#include <hash.h>
#include <node/blockstorage.h>
#include <timedata.h>
#include <chainparams.h>
#include <script/sign.h>
#include <script/solver.h>
#include <consensus/consensus.h>
#include <logging.h>

using namespace std;

// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
uint256 ComputeStakeModifier(const CBlockIndex* pindexPrev, const uint256& kernel)
{
    if (!pindexPrev)
        return uint256();  // genesis block's modifier is 0

    CDataStream ss(SER_GETHASH, 0);
    ss << kernel << pindexPrev->nStakeModifier;
    return Hash(ss);
}

// BTCW kernel protocol
// coinstake must meet hash target according to the protocol:
// kernel (input 0) must meet the formula
//     hash(nStakeModifier + blockFrom.nTime + txPrev.vout.hash + txPrev.vout.n + nTime ) < bnTarget; // nWeight removed to collapse PoS to PoW
// this ensures that the chance of getting a coinstake is proportional to the
// amount of coins one owns.
// The reason this hash is chosen is the following:
//   nStakeModifier: scrambles computation to make it very difficult to precompute
//                   future proof-of-stake
//   blockFrom.nTime: slightly scrambles computation
//   txPrev.vout.hash: hash of txPrev, to reduce the chance of nodes
//                     generating coinstake at the same time
//   txPrev.vout.n: output number of txPrev, to reduce the chance of nodes
//                  generating coinstake at the same time
//   nTime: current timestamp
//   block/tx hash should not be used here as they can be generated in vast
//   quantities so as to generate blocks faster, degrading the system back into
//   a proof-of-work situation.
//
bool CheckStakeKernelHash(CBlockIndex* pindexPrev, unsigned int nBits, uint32_t blockFromTime, CAmount prevoutValue, const COutPoint& prevout, unsigned int nTimeBlock, uint32_t nNonce, uint256& hashProofOfStake, uint256& targetProofOfStake, bool fPrintProofOfStake)
{
    if (nTimeBlock < blockFromTime)  // Transaction timestamp violation
        return error("CheckStakeKernelHash() : nTime violation");

    if (nNonce != 0xD0D0FACE)  // Proof of Transaction Work indicator
        return error("CheckStakeKernelHash() : nNonce violation");        

    // Base target with 0 PoS contribution
    arith_uint256 bnTarget;
    bnTarget.SetCompact(nBits);

    targetProofOfStake = ArithToUint256(bnTarget);
    bnTarget = POW_POT_DIFF_HELPER*bnTarget;
 

    uint256 nStakeModifier = pindexPrev->nStakeModifier;

    // Calculate hash
    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier;
    ss << blockFromTime << prevout.hash << prevout.n << nTimeBlock;
    hashProofOfStake = Hash(ss);

    if (fPrintProofOfStake) {
        LogPrint(BCLog::COINSTAKE, "CheckStakeKernelHash() : check modifier=%s nTimeBlockFrom=%u nPrevout=%u nTimeBlock=%u hashProof=%s\n",
            nStakeModifier.GetHex().c_str(),
            blockFromTime, prevout.n, nTimeBlock,
            hashProofOfStake.ToString());
    }

    // Now check if Collapsed proof-of-stake hash meets target protocol
    arith_uint256 actual = UintToArith256(hashProofOfStake);
    if (actual > bnTarget)
        return false;

    return true;
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStake(CBlockIndex* pindexPrev, BlockValidationState& state, const CTransaction& tx, unsigned int nBits, uint32_t nTimeBlock, uint32_t nNonce, uint256& hashProofOfStake, uint256& targetProofOfStake, CCoinsViewCache& view)
{
    if (!tx.IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx.GetHash().ToString());

    // Kernel (input 0) must match the stake hash target (nBits)
    const CTxIn& txin = tx.vin[0];

    Coin coinPrev;

    if (!view.GetCoin(txin.prevout, coinPrev))
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "bad-stake-prevout-doesnotexist", 
                            strprintf("CheckProofOfStake() : Stake prevout does not exist %s", txin.prevout.hash.ToString()));

    if (pindexPrev->nHeight + 1 - coinPrev.nHeight < COINBASE_MATURITY)
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "bad-stake-prevout-notmature", 
                            strprintf("CheckProofOfStake() : Stake prevout is not mature, expecting %i and only matured to %i", COINBASE_MATURITY, pindexPrev->nHeight + 1 - coinPrev.nHeight));
    
    CBlockIndex* blockFrom = pindexPrev->GetAncestor(coinPrev.nHeight);
    if (!blockFrom) 
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "bad-stake-prevout-couldnotload", 
                            strprintf("CheckProofOfStake() : Block at height %i for prevout can not be loaded", coinPrev.nHeight));

    // Check the staker min utxo value
    if(coinPrev.out.nValue < DEFAULT_STAKING_MIN_UTXO_VALUE)
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "stake-less-than-min-utxo",
                            strprintf("CheckProofOfStake() : Stake for block at height %i does not have the minimum amount required for staking", pindexPrev->nHeight + 1));

    // Verify signature
    if (!VerifySignature(coinPrev, txin.prevout.hash, tx, 0, SCRIPT_VERIFY_NONE))
        return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "bad-stake-signature-verify", 
                            strprintf("CheckProofOfStake() : VerifySignature failed on coinstake %s", tx.GetHash().ToString()));

    if (!CheckStakeKernelHash(pindexPrev, nBits, blockFrom->nTime, coinPrev.out.nValue, txin.prevout, nTimeBlock, nNonce, hashProofOfStake, targetProofOfStake, true))
        // may occur during initial download or if behind on block chain sync
        return state.Invalid(BlockValidationResult::BLOCK_HEADER_SYNC, "bad-stake-kernel-check", 
                            strprintf("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s", tx.GetHash().ToString(), hashProofOfStake.ToString()));
        
    return true;
}

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(uint32_t nTimeBlock)
{
    return (nTimeBlock & STAKE_TIMESTAMP_MASK) == 0;
}

bool CheckBlockInputPubKeyMatchesOutputPubKey(const CBlock& block, CCoinsViewCache& view) {
    Coin coinIn;
    if(!view.GetCoin(block.prevoutStake, coinIn)) {
        return error("%s: Could not fetch prevoutStake from UTXO set", __func__);
    }

    CTransactionRef coinstakeTx = block.vtx[1];
    if(coinstakeTx->vout.size() < 2) {
        return error("%s: coinstake transaction does not have the minimum number of outputs", __func__);
    }

    const CTxOut& txout = coinstakeTx->vout[1];

    if(coinIn.out.scriptPubKey == txout.scriptPubKey) {
        return true;
    }

    // If the input does not exactly match the output, it MUST be on P2PKH spent and P2PK out.
    CTxDestination inputAddress;
    TxoutType inputTxType=TxoutType::NONSTANDARD;
    if(!ExtractDestination(coinIn.out.scriptPubKey, inputAddress, &inputTxType)) {
        return error("%s: Could not extract address from input", __func__);
    }

    PKHash* pkhash_in = std::get_if<PKHash>(&inputAddress);
    if(inputTxType != TxoutType::PUBKEYHASH || (nullptr == pkhash_in)) {
        return error("%s: non-exact match input must be P2PKH", __func__);
    }

    CTxDestination outputAddress;
    TxoutType outputTxType=TxoutType::NONSTANDARD;
    if(!ExtractDestination(txout.scriptPubKey, outputAddress, &outputTxType)) {
        return error("%s: Could not extract address from output", __func__);
    }

    PKHash* pkhash_out = std::get_if<PKHash>(&outputAddress);
    if(outputTxType != TxoutType::PUBKEY || (nullptr == pkhash_out)) {
        return error("%s: non-exact match output must be P2PK", __func__);
    }

    // pkhash_in and pkhash_out verified non null from above.
    if( *pkhash_in != *pkhash_out ) {
        return error("%s: input P2PKH pubkey does not match output P2PK pubkey", __func__);
    }

    return true;
}

bool CheckRecoveredPubKeyFromBlockSignature(CBlockIndex* pindexPrev, const CBlockHeader& block, CCoinsViewCache& view) {

    Coin coinPrev;
    if(!view.GetCoin(block.prevoutStake, coinPrev)){
        if(!GetSpentCoinFromMainChain(pindexPrev, block.prevoutStake, &coinPrev)) {
            return error("CheckRecoveredPubKeyFromBlockSignature(): Could not find %s and it was not at the tip", block.prevoutStake.hash.GetHex());
        }
    }

    uint256 hash = block.GetHashWithoutSign();
    CPubKey pubkey;

    if(block.vchBlockSig.empty()) {
        return error("CheckRecoveredPubKeyFromBlockSignature(): Signature is empty\n");
    }

    for(uint8_t recid = 0; recid <= 3; ++recid) {
        for(uint8_t compressed = 0; compressed < 2; ++compressed) {
            if(!pubkey.RecoverLaxDER(hash, block.vchBlockSig, recid, compressed)) {
                continue;
            }

            CTxDestination address;
            TxoutType txType=TxoutType::NONSTANDARD;
            if(ExtractDestination(coinPrev.out.scriptPubKey, address, &txType)){
                PKHash* pkhash = std::get_if<PKHash>(&address);
                if ((txType == TxoutType::PUBKEY || txType == TxoutType::PUBKEYHASH) && pkhash) {
                    if(PKHash(pubkey.GetID()) == *pkhash) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool CheckKernel(CBlockIndex* pindexPrev, unsigned int nBits, uint32_t nTimeBlock, uint32_t nNonce, const COutPoint& prevout, CCoinsViewCache& view)
{
    std::map<COutPoint, CStakeCache> tmp;
    return CheckKernel(pindexPrev, nBits, nTimeBlock, nNonce, prevout, view, tmp);
}

bool CheckKernel(CBlockIndex* pindexPrev, unsigned int nBits, uint32_t nTimeBlock, uint32_t nNonce, const COutPoint& prevout, CCoinsViewCache& view, const std::map<COutPoint, CStakeCache>& cache)
{
    uint256 hashProofOfStake, targetProofOfStake;
    auto it=cache.find(prevout);
    if(it == cache.end()) {
        //not found in cache (shouldn't happen during staking, only during verification which does not use cache)
        Coin coinPrev;
        if(!view.GetCoin(prevout, coinPrev)){
            if(!GetSpentCoinFromMainChain(pindexPrev, prevout, &coinPrev)) {
                return error("CheckKernel(): Could not find coin and it was not at the tip");
            }
        }

        if(pindexPrev->nHeight + 1 - coinPrev.nHeight < COINBASE_MATURITY){
            return error("CheckKernel(): Coin not matured");
        }
        CBlockIndex* blockFrom = pindexPrev->GetAncestor(coinPrev.nHeight);
        if(!blockFrom) {
            return error("CheckKernel(): Could not find block");
        }
        if(coinPrev.IsSpent()){
            return error("CheckKernel(): Coin is spent");
        }

        return CheckStakeKernelHash(pindexPrev, nBits, blockFrom->nTime, coinPrev.out.nValue, prevout,
                                    nTimeBlock, nNonce, hashProofOfStake, targetProofOfStake);
    }else{
        //found in cache
        const CStakeCache& stake = it->second;
        if(CheckStakeKernelHash(pindexPrev, nBits, stake.blockFromTime, stake.amount, prevout,
                                    nTimeBlock, nNonce, hashProofOfStake, targetProofOfStake)){
            //Cache could potentially cause false positive stakes in the event of deep reorgs, so check without cache also
            return CheckKernel(pindexPrev, nBits, nTimeBlock, nNonce, prevout, view);
        }
    }
    return false;
}

void CacheKernel(std::map<COutPoint, CStakeCache>& cache, const COutPoint& prevout, CBlockIndex* pindexPrev, CCoinsViewCache& view){
    if(cache.find(prevout) != cache.end()){
        //already in cache
        return;
    }

    Coin coinPrev;
    if(!view.GetCoin(prevout, coinPrev)){
        return;
    }

    if(pindexPrev->nHeight + 1 - coinPrev.nHeight < COINBASE_MATURITY){
        return;
    }
    CBlockIndex* blockFrom = pindexPrev->GetAncestor(coinPrev.nHeight);
    if(!blockFrom) {
        return;
    }

    CStakeCache c(blockFrom->nTime, coinPrev.out.nValue);
    cache.insert({prevout, c});
}
