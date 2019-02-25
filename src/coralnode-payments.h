// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CORALNODE_PAYMENTS_H
#define CORALNODE_PAYMENTS_H

#include "key.h"
#include "main.h"
#include "coralnode.h"
#include <boost/lexical_cast.hpp>

using namespace std;

extern CCriticalSection cs_vecPayments;
extern CCriticalSection cs_mapCoralnodeBlocks;
extern CCriticalSection cs_mapCoralnodePayeeVotes;

class CCoralnodePayments;
class CCoralnodePaymentWinner;
class CCoralnodeBlockPayees;

extern CCoralnodePayments coralnodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageCoralnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(const CBlock& block, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, CAmount nFees, bool fProofOfStake, bool IsMasternode );

void DumpCoralnodePayments();

/** Save Coralnode Payment Data (fnpayments.dat)
 */
class CCoralnodePaymentDB
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

    CCoralnodePaymentDB();
    bool Write(const CCoralnodePayments& objToSave);
    ReadResult Read(CCoralnodePayments& objToLoad, bool fDryRun = false);
};

class CCoralnodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CCoralnodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CCoralnodePayee(CScript payee, int nVotesIn)
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

// Keep track of votes for payees from coralnodes
class CCoralnodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CCoralnodePayee> vecPayments;

    CCoralnodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CCoralnodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(CScript payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        BOOST_FOREACH (CCoralnodePayee& payee, vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CCoralnodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee)
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        BOOST_FOREACH (CCoralnodePayee& p, vecPayments) {
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

        BOOST_FOREACH (CCoralnodePayee& p, vecPayments) {
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
class CCoralnodePaymentWinner
{
public:
    CTxIn vinCoralnode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CCoralnodePaymentWinner()
    {
        nBlockHeight = 0;
        vinCoralnode = CTxIn();
        payee = CScript();
    }

    CCoralnodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinCoralnode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << payee;
        ss << nBlockHeight;
        ss << vinCoralnode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyCoralnode, CPubKey& pubKeyCoralnode);
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
        READWRITE(vinCoralnode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinCoralnode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

//
// coralnode Payments Class
// Keeps track of who should get paid for which blocks
//

class CCoralnodePayments
{
private:
    int nSyncedFromPeer;
    int nLastBlockHeight;

public:
    std::map<uint256, CCoralnodePaymentWinner> mapCoralnodePayeeVotes;
    std::map<int, CCoralnodeBlockPayees> mapCoralnodeBlocks;
    std::map<uint256, int> mapCoralnodesLastVote; //prevout.hash + prevout.n, nBlockHeight

    CCoralnodePayments()
    {
        nSyncedFromPeer = 0;
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapCoralnodeBlocks, cs_mapCoralnodePayeeVotes);
        mapCoralnodeBlocks.clear();
        mapCoralnodePayeeVotes.clear();
    }

    bool AddWinningCoralnode(CCoralnodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList();
    int LastPayment(CCoralnode& mn);

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CCoralnode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outCoralnode, int nBlockHeight)
    {
        LOCK(cs_mapCoralnodePayeeVotes);

        if (mapCoralnodesLastVote.count(outCoralnode.hash + outCoralnode.n)) {
            if (mapCoralnodesLastVote[outCoralnode.hash + outCoralnode.n] == nBlockHeight) {
                return false;
            }
        }

        //record this coralnode voted
        mapCoralnodesLastVote[outCoralnode.hash + outCoralnode.n] = nBlockHeight;
        return true;
    }

    int GetMinCoralnodePaymentsProto();
    void ProcessMessageCoralnodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees, bool fProofOfStake, bool IsMasternode);
    std::string ToString() const;
    int GetOldestBlock();
    int GetNewestBlock();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapCoralnodePayeeVotes);
        READWRITE(mapCoralnodeBlocks);
    }
};


#endif
