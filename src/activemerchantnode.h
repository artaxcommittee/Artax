// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEMERCHANTNODE_H
#define ACTIVEMERCHANTNODE_H

#include "init.h"
#include "key.h"
#include "merchantnode.h"
#include "net.h"
#include "sync.h"
#include "wallet.h"

#define ACTIVE_MERCHANTNODE_INITIAL 0 // initial state
#define ACTIVE_MERCHANTNODE_SYNC_IN_PROCESS 1
#define ACTIVE_MERCHANTNODE_INPUT_TOO_NEW 2
#define ACTIVE_MERCHANTNODE_NOT_CAPABLE 3
#define ACTIVE_MERCHANTNODE_STARTED 4

// Responsible for activating the Merchantnode and pinging the network
class CActiveMerchantnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Merchantnode
    bool SendMerchantnodePing(std::string& errorMessage);

    /// Create Merchantnode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyMerchantnode, CPubKey pubKeyMerchantnode, std::string& errorMessage, CMerchantnodeBroadcast &mnb);

    /// Get XAX collateral input that can be used for the Merchantnode
    bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
    // Initialized by init.cpp
    // Keys for the main Merchantnode
    CPubKey pubKeyMerchantnode;

    // Initialized while registering Merchantnode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveMerchantnode()
    {
        status = ACTIVE_MERCHANTNODE_INITIAL;
    }

    /// Manage status of main Merchantnode
    void ManageStatus();
    std::string GetStatus();

    /// Create Merchantnode broadcast, needs to be relayed manually after that
    bool CreateBroadcast(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage, CMerchantnodeBroadcast &mnb, bool fOffline = false);

    /// Get XAX collateral input that can be used for the Merchantnode
    bool GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsMerchantnode();

    /// Enable cold wallet mode (run a Merchantnode with no funds)
    bool EnableHotColdMasterNode(CTxIn& vin, CService& addr);
};

extern CActiveMerchantnode activeMerchantnode;

#endif
