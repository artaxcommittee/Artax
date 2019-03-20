// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemerchantnode.h"
#include "merchantnode-payments.h"
#include "addrman.h"
#include "merchantnode-budget.h"
#include "merchantnode-sync.h"
#include "merchantnodeman.h"
#include "merchantnode-helpers.h"
#include "merchantnodeconfig.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CMerchantnodePayments merchantnodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapMerchantnodeBlocks;
CCriticalSection cs_mapMerchantnodePayeeVotes;

//
// CMerchantnodePaymentDB
//

CMerchantnodePaymentDB::CMerchantnodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "MerchantnodePayments";
}

bool CMerchantnodePaymentDB::Write(const CMerchantnodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // merchantnode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint("merchantnode","Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CMerchantnodePaymentDB::ReadResult CMerchantnodePaymentDB::Read(CMerchantnodePayments& objToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathDB.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathDB);
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

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (merchantnode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid merchantnode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CMerchantnodePayments object
        ssObj >> objToLoad;
    } catch (std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("merchantnode","Loaded info from mnpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("merchantnode","  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint("merchantnode","Merchantnode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint("merchantnode","Merchantnode payments manager - result:\n");
        LogPrint("merchantnode","  %s\n", objToLoad.ToString());
    }

    return Ok;
}

void DumpMerchantnodePayments()
{
    int64_t nStart = GetTimeMillis();

    CMerchantnodePaymentDB paymentdb;
    CMerchantnodePayments tempPayments;

    LogPrint("merchantnode","Verifying mnpayments.dat format...\n");
    CMerchantnodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMerchantnodePaymentDB::FileError)
        LogPrint("merchantnode","Missing budgets file - mnpayments.dat, will try to recreate\n");
    else if (readResult != CMerchantnodePaymentDB::Ok) {
        LogPrint("merchantnode","Error reading mnpayments.dat: ");
        if (readResult == CMerchantnodePaymentDB::IncorrectFormat)
            LogPrint("merchantnode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("merchantnode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("merchantnode","Writting info to mnpayments.dat...\n");
    paymentdb.Write(merchantnodePayments);

    LogPrint("merchantnode","Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint("merchantnode","IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    if (!merchantnodeSync.IsSynced()) { //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % GetBudgetPaymentCycleBlocks() < 100) {
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    } else { // we're synced and have data so check the budget schedule

        //are these blocks even enabled
        if (!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
            return nMinted <= nExpectedValue;
        }

        if (budget.IsBudgetPaymentBlock(nHeight)) {
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if (nMinted > nExpectedValue) {
                return false;
            }
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;

    if (!merchantnodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint("mnpayments", "Client not synced, skipping block payee checks\n");
        return true;
    }

    const CTransaction& txNew = (nBlockHeight > Params().LAST_POW_BLOCK() ? block.vtx[1] : block.vtx[0]);

    //check if it's a budget block
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
            transactionStatus = budget.IsTransactionValid(txNew, nBlockHeight);
            if (transactionStatus == TrxValidationStatus::Valid)
                return true;

            if (transactionStatus == TrxValidationStatus::InValid) {
                LogPrint("merchantnode","Invalid budget payment detected %s\n", txNew.ToString().c_str());
                if (IsSporkActive(SPORK_9_MERCHANTNODE_BUDGET_ENFORCEMENT))
                    return false;

                LogPrint("merchantnode","Budget enforcement is disabled, accepting block\n");
            }
        }
    }

    // If we end here the transaction was either TrxValidationStatus::InValid and Budget enforcement is disabled, or
    // a double budget payment (status = TrxValidationStatus::DoublePayment) was detected, or no/not enough merchantnode
    // votes (status = TrxValidationStatus::VoteThreshold) for a finalized budget were found
    // In all cases a merchantnode will get the payment for this block

    //check for merchantnode payee
    if (merchantnodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;
    LogPrint("merchantnode","Invalid mn payment detected %s\n", txNew.ToString().c_str());

    if (IsSporkActive(SPORK_8_MERCHANTNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrint("merchantnode","Merchantnode payment enforcement is disabled, accepting block\n");

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(pindexPrev->nHeight + 1)) {
        budget.FillBlockPayee(txNew, nFees, fProofOfStake);
    } else {
        merchantnodePayments.FillBlockPayee(txNew, nFees, fProofOfStake);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(nBlockHeight)) {
        return budget.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return merchantnodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CMerchantnodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!merchantnodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no merchantnode detected
        CMerchantnode* winningNode = mnodeman.GetCurrentMasterNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint("merchantnode","CreateNewBlock: Failed to detect merchantnode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nHeight);
    CAmount merchantnodePayment = GetMerchantnodePayment(pindexPrev->nHeight, blockValue);

    if (hasPayment) {
        if (fProofOfStake) {
            /**For Proof Of Stake vout[0] must be null
             * Stake reward can be split into many different outputs, so we must
             * use vout.size() to align with several different cases.
             * An additional output is appended as the merchantnode payment
             */
            unsigned int i = txNew.vout.size();
            txNew.vout.resize(i + 1);
            txNew.vout[i].scriptPubKey = payee;
            txNew.vout[i].nValue = merchantnodePayment;

            //subtract mn payment from the stake reward
            txNew.vout[i - 1].nValue -= merchantnodePayment;
        } else {
            txNew.vout.resize(2);
            txNew.vout[1].scriptPubKey = payee;
            txNew.vout[1].nValue = merchantnodePayment;
            txNew.vout[0].nValue = blockValue - merchantnodePayment;
        }

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("merchantnode","Merchantnode payment of %s to %s\n", FormatMoney(merchantnodePayment).c_str(), address2.ToString().c_str());
    }
}

int CMerchantnodePayments::GetMinMerchantnodePaymentsProto()
{
    if (IsSporkActive(SPORK_10_MERCHANTNODE_PAY_UPDATED_NODES))
        return ActiveProtocol();                          // Allow only updated peers
    else
        return MIN_PEER_PROTO_VERSION_BEFORE_ENFORCEMENT; // Also allow old peers as long as they are allowed to run
}

void CMerchantnodePayments::ProcessMessageMerchantnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!merchantnodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Merchantnode related functionality


    if (strCommand == "mnget") { //Merchantnode Payments Request Sync
        if (fLiteMode) return;   //disable all Merchantnode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest("mnget")) {
                LogPrintf("CMerchantnodePayments::ProcessMessageMerchantnodePayments() : mnget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }

        pfrom->FulfilledRequest("mnget");
        merchantnodePayments.Sync(pfrom, nCountNeeded);
        LogPrint("mnpayments", "mnget - Sent Merchantnode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "mnw") { //Merchantnode Payments Declare Winner
        //this is required in litemodef
        CMerchantnodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (merchantnodePayments.mapMerchantnodePayeeVotes.count(winner.GetHash())) {
            LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            merchantnodeSync.AddedMerchantnodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            // if(strError != "") LogPrint("merchantnode","mnw - invalid message - %s\n", strError);
            return;
        }

        if (!merchantnodePayments.CanVote(winner.vinMerchantnode.prevout, winner.nBlockHeight)) {
            //  LogPrint("merchantnode","mnw - merchantnode already voted - %s\n", winner.vinMerchantnode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            if (merchantnodeSync.IsSynced()) {
                LogPrintf("CMerchantnodePayments::ProcessMessageMerchantnodePayments() : mnw - invalid signature\n");
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced merchantnode
            mnodeman.AskForMN(pfrom, winner.vinMerchantnode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        //   LogPrint("mnpayments", "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinMerchantnode.prevout.ToStringShort());

        if (merchantnodePayments.AddWinningMerchantnode(winner)) {
            winner.Relay();
            merchantnodeSync.AddedMerchantnodeWinner(winner.GetHash());
        }
    }
}

bool CMerchantnodePaymentWinner::Sign(CKey& keyMerchantnode, CPubKey& pubKeyMerchantnode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    std::string strMessage = vinMerchantnode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             payee.ToString();

    if (!merchantnodeSigner.SignMessage(strMessage, errorMessage, vchSig, keyMerchantnode)) {
        LogPrint("merchantnode","CMerchantnodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!merchantnodeSigner.VerifyMessage(pubKeyMerchantnode, vchSig, strMessage, errorMessage)) {
        LogPrint("merchantnode","CMerchantnodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CMerchantnodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapMerchantnodeBlocks.count(nBlockHeight)) {
        return mapMerchantnodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this merchantnode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMerchantnodePayments::IsScheduled(CMerchantnode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapMerchantnodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapMerchantnodeBlocks.count(h)) {
            if (mapMerchantnodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CMerchantnodePayments::AddWinningMerchantnode(CMerchantnodePaymentWinner& winnerIn)
{
    uint256 blockHash = 0;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100))
        return false;

    {
        LOCK2(cs_mapMerchantnodePayeeVotes, cs_mapMerchantnodeBlocks);

        if (mapMerchantnodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapMerchantnodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapMerchantnodeBlocks.count(winnerIn.nBlockHeight)) {
            CMerchantnodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapMerchantnodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapMerchantnodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CMerchantnodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    int nMerchantnode_Drift_Count = 0;

    std::string strPayeesPossible = "";

    CAmount nReward = GetBlockValue(nBlockHeight);

    if (IsSporkActive(SPORK_8_MERCHANTNODE_PAYMENT_ENFORCEMENT)) {
        // Get a stable number of merchantnodes by ignoring newly activated (< 8000 sec old) merchantnodes
        nMerchantnode_Drift_Count = mnodeman.stable_size() + Params().MerchantnodeCountDrift();
    }
    else {
        //account for the fact that all peers do not see the same merchantnode count. A allowance of being off our merchantnode count is given
        //we only need to look at an increased merchantnode count because as count increases, the reward decreases. This code only checks
        //for mnPayment >= required, so it only makes sense to check the max node count allowed.
        nMerchantnode_Drift_Count = mnodeman.size() + Params().MerchantnodeCountDrift();
    }

    CAmount requiredMerchantnodePayment = GetMerchantnodePayment(nBlockHeight, nReward, nMerchantnode_Drift_Count);

    //require at least 6 signatures
    BOOST_FOREACH (CMerchantnodePayee& payee, vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    BOOST_FOREACH (CMerchantnodePayee& payee, vecPayments) {
        bool found = false;
        BOOST_FOREACH (CTxOut out, txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredMerchantnodePayment)
                    found = true;
                else
                    LogPrint("merchantnode","Merchantnode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredMerchantnodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrint("merchantnode","CMerchantnodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredMerchantnodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CMerchantnodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (CMerchantnodePayee& payee : vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if (ret != "Unknown") {
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CMerchantnodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMerchantnodeBlocks);

    if (mapMerchantnodeBlocks.count(nBlockHeight)) {
        return mapMerchantnodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMerchantnodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapMerchantnodeBlocks);

    if (mapMerchantnodeBlocks.count(nBlockHeight)) {
        return mapMerchantnodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CMerchantnodePayments::CleanPaymentList()
{
    LOCK2(cs_mapMerchantnodePayeeVotes, cs_mapMerchantnodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CMerchantnodePaymentWinner>::iterator it = mapMerchantnodePayeeVotes.begin();
    while (it != mapMerchantnodePayeeVotes.end()) {
        CMerchantnodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CMerchantnodePayments::CleanPaymentList - Removing old Merchantnode payment - block %d\n", winner.nBlockHeight);
            merchantnodeSync.mapSeenSyncMNW.erase((*it).first);
            mapMerchantnodePayeeVotes.erase(it++);
            mapMerchantnodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CMerchantnodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CMerchantnode* pmn = mnodeman.Find(vinMerchantnode);

    if (!pmn) {
        strError = strprintf("Unknown Merchantnode %s", vinMerchantnode.prevout.hash.ToString());
        LogPrint("merchantnode","CMerchantnodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinMerchantnode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Merchantnode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("merchantnode","CMerchantnodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetMerchantnodeRank(vinMerchantnode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have merchantnodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Merchantnode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("merchantnode","CMerchantnodePaymentWinner::IsValid - %s\n", strError);
            //if (merchantnodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CMerchantnodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fMasterNode) return false;

    //reference node - hybrid mode

    int n = mnodeman.GetMerchantnodeRank(activeMerchantnode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrint("mnpayments", "CMerchantnodePayments::ProcessBlock - Unknown Merchantnode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CMerchantnodePayments::ProcessBlock - Merchantnode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CMerchantnodePaymentWinner newWinner(activeMerchantnode.vin);

    if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
    } else {
        LogPrint("merchantnode","CMerchantnodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMerchantnode.vin.prevout.hash.ToString());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CMerchantnode* pmn = mnodeman.GetNextMerchantnodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn != NULL) {
            LogPrint("merchantnode","CMerchantnodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            LogPrint("merchantnode","CMerchantnodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint("merchantnode","CMerchantnodePayments::ProcessBlock() Failed to find merchantnode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeyMerchantnode;
    CKey keyMerchantnode;

    if (!merchantnodeSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMerchantnode, pubKeyMerchantnode)) {
        LogPrint("merchantnode","CMerchantnodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint("merchantnode","CMerchantnodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyMerchantnode, pubKeyMerchantnode)) {
        LogPrint("merchantnode","CMerchantnodePayments::ProcessBlock() - AddWinningMerchantnode\n");

        if (AddWinningMerchantnode(newWinner)) {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CMerchantnodePaymentWinner::Relay()
{
    CInv inv(MSG_MERCHANTNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CMerchantnodePaymentWinner::SignatureValid()
{
    CMerchantnode* pmn = mnodeman.Find(vinMerchantnode);

    if (pmn != NULL) {
        std::string strMessage = vinMerchantnode.prevout.ToStringShort() +
                                 boost::lexical_cast<std::string>(nBlockHeight) +
                                 payee.ToString();

        std::string errorMessage = "";
        if (!merchantnodeSigner.VerifyMessage(pmn->pubKeyMerchantnode, vchSig, strMessage, errorMessage)) {
            return error("CMerchantnodePaymentWinner::SignatureValid() - Got bad Merchantnode address signature %s\n", vinMerchantnode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CMerchantnodePayments::Sync(CNode* node, int nCountNeeded)
{
    LOCK(cs_mapMerchantnodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CMerchantnodePaymentWinner>::iterator it = mapMerchantnodePayeeVotes.begin();
    while (it != mapMerchantnodePayeeVotes.end()) {
        CMerchantnodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_MERCHANTNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    node->PushMessage("ssc", MERCHANTNODE_SYNC_MNW, nInvCount);
}

std::string CMerchantnodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMerchantnodePayeeVotes.size() << ", Blocks: " << (int)mapMerchantnodeBlocks.size();

    return info.str();
}


int CMerchantnodePayments::GetOldestBlock()
{
    LOCK(cs_mapMerchantnodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CMerchantnodeBlockPayees>::iterator it = mapMerchantnodeBlocks.begin();
    while (it != mapMerchantnodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}


int CMerchantnodePayments::GetNewestBlock()
{
    LOCK(cs_mapMerchantnodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CMerchantnodeBlockPayees>::iterator it = mapMerchantnodeBlocks.begin();
    while (it != mapMerchantnodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
