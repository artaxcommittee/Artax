// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Artax developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MERCHANTNODE_H
#define MERCHANTNODE_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

#define MERCHANTNODE_MIN_CONFIRMATIONS 15
#define MERCHANTNODE_MIN_MNP_SECONDS (10 * 60)
#define MERCHANTNODE_MIN_MNB_SECONDS (5 * 60)
#define MERCHANTNODE_PING_SECONDS (5 * 60)
#define MERCHANTNODE_EXPIRATION_SECONDS (120 * 60)
#define MERCHANTNODE_REMOVAL_SECONDS (130 * 60)
#define MERCHANTNODE_CHECK_SECONDS 5

#define MERCHANTNODE_COLLATERAL 2500

using namespace std;

class CMerchantnode;
class CMerchantnodeBroadcast;
class CMerchantnodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);


//
// The Merchantnode Ping Class : Contains a different serialize method for sending pings from merchantnodes throughout the network
//

class CMerchantnodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CMerchantnodePing();
    CMerchantnodePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true, bool fCheckSigTimeOnly = false);
    bool Sign(CKey& keyMerchantnode, CPubKey& pubKeyMerchantnode);
    bool VerifySignature(CPubKey& pubKeyMerchantnode, int &nDos);
    void Relay();

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CMerchantnodePing& first, CMerchantnodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    CMerchantnodePing& operator=(CMerchantnodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CMerchantnodePing& a, const CMerchantnodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CMerchantnodePing& a, const CMerchantnodePing& b)
    {
        return !(a == b);
    }
};

//
// The Merchantnode Class. It contains the input of the XAX collateral, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CMerchantnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    int64_t lastTimeChecked;

public:
    enum state {
        MERCHANTNODE_PRE_ENABLED,
        MERCHANTNODE_ENABLED,
        MERCHANTNODE_EXPIRED,
        MERCHANTNODE_OUTPOINT_SPENT,
        MERCHANTNODE_REMOVE,
        MERCHANTNODE_WATCHDOG_EXPIRED,
        MERCHANTNODE_POSE_BAN,
        MERCHANTNODE_VIN_SPENT,
        MERCHANTNODE_POS_ERROR
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyMerchantnode;
    CPubKey pubKeyCollateralAddress1;
    CPubKey pubKeyMerchantnode1;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //mnb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int nActiveState;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CMerchantnodePing lastPing;

    int64_t nLastDsee;  // temporary, do not save. Remove after migration to v12
    int64_t nLastDseep; // temporary, do not save. Remove after migration to v12

    CMerchantnode();
    CMerchantnode(const CMerchantnode& other);
    CMerchantnode(const CMerchantnodeBroadcast& mnb);

    void swap(CMerchantnode& first, CMerchantnode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyMerchantnode, second.pubKeyMerchantnode);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    }

    CMerchantnode& operator=(CMerchantnode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CMerchantnode& a, const CMerchantnode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CMerchantnode& a, const CMerchantnode& b)
    {
        return !(a.vin == b.vin);
    }

    uint256 CalculateScore(int mod = 1, int64_t nBlockHeight = 0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);

        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMerchantnode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(activeState);
        READWRITE(lastPing);
        READWRITE(cacheInputAge);
        READWRITE(cacheInputAgeBlock);
        READWRITE(unitTest);
        READWRITE(allowFreeTx);
        READWRITE(nLastDsq);
        READWRITE(nScanningErrorCount);
        READWRITE(nLastScanningErrorBlockHeight);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CMerchantnodeBroadcast& mnb);

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash + slice * 64, 64);
        return n;
    }

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CMerchantnodePing()) ? false : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CMerchantnodePing();
    }

    bool IsEnabled()
    {
        return activeState == MERCHANTNODE_ENABLED;
    }

    int GetMerchantnodeInputAge()
    {
        if (chainActive.Tip() == NULL) return 0;

        if (cacheInputAge == 0) {
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = chainActive.Tip()->nHeight;
        }

        return cacheInputAge + (chainActive.Tip()->nHeight - cacheInputAgeBlock);
    }

    std::string GetStatus();

    std::string Status()
    {
        std::string strStatus = "ACTIVE";

        if (activeState == CMerchantnode::MERCHANTNODE_ENABLED) strStatus = "ENABLED";
        if (activeState == CMerchantnode::MERCHANTNODE_EXPIRED) strStatus = "EXPIRED";
        if (activeState == CMerchantnode::MERCHANTNODE_VIN_SPENT) strStatus = "VIN_SPENT";
        if (activeState == CMerchantnode::MERCHANTNODE_REMOVE) strStatus = "REMOVE";
        if (activeState == CMerchantnode::MERCHANTNODE_POS_ERROR) strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid();
    bool IsValidNetAddr();
};


//
// The Merchantnode Broadcast Class : Contains a different serialize method for sending merchantnodes through the network
//

class CMerchantnodeBroadcast : public CMerchantnode
{
public:
    CMerchantnodeBroadcast();
    CMerchantnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CMerchantnodeBroadcast(const CMerchantnode& mn);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
    bool VerifySignature();
    void Relay();
    std::string GetStrMessage();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMerchantnode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);
        READWRITE(nLastDsq);
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubKeyCollateralAddress;
        return ss.GetHash();
    }

    /// Create Merchantnode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyMerchantnodeNew, CPubKey pubKeyMerchantnodeNew, std::string& strErrorRet, CMerchantnodeBroadcast& mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CMerchantnodeBroadcast& mnbRet, bool fOffline = false);
    static bool CheckDefaultPort(std::string strService, std::string& strErrorRet, std::string strContext);
};

#endif
