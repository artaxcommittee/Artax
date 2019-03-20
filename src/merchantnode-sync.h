// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERCHANTNODE_SYNC_H
#define MERCHANTNODE_SYNC_H

#define MERCHANTNODE_SYNC_INITIAL 0
#define MERCHANTNODE_SYNC_SPORKS 1
#define MERCHANTNODE_SYNC_LIST 2
#define MERCHANTNODE_SYNC_MNW 3
#define MERCHANTNODE_SYNC_BUDGET 4
#define MERCHANTNODE_SYNC_BUDGET_PROP 10
#define MERCHANTNODE_SYNC_BUDGET_FIN 11
#define MERCHANTNODE_SYNC_COMMUNITYVOTE 5
#define MERCHANTNODE_SYNC_COMMUNITYVOTE_PROP 20
#define MERCHANTNODE_SYNC_FAILED 998
#define MERCHANTNODE_SYNC_FINISHED 999

#define MERCHANTNODE_SYNC_TIMEOUT 5
#define MERCHANTNODE_SYNC_THRESHOLD 2

class CMerchantnodeSync;
extern CMerchantnodeSync merchantnodeSync;

//
// CMerchantnodeSync : Sync merchantnode assets in stages
//

class CMerchantnodeSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncMNW;
    std::map<uint256, int> mapSeenSyncBudget;
    std::map<uint256, int> mapSeenSyncCommunity;

    int64_t lastMerchantnodeList;
    int64_t lastMerchantnodeWinner;
    int64_t lastBudgetItem;
    int64_t lastCommunityItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumMerchantnodeList;
    int sumMerchantnodeWinner;
    int sumBudgetItemProp;
    int sumBudgetItemFin;
    int sumCommunityItemProp;
    // peers that reported counts
    int countMerchantnodeList;
    int countMerchantnodeWinner;
    int countBudgetItemProp;
    int countBudgetItemFin;
    int countCommunityItemProp;

    // Count peers we've requested the list from
    int RequestedMerchantnodeAssets;
    int RequestedMerchantnodeAttempt;

    // Time when current merchantnode asset sync started
    int64_t nAssetSyncStarted;

    CMerchantnodeSync();

    void AddedMerchantnodeList(uint256 hash);
    void AddedMerchantnodeWinner(uint256 hash);
    void AddedBudgetItem(uint256 hash);
    void AddedCommunityItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsBudgetFinEmpty();
    bool IsBudgetPropEmpty();
    bool IsCommunityPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    bool IsMerchantnodeListSynced() { return RequestedMerchantnodeAssets > MERCHANTNODE_SYNC_LIST; }
    void ClearFulfilledRequest();
};

#endif
