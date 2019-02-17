// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "activemerchantnode.h"
#include "merchantnode-sync.h"
#include "merchantnode-payments.h"
#include "merchantnode-budget.h"
#include "merchantnode-vote.h"
#include "merchantnode.h"
#include "merchantnodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CMerchantnodeSync;
CMerchantnodeSync merchantnodeSync;

CMerchantnodeSync::CMerchantnodeSync()
{
    Reset();
}

bool CMerchantnodeSync::IsSynced()
{
    return RequestedMerchantnodeAssets == MASTERNODE_SYNC_FINISHED;
}

bool CMerchantnodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CMerchantnodeSync::Reset()
{
    lastMerchantnodeList = 0;
    lastMerchantnodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumMerchantnodeList = 0;
    sumMerchantnodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countMerchantnodeList = 0;
    countMerchantnodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    sumCommunityItemProp = 0;
    countCommunityItemProp = 0;
    RequestedMerchantnodeAssets = MASTERNODE_SYNC_INITIAL;
    RequestedMerchantnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CMerchantnodeSync::AddedMerchantnodeList(uint256 hash)
{
    if (mnodeman.mapSeenMerchantnodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastMerchantnodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastMerchantnodeList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CMerchantnodeSync::AddedMerchantnodeWinner(uint256 hash)
{
    if (merchantnodePayments.mapMerchantnodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastMerchantnodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastMerchantnodeWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
}

void CMerchantnodeSync::AddedBudgetItem(uint256 hash)
{
    if (budget.mapSeenMerchantnodeBudgetProposals.count(hash) || budget.mapSeenMerchantnodeBudgetVotes.count(hash) ||
        budget.mapSeenFinalizedBudgets.count(hash) || budget.mapSeenFinalizedBudgetVotes.count(hash)) {
        if (mapSeenSyncBudget[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.insert(make_pair(hash, 1));
    }
}

void CMerchantnodeSync::AddedCommunityItem(uint256 hash)
{
    if (communityVote.mapSeenMerchantnodeCommunityProposals.count(hash) || communityVote.mapSeenMerchantnodeCommunityVotes.count(hash)) {
        if (mapSeenSyncCommunity[hash] < MASTERNODE_SYNC_THRESHOLD) {
            lastCommunityItem = GetTime();
            mapSeenSyncCommunity[hash]++;
        }
    } else {
        lastCommunityItem = GetTime();
        mapSeenSyncCommunity.insert(make_pair(hash, 1));
    }
}

bool CMerchantnodeSync::IsBudgetPropEmpty()
{
    return sumBudgetItemProp == 0 && countBudgetItemProp > 0;
}

bool CMerchantnodeSync::IsBudgetFinEmpty()
{
    return sumBudgetItemFin == 0 && countBudgetItemFin > 0;
}

bool CMerchantnodeSync::IsCommunityPropEmpty()
{
    return sumCommunityItemProp == 0 && countCommunityItemProp > 0;
}

void CMerchantnodeSync::GetNextAsset()
{
    switch (RequestedMerchantnodeAssets) {
    case (MASTERNODE_SYNC_INITIAL):
    case (MASTERNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest();
        RequestedMerchantnodeAssets = MASTERNODE_SYNC_SPORKS;
        break;
    case (MASTERNODE_SYNC_SPORKS):
        RequestedMerchantnodeAssets = MASTERNODE_SYNC_LIST;
        break;
    case (MASTERNODE_SYNC_LIST):
        RequestedMerchantnodeAssets = MASTERNODE_SYNC_MNW;
        break;
    case (MASTERNODE_SYNC_MNW):
        RequestedMerchantnodeAssets = MASTERNODE_SYNC_BUDGET;
        break;
    case (MASTERNODE_SYNC_BUDGET):
        RequestedMerchantnodeAssets = MASTERNODE_SYNC_COMMUNITYVOTE;
        break;
    case (MASTERNODE_SYNC_COMMUNITYVOTE):
        LogPrintf("CMerchantnodeSync::GetNextAsset - Sync has finished\n");
        RequestedMerchantnodeAssets = MASTERNODE_SYNC_FINISHED;
        break;
    }
    RequestedMerchantnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CMerchantnodeSync::GetSyncStatus()
{
    switch (merchantnodeSync.RequestedMerchantnodeAssets) {
    case MASTERNODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case MASTERNODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case MASTERNODE_SYNC_LIST:
        return _("Synchronizing merchantnodes...");
    case MASTERNODE_SYNC_MNW:
        return _("Synchronizing merchantnode winners...");
    case MASTERNODE_SYNC_BUDGET:
        return _("Synchronizing budgets...");
    case MASTERNODE_SYNC_COMMUNITYVOTE:
        return _("Synchronizing community proposals...");
    case MASTERNODE_SYNC_FAILED:
        return _("Synchronization failed");
    case MASTERNODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CMerchantnodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedMerchantnodeAssets >= MASTERNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (MASTERNODE_SYNC_LIST):
            if (nItemID != RequestedMerchantnodeAssets) return;
            sumMerchantnodeList += nCount;
            countMerchantnodeList++;
            break;
        case (MASTERNODE_SYNC_MNW):
            if (nItemID != RequestedMerchantnodeAssets) return;
            sumMerchantnodeWinner += nCount;
            countMerchantnodeWinner++;
            break;
        case (MASTERNODE_SYNC_BUDGET_PROP):
            if (RequestedMerchantnodeAssets != MASTERNODE_SYNC_BUDGET) return;
            sumBudgetItemProp += nCount;
            countBudgetItemProp++;
            break;
        case (MASTERNODE_SYNC_BUDGET_FIN):
            if (RequestedMerchantnodeAssets != MASTERNODE_SYNC_BUDGET) return;
            sumBudgetItemFin += nCount;
            countBudgetItemFin++;
            break;
        case (MASTERNODE_SYNC_COMMUNITYVOTE_PROP):
            if (RequestedMerchantnodeAssets != MASTERNODE_SYNC_COMMUNITYVOTE) return;
            sumCommunityItemProp += nCount;
            countCommunityItemProp++;
            break;
        }

        LogPrint("merchantnode", "CMerchantnodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CMerchantnodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    for (CNode* pnode : vNodes) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("mnsync");
        pnode->ClearFulfilledRequest("mnwsync");
        pnode->ClearFulfilledRequest("busync");
        pnode->ClearFulfilledRequest("comsync");
    }
}

void CMerchantnodeSync::Process()
{
    static int tick = 0;

    if (tick++ % MASTERNODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /*
            Resync if we lose all merchantnodes from sleep/wake or failure to sync originally
        */
        if (mnodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedMerchantnodeAssets == MASTERNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedMerchantnodeAssets == MASTERNODE_SYNC_FAILED) {
        return;
    }

    LogPrint("merchantnode", "CMerchantnodeSync::Process() - tick %d RequestedMerchantnodeAssets %d\n", tick, RequestedMerchantnodeAssets);

    if (RequestedMerchantnodeAssets == MASTERNODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (Params().NetworkID() != CBaseChainParams::REGTEST &&
        !IsBlockchainSynced() && RequestedMerchantnodeAssets > MASTERNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (RequestedMerchantnodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if (RequestedMerchantnodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (RequestedMerchantnodeAttempt < 6) {
                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync merchantnode votes
            } else {
                RequestedMerchantnodeAssets = MASTERNODE_SYNC_FINISHED;
            }
            RequestedMerchantnodeAttempt++;
            return;
        }

        //set to synced
        if (RequestedMerchantnodeAssets == MASTERNODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if (RequestedMerchantnodeAttempt >= 2) GetNextAsset();
            RequestedMerchantnodeAttempt++;

            return;
        }

        if (pnode->nVersion >= merchantnodePayments.GetMinMerchantnodePaymentsProto()) {
            if (RequestedMerchantnodeAssets == MASTERNODE_SYNC_LIST) {
                LogPrint("merchantnode", "CMerchantnodeSync::Process() - lastMerchantnodeList %lld (GetTime() - MASTERNODE_SYNC_TIMEOUT) %lld\n", lastMerchantnodeList, GetTime() - MASTERNODE_SYNC_TIMEOUT);
                if (lastMerchantnodeList > 0 && lastMerchantnodeList < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnsync")) continue;
                pnode->FulfilledRequest("mnsync");

                // timeout
                if (lastMerchantnodeList == 0 &&
                    (RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CMerchantnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedMerchantnodeAssets = MASTERNODE_SYNC_FAILED;
                        RequestedMerchantnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3) return;

                mnodeman.DsegUpdate(pnode);
                RequestedMerchantnodeAttempt++;
                return;
            }

            if (RequestedMerchantnodeAssets == MASTERNODE_SYNC_MNW) {
                if (lastMerchantnodeWinner > 0 && lastMerchantnodeWinner < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("mnwsync")) continue;
                pnode->FulfilledRequest("mnwsync");

                // timeout
                if (lastMerchantnodeWinner == 0 &&
                    (RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CMerchantnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedMerchantnodeAssets = MASTERNODE_SYNC_FAILED;
                        RequestedMerchantnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return;

                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("mnget", nMnCount); //sync payees
                RequestedMerchantnodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= ActiveProtocol()) {
            if (RequestedMerchantnodeAssets == MASTERNODE_SYNC_BUDGET) {

                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD) {

                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our merchantnode if possible
                    activeMerchantnode.ManageStatus();

                    return;
                }

                // timeout
                if (lastBudgetItem == 0 &&
                    (RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset();
                    activeMerchantnode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if (RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3) return;

                uint256 n = 0;
                pnode->PushMessage("mnvs", n); //sync merchantnode votes
                RequestedMerchantnodeAttempt++;

                return;
            }

            if (RequestedMerchantnodeAssets == MASTERNODE_SYNC_COMMUNITYVOTE) {

                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastCommunityItem > 0 && lastCommunityItem < GetTime() - MASTERNODE_SYNC_TIMEOUT * 2 && RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD) {

                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our merchantnode if possible
                    activeMerchantnode.ManageStatus();

                    return;
                }

                // timeout
                if (lastCommunityItem == 0 &&
                    (RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > MASTERNODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is are no community proposals at all, so just finish syncing
                    GetNextAsset();
                    activeMerchantnode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("comsync")) continue;
                pnode->FulfilledRequest("comsync");

                if (RequestedMerchantnodeAttempt >= MASTERNODE_SYNC_THRESHOLD * 3) return;

                uint256 n = 0;
                pnode->PushMessage("mncvs", n); //sync merchantnode community votes
                RequestedMerchantnodeAttempt++;

                return;
            }
        }
    }
}
