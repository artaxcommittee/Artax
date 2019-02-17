// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Artax developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "merchantnode.h"
#include "addrman.h"
#include "merchantnodeman.h"
#include "merchantnode-payments.h"
#include "merchantnode-helpers.h"
#include "sync.h"
#include "util.h"
#include "init.h"
#include "wallet.h"
#include "activemerchantnode.h"

#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenMerchantnodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if (nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if (mapCacheBlockHashes.count(nBlockHeight)) {
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex* BlockLastSolved = chainActive.Tip();
    const CBlockIndex* BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight + 1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if (nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight + 1) - nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nBlocksAgo) {
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CMerchantnode::CMerchantnode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyMerchantnode = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = MASTERNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMerchantnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = MASTERNODE_ENABLED,
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

CMerchantnode::CMerchantnode(const CMerchantnode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeyMerchantnode = other.pubKeyMerchantnode;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    nActiveState = MASTERNODE_ENABLED,
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
    nLastDsee = other.nLastDsee;   // temporary, do not save. Remove after migration to v12
    nLastDseep = other.nLastDseep; // temporary, do not save. Remove after migration to v12
}

CMerchantnode::CMerchantnode(const CMerchantnodeBroadcast& mnb)
{
    LOCK(cs);
    vin = mnb.vin;
    addr = mnb.addr;
    pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
    pubKeyMerchantnode = mnb.pubKeyMerchantnode;
    sig = mnb.sig;
    activeState = MASTERNODE_ENABLED;
    sigTime = mnb.sigTime;
    lastPing = mnb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = MASTERNODE_ENABLED,
    protocolVersion = mnb.protocolVersion;
    nLastDsq = mnb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

//
// When a new merchantnode broadcast is sent, update our information
//
bool CMerchantnode::UpdateFromNewBroadcast(CMerchantnodeBroadcast& mnb)
{
    if (mnb.sigTime > sigTime) {
        pubKeyMerchantnode = mnb.pubKeyMerchantnode;
        pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        sigTime = mnb.sigTime;
        sig = mnb.sig;
        protocolVersion = mnb.protocolVersion;
        addr = mnb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if (mnb.lastPing == CMerchantnodePing() || (mnb.lastPing != CMerchantnodePing() && mnb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenMerchantnodePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Merchantnode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CMerchantnode::CalculateScore(int mod, int64_t nBlockHeight)
{
    if (chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if (!GetBlockHash(hash, nBlockHeight)) {
        LogPrint("merchantnode","CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CMerchantnode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < MASTERNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == MASTERNODE_VIN_SPENT) return;


    if (!IsPingedWithin(MASTERNODE_REMOVAL_SECONDS)) {
        activeState = MASTERNODE_REMOVE;
        return;
    }

    if (!IsPingedWithin(MASTERNODE_EXPIRATION_SECONDS)) {
        activeState = MASTERNODE_EXPIRED;
        return;
    }

    if (lastPing.sigTime - sigTime < MASTERNODE_MIN_MNP_SECONDS){
    	activeState = MASTERNODE_PRE_ENABLED;
    	return;
    }

    if (!unitTest) {
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut((MASTERNODE_COLLATERAL-0.01) * COIN, merchantnodeSigner.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;

            if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
                activeState = MASTERNODE_VIN_SPENT;
                return;
            }
        }
    }

    activeState = MASTERNODE_ENABLED; // OK
}

int64_t CMerchantnode::SecondsSincePayment()
{
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CMerchantnode::GetLastPaid()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150;

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex* BlockReading = chainActive.Tip();

    int nMnCount = mnodeman.CountEnabled() * 1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nMnCount) {
            return 0;
        }
        n++;

        if (merchantnodePayments.mapMerchantnodeBlocks.count(BlockReading->nHeight)) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (merchantnodePayments.mapMerchantnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

std::string CMerchantnode::GetStatus()
{
    switch (nActiveState) {
    case CMerchantnode::MASTERNODE_PRE_ENABLED:
        return "PRE_ENABLED";
    case CMerchantnode::MASTERNODE_ENABLED:
        return "ENABLED";
    case CMerchantnode::MASTERNODE_EXPIRED:
        return "EXPIRED";
    case CMerchantnode::MASTERNODE_OUTPOINT_SPENT:
        return "OUTPOINT_SPENT";
    case CMerchantnode::MASTERNODE_REMOVE:
        return "REMOVE";
    case CMerchantnode::MASTERNODE_WATCHDOG_EXPIRED:
        return "WATCHDOG_EXPIRED";
    case CMerchantnode::MASTERNODE_POSE_BAN:
        return "POSE_BAN";
    default:
        return "UNKNOWN";
    }
}

bool CMerchantnode::IsValidNetAddr()
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkID() == CBaseChainParams::REGTEST ||
           (IsReachable(addr) && addr.IsRoutable());
}

CMerchantnodeBroadcast::CMerchantnodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyMerchantnode1 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = MASTERNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMerchantnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CMerchantnodeBroadcast::CMerchantnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyMerchantnodeNew, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyMerchantnode = pubKeyMerchantnodeNew;
    sig = std::vector<unsigned char>();
    activeState = MASTERNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CMerchantnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CMerchantnodeBroadcast::CMerchantnodeBroadcast(const CMerchantnode& mn)
{
    vin = mn.vin;
    addr = mn.addr;
    pubKeyCollateralAddress = mn.pubKeyCollateralAddress;
    pubKeyMerchantnode = mn.pubKeyMerchantnode;
    sig = mn.sig;
    activeState = mn.activeState;
    sigTime = mn.sigTime;
    lastPing = mn.lastPing;
    cacheInputAge = mn.cacheInputAge;
    cacheInputAgeBlock = mn.cacheInputAgeBlock;
    unitTest = mn.unitTest;
    allowFreeTx = mn.allowFreeTx;
    protocolVersion = mn.protocolVersion;
    nLastDsq = mn.nLastDsq;
    nScanningErrorCount = mn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = mn.nLastScanningErrorBlockHeight;
}

bool CMerchantnodeBroadcast::Create(std::string strService, std::string strKeyMerchantnode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CMerchantnodeBroadcast& mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyMerchantnodeNew;
    CKey keyMerchantnodeNew;

    //need correct blocks to send ping
    if (!fOffline && !merchantnodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Merchantnode";
        LogPrint("merchantnode","CMerchantnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!merchantnodeSigner.GetKeysFromSecret(strKeyMerchantnode, keyMerchantnodeNew, pubKeyMerchantnodeNew)) {
        strErrorRet = strprintf("Invalid merchantnode key %s", strKeyMerchantnode);
        LogPrint("merchantnode","CMerchantnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetMerchantnodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for merchantnode %s", strTxHash, strOutputIndex, strService);
        LogPrint("merchantnode","CMerchantnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    // The service needs the correct default port to work properly
    if (!CheckDefaultPort(strService, strErrorRet, "CMerchantnodeBroadcast::Create"))
        return false;

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyMerchantnodeNew, pubKeyMerchantnodeNew, strErrorRet, mnbRet);
}

bool CMerchantnodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyMerchantnodeNew, CPubKey pubKeyMerchantnodeNew, std::string& strErrorRet, CMerchantnodeBroadcast& mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("merchantnode", "CMerchantnodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyMerchantnodeNew.GetID() = %s\n",
        CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
        pubKeyMerchantnodeNew.GetID().ToString());

    CMerchantnodePing mnp(txin);
    if (!mnp.Sign(keyMerchantnodeNew, pubKeyMerchantnodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, merchantnode=%s", txin.prevout.hash.ToString());
        LogPrint("merchantnode","CMerchantnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CMerchantnodeBroadcast();
        return false;
    }

    mnbRet = CMerchantnodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyMerchantnodeNew, PROTOCOL_VERSION);

    // if (!mnbRet.IsValidNetAddr()) {
    //     strErrorRet = strprintf("Invalid IP address %s, merchantnode=%s", mnbRet.addr.ToStringIP (), txin.prevout.hash.ToString());
    //     LogPrint("merchantnode","CMerchantnodeBroadcast::Create -- %s\n", strErrorRet);
    //     mnbRet = CMerchantnodeBroadcast();
    //     return false;
    // }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, merchantnode=%s", txin.prevout.hash.ToString());
        LogPrint("merchantnode","CMerchantnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CMerchantnodeBroadcast();
        return false;
    }

    return true;
}

bool CMerchantnodeBroadcast::CheckDefaultPort(std::string strService, std::string& strErrorRet, std::string strContext)
{
    return true;
    CService service = CService(strService);
    int nDefaultPort = Params().GetDefaultPort();

    if (service.GetPort() != nDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for merchantnode %s, only %d is supported on %s-net.",
                                service.GetPort(), strService, nDefaultPort, Params().NetworkIDString());
        LogPrint("merchantnode", "%s - %s\n", strContext, strErrorRet);
        return false;
    }

    return true;
}

bool CMerchantnodeBroadcast::CheckAndUpdate(int& nDos)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("merchantnode","mnb - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    // incorrect ping or its sigTime
    if (lastPing == CMerchantnodePing() || !lastPing.CheckAndUpdate(nDos, false, true))
        return false;

    if (protocolVersion < merchantnodePayments.GetMinMerchantnodePaymentsProto()) {
        LogPrint("merchantnode","mnb - ignoring outdated Merchantnode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrint("merchantnode","mnb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyMerchantnode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrint("merchantnode","mnb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrint("merchantnode","mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!merchantnodeSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetStrMessage(), errorMessage)) {
        nDos = 100;
        return error("CMerchantnodeBroadcast::CheckAndUpdate - Got bad Merchantnode address signature : %s", errorMessage);
    }

    // if (Params().NetworkID() == CBaseChainParams::MAIN) {
    //     if (addr.GetPort() != 9333) return false;
    // } else if (addr.GetPort() == 9333)
    //     return false;

    //search existing Merchantnode list, this is where we update existing Merchantnodes with new mnb broadcasts
    CMerchantnode* pmn = mnodeman.Find(vin);

    // no such merchantnode, nothing to update
    if (pmn == NULL) return true;

    // this broadcast is older or equal than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    // (mapSeenMerchantnodeBroadcast in CMerchantnodeMan::ProcessMessage should filter legit duplicates)
    if (pmn->sigTime >= sigTime)
        return error("CMerchantnodeBroadcast::CheckAndUpdate - Bad sigTime %d for Merchantnode %20s %105s (existing broadcast is at %d)",
					  sigTime, addr.ToString(), vin.ToString(), pmn->sigTime);

    // merchantnode is not enabled yet/already, nothing to update
    if (!pmn->IsEnabled()) return true;

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(MASTERNODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("merchantnode","mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            if (pmn->IsEnabled()) Relay();
        }
        merchantnodeSync.AddedMerchantnodeList(GetHash());
    }

    return true;
}

bool CMerchantnodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a merchantnode with the same vin (i.e. already activated) and this mnb is ours (matches our Merchantnode privkey)
    // so nothing to do here for us
    if (fMasterNode && vin.prevout == activeMerchantnode.vin.prevout && pubKeyMerchantnode == activeMerchantnode.pubKeyMerchantnode)
        return true;

    // incorrect ping or its sigTime
    if (lastPing == CMerchantnodePing() || !lastPing.CheckAndUpdate(nDoS, false, true)) return false;

    // search existing Merchantnode list
    CMerchantnode* pmn = mnodeman.Find(vin);

    if (pmn != NULL) {
        // nothing to do here if we already know about this merchantnode and it's enabled
        if (pmn->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else
            mnodeman.Remove(pmn->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CTxOut vout = CTxOut((MASTERNODE_COLLATERAL-0.01) * COIN, merchantnodeSigner.collateralPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            mnodeman.mapSeenMerchantnodeBroadcast.erase(GetHash());
            merchantnodeSync.mapSeenSyncMNB.erase(GetHash());
            return false;
        }

        if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            //set nDos
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("merchantnode", "mnb - Accepted Merchantnode entry\n");

    if (GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS) {
        LogPrint("merchantnode","mnb - Input must have at least %d confirmations\n", MASTERNODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenMerchantnodeBroadcast.erase(GetHash());
        merchantnodeSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when XAX collateral tx got MASTERNODE_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 PIVX tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + MASTERNODE_MIN_CONFIRMATIONS - 1]; // block where tx got MASTERNODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrint("merchantnode","mnb - Bad sigTime %d for Merchantnode %s (%i conf block is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), MASTERNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrint("merchantnode","mnb - Got NEW Merchantnode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CMerchantnode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Merchantnode privkey, then we've been remotely activated
    if (pubKeyMerchantnode == activeMerchantnode.pubKeyMerchantnode && protocolVersion == PROTOCOL_VERSION) {
        activeMerchantnode.EnableHotColdMasterNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal) Relay();

    return true;
}

void CMerchantnodeBroadcast::Relay()
{
    CInv inv(MSG_MASTERNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CMerchantnodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;
    sigTime = GetAdjustedTime();

    std::string strMessage = GetStrMessage();

    if (!merchantnodeSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress))
        return error("CMerchantnodeBroadcast::Sign() - Error: %s", errorMessage);

    if (!merchantnodeSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage))
        return error("CMerchantnodeBroadcast::Sign() - Error: %s", errorMessage);

    return true;
}

bool CMerchantnodeBroadcast::VerifySignature()
{
    std::string errorMessage;

    if (!merchantnodeSigner.VerifyMessage(pubKeyCollateralAddress, sig, GetStrMessage(), errorMessage))
        return error("CMerchantnodeBroadcast::VerifySignature() - Error: %s", errorMessage);

    return true;
}

std::string CMerchantnodeBroadcast::GetStrMessage()
{
    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyMerchantnode.begin(), pubKeyMerchantnode.end());
    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);
    return strMessage;
}

CMerchantnodePing::CMerchantnodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CMerchantnodePing::CMerchantnodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CMerchantnodePing::Sign(CKey& keyMerchantnode, CPubKey& pubKeyMerchantnode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!merchantnodeSigner.SignMessage(strMessage, errorMessage, vchSig, keyMerchantnode)) {
        LogPrint("merchantnode","CMerchantnodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!merchantnodeSigner.VerifyMessage(pubKeyMerchantnode, vchSig, strMessage, errorMessage)) {
        LogPrint("merchantnode","CMerchantnodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CMerchantnodePing::VerifySignature(CPubKey& pubKeyMerchantnode, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string errorMessage = "";

    if (!merchantnodeSigner.VerifyMessage(pubKeyMerchantnode, vchSig, strMessage, errorMessage)){
        nDos = 33;
        return error("CMerchantnodePing::VerifySignature - Got bad Merchantnode ping signature %s Error: %s", vin.ToString(), errorMessage);
    }
    return true;
}

bool CMerchantnodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled, bool fCheckSigTimeOnly)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("merchantnode","CMerchantnodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint("merchantnode","CMerchantnodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    if (fCheckSigTimeOnly) {
        CMerchantnode* pmn = mnodeman.Find(vin);
        if (pmn) return VerifySignature(pmn->pubKeyMerchantnode, nDos);
        return true;
    }

    LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate - New Ping - %s - %s - %lli\n", GetHash().ToString(), blockHash.ToString(), sigTime);

    // see if we have this Merchantnode
    CMerchantnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL && pmn->protocolVersion >= merchantnodePayments.GetMinMerchantnodePaymentsProto()) {
        if (fRequireEnabled && !pmn->IsEnabled()) return false;

        // LogPrint("merchantnode","mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this merchantnode or
        // last ping was more then MASTERNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(MASTERNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        	if (!VerifySignature(pmn->pubKeyMerchantnode, nDos))
                return false;

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrint("merchantnode","CMerchantnodePing::CheckAndUpdate - Merchantnode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Merchantnode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrint("merchantnode","CMerchantnodePing::CheckAndUpdate - Merchantnode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pmn->lastPing = *this;

            //mnodeman.mapSeenMerchantnodeBroadcast.lastPing is probably outdated, so we'll update it
            CMerchantnodeBroadcast mnb(*pmn);
            uint256 hash = mnb.GetHash();
            if (mnodeman.mapSeenMerchantnodeBroadcast.count(hash)) {
                mnodeman.mapSeenMerchantnodeBroadcast[hash].lastPing = *this;
            }

            pmn->Check(true);
            if (!pmn->IsEnabled()) return false;

            LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate - Merchantnode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate - Merchantnode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("merchantnode", "CMerchantnodePing::CheckAndUpdate - Couldn't find compatible Merchantnode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CMerchantnodePing::Relay()
{
    CInv inv(MSG_MASTERNODE_PING, GetHash());
    RelayInv(inv);
}
