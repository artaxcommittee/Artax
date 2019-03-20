// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERCHANTNODEMAN_H
#define MERCHANTNODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "merchantnode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define MERCHANTNODES_DUMP_SECONDS (15 * 60)
#define MERCHANTNODES_DSEG_SECONDS (3 * 60 * 60)

using namespace std;

class CMerchantnodeMan;

extern CMerchantnodeMan mnodeman;
void DumpMerchantnodes();

/** Access to the MN database (mncache.dat)
 */
class CMerchantnodeDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CMerchantnodeDB();
    bool Write(const CMerchantnodeMan& mnodemanToSave);
    ReadResult Read(CMerchantnodeMan& mnodemanToLoad, bool fDryRun = false);
};

class CMerchantnodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CMerchantnode> vMerchantnodes;
    // who's asked for the Merchantnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForMerchantnodeList;
    // who we asked for the Merchantnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForMerchantnodeList;
    // which Merchantnodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForMerchantnodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CMerchantnodeBroadcast> mapSeenMerchantnodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CMerchantnodePing> mapSeenMerchantnodePing;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(vMerchantnodes);
        READWRITE(mAskedUsForMerchantnodeList);
        READWRITE(mWeAskedForMerchantnodeList);
        READWRITE(mWeAskedForMerchantnodeListEntry);

        READWRITE(mapSeenMerchantnodeBroadcast);
        READWRITE(mapSeenMerchantnodePing);
    }

    CMerchantnodeMan();
    CMerchantnodeMan(CMerchantnodeMan& other);

    /// Add an entry
    bool Add(CMerchantnode& mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode* pnode, CTxIn& vin);

    /// Check all Merchantnodes
    void Check();

    /// Check all Merchantnodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Merchantnode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CMerchantnode* Find(const CScript& payee);
    CMerchantnode* Find(const CTxIn& vin);
    CMerchantnode* Find(const CPubKey& pubKeyMerchantnode);

    /// Find an entry in the merchantnode list that is next to be paid
    CMerchantnode* GetNextMerchantnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CMerchantnode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CMerchantnode* GetCurrentMasterNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CMerchantnode> GetFullMerchantnodeVector()
    {
        Check();
        return vMerchantnodes;
    }

    std::vector<pair<int, CMerchantnode> > GetMerchantnodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetMerchantnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CMerchantnode* GetMerchantnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Merchantnodes
    int size() { return vMerchantnodes.size(); }

    /// Return the number of Merchantnodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update merchantnode list and maps using provided CMerchantnodeBroadcast
    void UpdateMerchantnodeList(CMerchantnodeBroadcast mnb);
};

#endif
