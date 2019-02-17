// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "merchantnode.h"
#include "clientversion.h"

#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapMerchantnodeBlocks;
extern CCriticalSection cs_mapMerchantnodePayeeVotes;

class CMerchantnodePayments;
class CMerchantnodePaymentWinner;
class CMerchantnodeBlockPayees;

extern CMerchantnodePayments merchantnodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageMerchantnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake);

void DumpMerchantnodePayments();

/** Save Merchantnode Payment Data (mnpayments.dat)
 */
class CMerchantnodePaymentDB
{
private:
    boost::filesystem::path pathDB;
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

    CMerchantnodePaymentDB();
    bool Write(const CMerchantnodePayments& objToSave);
    ReadResult Read(CMerchantnodePayments& objToLoad, bool fDryRun = false);
};

class CMerchantnodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CMerchantnodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CMerchantnodePayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(scriptPubKey);
        READWRITE(nVotes);
    }
};

// Keep track of votes for payees from merchantnodes
class CMerchantnodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CMerchantnodePayee> vecPayments;

    CMerchantnodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CMerchantnodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        for (CMerchantnodePayee& payee : vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CMerchantnodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        for (CMerchantnodePayee& p : vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(CScript payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        for (CMerchantnodePayee& p : vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CMerchantnodePaymentWinner
{
public:
    CTxIn vinMerchantnode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CMerchantnodePaymentWinner()
    {
        nBlockHeight = 0;
        vinMerchantnode = CTxIn();
        payee = CScript();
    }

    CMerchantnodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinMerchantnode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinMerchantnode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyMerchantnode, CPubKey& pubKeyMerchantnode);
    bool IsValid(CNode* pnode, std::string& strError);
    bool SignatureValid();
    void Relay();

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinMerchantnode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinMerchantnode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// Merchantnode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMerchantnodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CMerchantnodePaymentWinner> mapMerchantnodePayeeVotes;
    std::map<int, CMerchantnodeBlockPayees> mapMerchantnodeBlocks;
    std::map<uint256, int> mapMerchantnodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CMerchantnodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapMerchantnodeBlocks, cs_mapMerchantnodePayeeVotes);
        mapMerchantnodeBlocks.clear();
        mapMerchantnodePayeeVotes.clear();
    }

    bool AddWinningMerchantnode(CMerchantnodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CMerchantnode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CMerchantnode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outMerchantnode, int nBlockHeight)
    {
        LOCK(cs_mapMerchantnodePayeeVotes);

        if (mapMerchantnodesLastVote.count(outMerchantnode.hash + outMerchantnode.n)) {
            if (mapMerchantnodesLastVote[outMerchantnode.hash + outMerchantnode.n] == nBlockHeight) {
                return false;
            }
        }

        //record this merchantnode voted
        mapMerchantnodesLastVote[outMerchantnode.hash + outMerchantnode.n] = nBlockHeight;
        return true;
    }

    int GetMinMerchantnodePaymentsProto();
    void ProcessMessageMerchantnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapMerchantnodePayeeVotes);
        READWRITE(mapMerchantnodeBlocks);
    }
};


#endif
