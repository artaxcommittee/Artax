// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Artax developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "merchantnodeman.h"
#include "activemerchantnode.h"
#include "merchantnode-payments.h"
#include "merchantnode-helpers.h"
#include "addrman.h"
#include "merchantnode.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > MASTERNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Merchantnode manager */
CMerchantnodeMan mnodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const pair<int64_t, CMerchantnode>& t1,
        const pair<int64_t, CMerchantnode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CMerchantnodeDB
//

CMerchantnodeDB::CMerchantnodeDB()
{
    pathMN = GetDataDir() / "mncache.dat";
    strMagicMessage = "MerchantnodeCache";
}

bool CMerchantnodeDB::Write(const CMerchantnodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssMerchantnodes(SER_DISK, CLIENT_VERSION);
    ssMerchantnodes << strMagicMessage;                   // merchantnode cache file specific magic message
    ssMerchantnodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssMerchantnodes << mnodemanToSave;
    uint256 hash = Hash(ssMerchantnodes.begin(), ssMerchantnodes.end());
    ssMerchantnodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssMerchantnodes;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("merchantnode","Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("merchantnode","  %s\n", mnodemanToSave.ToString());

    return true;
}

CMerchantnodeDB::ReadResult CMerchantnodeDB::Read(CMerchantnodeMan& mnodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssMerchantnodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssMerchantnodes.begin(), ssMerchantnodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (merchantnode cache file specific magic message) and ..

        ssMerchantnodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid merchantnode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssMerchantnodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CMerchantnodeMan object
        ssMerchantnodes >> mnodemanToLoad;
    } catch (std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("merchantnode","Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("merchantnode","  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("merchantnode","Merchantnode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint("merchantnode","Merchantnode manager - result:\n");
        LogPrint("merchantnode","  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpMerchantnodes()
{
    int64_t nStart = GetTimeMillis();

    CMerchantnodeDB mndb;
    CMerchantnodeMan tempMnodeman;

    LogPrint("merchantnode","Verifying mncache.dat format...\n");
    CMerchantnodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMerchantnodeDB::FileError)
        LogPrint("merchantnode","Missing merchantnode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CMerchantnodeDB::Ok) {
        LogPrint("merchantnode","Error reading mncache.dat: ");
        if (readResult == CMerchantnodeDB::IncorrectFormat)
            LogPrint("merchantnode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("merchantnode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("merchantnode","Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint("merchantnode","Merchantnode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CMerchantnodeMan::CMerchantnodeMan()
{
}

bool CMerchantnodeMan::Add(CMerchantnode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CMerchantnode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("merchantnode", "CMerchantnodeMan: Adding new Merchantnode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vMerchantnodes.push_back(mn);
        return true;
    }

    return false;
}

void CMerchantnodeMan::AskForMN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForMerchantnodeListEntry.find(vin.prevout);
    if (i != mWeAskedForMerchantnodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the mnb info once from the node that sent mnp

    LogPrint("merchantnode", "CMerchantnodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + MASTERNODE_MIN_MNP_SECONDS;
    mWeAskedForMerchantnodeListEntry[vin.prevout] = askAgain;
}

void CMerchantnodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CMerchantnode& mn, vMerchantnodes) {
        mn.Check();
    }
}

void CMerchantnodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CMerchantnode>::iterator it = vMerchantnodes.begin();
    while (it != vMerchantnodes.end()) {
        if ((*it).activeState == CMerchantnode::MASTERNODE_REMOVE ||
            (*it).activeState == CMerchantnode::MASTERNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CMerchantnode::MASTERNODE_EXPIRED) ||
            (*it).protocolVersion < merchantnodePayments.GetMinMerchantnodePaymentsProto()) {
            LogPrint("merchantnode", "CMerchantnodeMan: Removing inactive Merchantnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new mnb
            map<uint256, CMerchantnodeBroadcast>::iterator it3 = mapSeenMerchantnodeBroadcast.begin();
            while (it3 != mapSeenMerchantnodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    merchantnodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenMerchantnodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this merchantnode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForMerchantnodeListEntry.begin();
            while (it2 != mWeAskedForMerchantnodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForMerchantnodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vMerchantnodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Merchantnode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForMerchantnodeList.begin();
    while (it1 != mAskedUsForMerchantnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForMerchantnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Merchantnode list
    it1 = mWeAskedForMerchantnodeList.begin();
    while (it1 != mWeAskedForMerchantnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForMerchantnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Merchantnodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForMerchantnodeListEntry.begin();
    while (it2 != mWeAskedForMerchantnodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForMerchantnodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenMerchantnodeBroadcast
    map<uint256, CMerchantnodeBroadcast>::iterator it3 = mapSeenMerchantnodeBroadcast.begin();
    while (it3 != mapSeenMerchantnodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMerchantnodeBroadcast.erase(it3++);
            merchantnodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenMerchantnodePing
    map<uint256, CMerchantnodePing>::iterator it4 = mapSeenMerchantnodePing.begin();
    while (it4 != mapSeenMerchantnodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (MASTERNODE_REMOVAL_SECONDS * 2)) {
            mapSeenMerchantnodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CMerchantnodeMan::Clear()
{
    LOCK(cs);
    vMerchantnodes.clear();
    mAskedUsForMerchantnodeList.clear();
    mWeAskedForMerchantnodeList.clear();
    mWeAskedForMerchantnodeListEntry.clear();
    mapSeenMerchantnodeBroadcast.clear();
    mapSeenMerchantnodePing.clear();
}

int CMerchantnodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nMerchantnode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMerchantnode_Age = 0;

    for (CMerchantnode& mn : vMerchantnodes) {
        if (mn.protocolVersion < nMinProtocol)
            continue; // Skip obsolete versions

        if (IsSporkActive (SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            nMerchantnode_Age = GetAdjustedTime() - mn.sigTime;
            if (nMerchantnode_Age < nMerchantnode_Min_Age)
                continue; // Skip merchantnodes younger than (default) 8000 sec (MUST be > MASTERNODE_REMOVAL_SECONDS)
        }
        mn.Check ();
        if (!mn.IsEnabled ())
            continue; // Skip not-enabled merchantnodes

        nStable_size++;
    }

    return nStable_size;
}

int CMerchantnodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? merchantnodePayments.GetMinMerchantnodePaymentsProto() : protocolVersion;

    for (CMerchantnode& mn : vMerchantnodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CMerchantnodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? merchantnodePayments.GetMinMerchantnodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CMerchantnode& mn, vMerchantnodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

void CMerchantnodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForMerchantnodeList.find(pnode->addr);
            if (it != mWeAskedForMerchantnodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("merchantnode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
    mWeAskedForMerchantnodeList[pnode->addr] = askAgain;
}

CMerchantnode* CMerchantnodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CMerchantnode& mn, vMerchantnodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CMerchantnode* CMerchantnodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CMerchantnode& mn, vMerchantnodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CMerchantnode* CMerchantnodeMan::Find(const CPubKey& pubKeyMerchantnode)
{
    LOCK(cs);

    BOOST_FOREACH (CMerchantnode& mn, vMerchantnodes) {
        if (mn.pubKeyMerchantnode == pubKeyMerchantnode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best merchantnode to pay on the network
//
CMerchantnode* CMerchantnodeMan::GetNextMerchantnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CMerchantnode* pBestMerchantnode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecMerchantnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    for (CMerchantnode& mn : vMerchantnodes) {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        //check protocol version
        if (mn.protocolVersion < merchantnodePayments.GetMinMerchantnodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (merchantnodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are merchantnodes
        if (mn.GetMerchantnodeInputAge() < nMnCount) continue;

        vecMerchantnodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecMerchantnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextMerchantnodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecMerchantnodeLastPaid.rbegin(), vecMerchantnodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecMerchantnodeLastPaid) {
        CMerchantnode* pmn = Find(s.second);
        if (!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestMerchantnode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestMerchantnode;
}

CMerchantnode* CMerchantnodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? merchantnodePayments.GetMinMerchantnodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("merchantnode", "CMerchantnodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("merchantnode", "CMerchantnodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    for (CMerchantnode& mn : vMerchantnodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        for (CTxIn& usedVin : vecToExclude) {
            if (mn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &mn;
        }
    }

    return NULL;
}

CMerchantnode* CMerchantnodeMan::GetCurrentMasterNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CMerchantnode* winner = NULL;

    // scan for winner
    for (CMerchantnode& mn : vMerchantnodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Merchantnode
        uint256 n = mn.CalculateScore(mod, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &mn;
        }
    }

    return winner;
}

int CMerchantnodeMan::GetMerchantnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecMerchantnodeScores;
    int64_t nMerchantnode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nMerchantnode_Age = 0;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    for (CMerchantnode& mn : vMerchantnodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint("merchantnode","Skipping Merchantnode with obsolete version %d\n", mn.protocolVersion);
            continue;                                                       // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            nMerchantnode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nMerchantnode_Age) < nMerchantnode_Min_Age) {
                if (fDebug) LogPrint("merchantnode","Skipping just activated Merchantnode. Age: %ld\n", nMerchantnode_Age);
                continue;                                                   // Skip merchantnodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMerchantnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecMerchantnodeScores.rbegin(), vecMerchantnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    for (PAIRTYPE(int64_t, CTxIn) & s : vecMerchantnodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout)
            return rank;
    }

    return -1;
}

std::vector<pair<int, CMerchantnode> > CMerchantnodeMan::GetMerchantnodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CMerchantnode> > vecMerchantnodeScores;
    std::vector<pair<int, CMerchantnode> > vecMerchantnodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return vecMerchantnodeRanks;

    // scan for winner
    BOOST_FOREACH (CMerchantnode& mn, vMerchantnodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol) continue;

        if (!mn.IsEnabled()) {
            vecMerchantnodeScores.push_back(make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMerchantnodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecMerchantnodeScores.rbegin(), vecMerchantnodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CMerchantnode) & s, vecMerchantnodeScores) {
        rank++;
        vecMerchantnodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecMerchantnodeRanks;
}

CMerchantnode* CMerchantnodeMan::GetMerchantnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecMerchantnodeScores;

    // scan for winner
    BOOST_FOREACH (CMerchantnode& mn, vMerchantnodes) {
        if (mn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecMerchantnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecMerchantnodeScores.rbegin(), vecMerchantnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecMerchantnodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CMerchantnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Merchantnode related functionality
    if (!merchantnodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "mnb") { //Merchantnode Broadcast
        CMerchantnodeBroadcast mnb;
        vRecv >> mnb;

        if (mapSeenMerchantnodeBroadcast.count(mnb.GetHash())) { //seen
            merchantnodeSync.AddedMerchantnodeList(mnb.GetHash());
            return;
        }
        mapSeenMerchantnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));

        int nDoS = 0;
        if (!mnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Merchantnode
        //  - this is expensive, so it's only done once per Merchantnode
        if (!merchantnodeSigner.IsVinAssociatedWithPubkey(mnb.vin, mnb.pubKeyCollateralAddress)) {
            LogPrintf("CMerchantnodeMan::ProcessMessage() : mnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        // make sure it's still unspent
        if (mnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(mnb.addr), pfrom->addr, 2 * 60 * 60);
            merchantnodeSync.AddedMerchantnodeList(mnb.GetHash());
        } else {
            LogPrint("merchantnode","mnb - Rejected Merchantnode entry %s\n", mnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "mnp") { //Merchantnode Ping
        CMerchantnodePing mnp;
        vRecv >> mnp;

        LogPrint("merchantnode", "mnp - Merchantnode ping, vin: %s\n", mnp.vin.prevout.hash.ToString());

        if (mapSeenMerchantnodePing.count(mnp.GetHash())) return; //seen
        mapSeenMerchantnodePing.insert(make_pair(mnp.GetHash(), mnp));

        int nDoS = 0;
        if (mnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Merchantnode list
            CMerchantnode* pmn = Find(mnp.vin);
            // if it's known, don't ask for the mnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a merchantnode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == "dseg") { //Get Merchantnode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForMerchantnodeList.find(pfrom->addr);
                if (i != mAskedUsForMerchantnodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        LogPrintf("CMerchantnodeMan::ProcessMessage() : dseg - peer already asked me for the list\n");
                        Misbehaving(pfrom->GetId(), 34);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + MASTERNODES_DSEG_SECONDS;
                mAskedUsForMerchantnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CMerchantnode& mn, vMerchantnodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("merchantnode", "dseg - Sending Merchantnode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CMerchantnodeBroadcast mnb = CMerchantnodeBroadcast(mn);
                    uint256 hash = mnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_MASTERNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenMerchantnodeBroadcast.count(hash)) mapSeenMerchantnodeBroadcast.insert(make_pair(hash, mnb));

                    if (vin == mn.vin) {
                        LogPrint("merchantnode", "dseg - Sent 1 Merchantnode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", MASTERNODE_SYNC_LIST, nInvCount);
            LogPrint("merchantnode", "dseg - Sent %d Merchantnode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
}

void CMerchantnodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CMerchantnode>::iterator it = vMerchantnodes.begin();
    while (it != vMerchantnodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("merchantnode", "CMerchantnodeMan: Removing Merchantnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vMerchantnodes.erase(it);
            break;
        }
        ++it;
    }
}

void CMerchantnodeMan::UpdateMerchantnodeList(CMerchantnodeBroadcast mnb)
{
    mapSeenMerchantnodePing.insert(make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
    mapSeenMerchantnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
    merchantnodeSync.AddedMerchantnodeList(mnb.GetHash());

    LogPrint("merchantnode","CMerchantnodeMan::UpdateMerchantnodeList() -- merchantnode=%s\n", mnb.vin.prevout.ToString());

    CMerchantnode* pmn = Find(mnb.vin);
    if (pmn == NULL) {
        CMerchantnode mn(mnb);
        Add(mn);
    } else {
    	pmn->UpdateFromNewBroadcast(mnb);
    }
}

std::string CMerchantnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Merchantnodes: " << (int)vMerchantnodes.size() << ", peers who asked us for Merchantnode list: " << (int)mAskedUsForMerchantnodeList.size() << ", peers we asked for Merchantnode list: " << (int)mWeAskedForMerchantnodeList.size() << ", entries in Merchantnode list we asked for: " << (int)mWeAskedForMerchantnodeListEntry.size();

    return info.str();
}
