// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CORALNODEMAN_H
#define CORALNODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "coralnode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define CORALNODES_DUMP_SECONDS (15 * 60)
#define CORALNODES_DSEG_SECONDS (3 * 60 * 60)

using namespace std;

class CCoralnodeMan;

extern CCoralnodeMan mnodeman;
void DumpCoralnodes();

/** Access to the MN database (mncache.dat)
 */
class CCoralnodeDB
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

    CCoralnodeDB();
    bool Write(const CCoralnodeMan& mnodemanToSave);
    ReadResult Read(CCoralnodeMan& mnodemanToLoad, bool fDryRun = false);
};

class CCoralnodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CCoralnode> vCoralnodes;
    // who's asked for the Coralnode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForCoralnodeList;
    // who we asked for the Coralnode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForCoralnodeList;
    // which Coralnodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForCoralnodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CCoralnodeBroadcast> mapSeenCoralnodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CCoralnodePing> mapSeenCoralnodePing;

    // keep track of dsq count to prevent coralnodes from gaming obfuscation queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        LOCK(cs);
        READWRITE(vCoralnodes);
        READWRITE(mAskedUsForCoralnodeList);
        READWRITE(mWeAskedForCoralnodeList);
        READWRITE(mWeAskedForCoralnodeListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenCoralnodeBroadcast);
        READWRITE(mapSeenCoralnodePing);
    }

    CCoralnodeMan();
    CCoralnodeMan(CCoralnodeMan& other);

    /// Add an entry
    bool Add(CCoralnode& mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode* pnode, CTxIn& vin);

    /// Check all Coralnodes
    void Check();

    /// Check all Coralnodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Coralnode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CCoralnode* Find(const CScript& payee);
    CCoralnode* Find(const CTxIn& vin);
    CCoralnode* Find(const CPubKey& pubKeyCoralnode);

    /// Find an entry in the coralnode list that is next to be paid
    CCoralnode* GetNextCoralnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CCoralnode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CCoralnode* GetCurrentCoralNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CCoralnode> GetFullCoralnodeVector()
    {
        Check();
        return vCoralnodes;
    }

    std::vector<pair<int, CCoralnode> > GetCoralnodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetCoralnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CCoralnode* GetCoralnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessCoralnodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Coralnodes
    int size() { return vCoralnodes.size(); }

    /// Return the number of Coralnodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update coralnode list and maps using provided CCoralnodeBroadcast
    void UpdateCoralnodeList(CCoralnodeBroadcast mnb);
};

#endif
