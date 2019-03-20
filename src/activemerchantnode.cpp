// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Artax developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemerchantnode.h"
#include "addrman.h"
#include "merchantnode.h"
#include "merchantnodeconfig.h"
#include "merchantnodeman.h"
#include "merchantnode-helpers.h"
#include "protocol.h"
#include "spork.h"

// Keep track of the active Merchantnode
CActiveMerchantnode activeMerchantnode;

//
// Bootup the Merchantnode, look for a XAX collateral input and register on the network
//
void CActiveMerchantnode::ManageStatus()
{
    std::string errorMessage;

    if (!fMasterNode) return;

    if (fDebug) LogPrintf("CActiveMerchantnode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkID() != CBaseChainParams::REGTEST && !merchantnodeSync.IsBlockchainSynced()) {
        status = ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMerchantnode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS) status = ACTIVE_MERCHANTNODE_INITIAL;

    if (status == ACTIVE_MERCHANTNODE_INITIAL) {
        CMerchantnode* pmn;
        pmn = mnodeman.Find(pubKeyMerchantnode);
        if (pmn != NULL) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_MERCHANTNODE_STARTED) {
        // Set defaults
        status = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveMerchantnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveMerchantnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strMasterNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the merchantnodeaddr configuration option.";
                LogPrintf("CActiveMerchantnode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strMasterNodeAddr);
        }

        // The service needs the correct default port to work properly
        if(!CMerchantnodeBroadcast::CheckDefaultPort(strMasterNodeAddr, errorMessage, "CActiveMerchantnode::ManageStatus()"))
            return;

        LogPrintf("CActiveMerchantnode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode* pnode = ConnectNode((CAddress)service, NULL);
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveMerchantnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetInputAge(vin) < MERCHANTNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_MERCHANTNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveMerchantnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyMerchantnode;
            CKey keyMerchantnode;

            if (!merchantnodeSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMerchantnode, pubKeyMerchantnode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CMerchantnodeBroadcast mnb;
            if (!CreateBroadcast(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyMerchantnode, pubKeyMerchantnode, errorMessage, mnb)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("CActiveMerchantnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //send to all peers
            LogPrintf("CActiveMerchantnode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay();

            LogPrintf("CActiveMerchantnode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_MERCHANTNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveMerchantnode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendMerchantnodePing(errorMessage)) {
        LogPrintf("CActiveMerchantnode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveMerchantnode::GetStatus()
{
    switch (status) {
    case ACTIVE_MERCHANTNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Merchantnode";
    case ACTIVE_MERCHANTNODE_INPUT_TOO_NEW:
        return strprintf("Merchantnode input must have at least %d confirmations", MERCHANTNODE_MIN_CONFIRMATIONS);
    case ACTIVE_MERCHANTNODE_NOT_CAPABLE:
        return "Not capable merchantnode: " + notCapableReason;
    case ACTIVE_MERCHANTNODE_STARTED:
        return "Merchantnode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveMerchantnode::SendMerchantnodePing(std::string& errorMessage)
{
    if (status != ACTIVE_MERCHANTNODE_STARTED) {
        errorMessage = "Merchantnode is not in a running status";
        return false;
    }

    CPubKey pubKeyMerchantnode;
    CKey keyMerchantnode;

    if (!merchantnodeSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMerchantnode, pubKeyMerchantnode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveMerchantnode::SendMerchantnodePing() - Relay Merchantnode Ping vin = %s\n", vin.ToString());

    CMerchantnodePing mnp(vin);
    if (!mnp.Sign(keyMerchantnode, pubKeyMerchantnode)) {
        errorMessage = "Couldn't sign Merchantnode Ping";
        return false;
    }

    // Update lastPing for our merchantnode in Merchantnode list
    CMerchantnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(MERCHANTNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Merchantnode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenMerchantnodePing.insert(make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenMerchantnodeBroadcast.lastPing is probably outdated, so we'll update it
        CMerchantnodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenMerchantnodeBroadcast.count(hash)) mnodeman.mapSeenMerchantnodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        return true;
    } else {
        // Seems like we are trying to send a ping while the Merchantnode is not registered in the network
        errorMessage = "Merchantnode List doesn't include our Merchantnode, shutting down Merchantnode pinging service! " + vin.ToString();
        status = ACTIVE_MERCHANTNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveMerchantnode::CreateBroadcast(std::string strService, std::string strKeyMerchantnode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CMerchantnodeBroadcast &mnb, bool fOffline)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyMerchantnode;
    CKey keyMerchantnode;

    //need correct blocks to send ping
    if (!fOffline && !merchantnodeSync.IsBlockchainSynced()) {
        errorMessage = "Sync in progress. Must wait until sync is complete to start Merchantnode";
        LogPrintf("CActiveMerchantnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!merchantnodeSigner.SetKey(strKeyMerchantnode, errorMessage, keyMerchantnode, pubKeyMerchantnode)) {
        errorMessage = strprintf("Can't find keys for merchantnode %s - %s", strService, errorMessage);
        LogPrintf("CActiveMerchantnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    if (!GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for merchantnode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveMerchantnode::CreateBroadcast() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);

    // The service needs the correct default port to work properly
    if(!CMerchantnodeBroadcast::CheckDefaultPort(strService, errorMessage, "CActiveMerchantnode::CreateBroadcast()"))
        return false;

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);

    return CreateBroadcast(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyMerchantnode, pubKeyMerchantnode, errorMessage, mnb);
}

bool CActiveMerchantnode::CreateBroadcast(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyMerchantnode, CPubKey pubKeyMerchantnode, std::string& errorMessage, CMerchantnodeBroadcast &mnb)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CMerchantnodePing mnp(vin);
    if (!mnp.Sign(keyMerchantnode, pubKeyMerchantnode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveMerchantnode::CreateBroadcast() -  %s\n", errorMessage);
        mnb = CMerchantnodeBroadcast();
        return false;
    }

    mnb = CMerchantnodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyMerchantnode, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if (!mnb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveMerchantnode::CreateBroadcast() - %s\n", errorMessage);
        mnb = CMerchantnodeBroadcast();
        return false;
    }

    return true;
}

bool CActiveMerchantnode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetMasterNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveMerchantnode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if (!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsMerchantnode();
    COutput* selectedOutput;

    // Find the vin
    if (!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex;
        try {
            outputIndex = std::stoi(strOutputIndex.c_str());
        } catch (const std::exception& e) {
            LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
            return false;
        }

        bool found = false;
        BOOST_FOREACH (COutput& out, possibleCoins) {
            if (out.tx->GetHash() == txHash && out.i == outputIndex) {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if (!found) {
            LogPrintf("CActiveMerchantnode::GetMasterNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveMerchantnode::GetMasterNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

// Extract Merchantnode vin information from output
bool CActiveMerchantnode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActiveMerchantnode::GetMasterNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf("CActiveMerchantnode::GetMasterNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Merchantnode
vector<COutput> CActiveMerchantnode::SelectCoinsMerchantnode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from merchantnode.conf
    if (GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH (CMerchantnodeConfig::CMerchantnodeEntry mne, merchantnodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());

            int nIndex;
            if (!mne.castOutputIndex(nIndex))
                continue;

            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from merchantnode.conf back if they where temporary unlocked
    if (!confLockedCoins.empty()) {
        BOOST_FOREACH (COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    for (const COutput& out : vCoins) {
        if (out.tx->vout[out.i].nValue == MERCHANTNODE_COLLATERAL * COIN)
            filteredCoins.push_back(out);
    }
    return filteredCoins;
}

// when starting a Merchantnode, this can enable to run as a hot wallet with no funds
bool CActiveMerchantnode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if (!fMasterNode) return false;

    status = ACTIVE_MERCHANTNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveMerchantnode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
