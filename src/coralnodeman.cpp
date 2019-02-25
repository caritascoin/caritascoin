// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activecoralnode.h"
#include "addrman.h"
#include "coralnode.h"
#include "coralnodeman.h"
#include "main.h"
#include "obfuscation.h"
#include "spork.h"
#include "util.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#define MN_WINNER_MINIMUM_AGE 8000 // Age in seconds. This should be > CORALNODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** coralnode manager */
CCoralnodeMan mnodeman;

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
    bool operator()(const pair<int64_t, CCoralnode>& t1,
        const pair<int64_t, CCoralnode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CCoralnodeDB
//

CCoralnodeDB::CCoralnodeDB()
{
    pathMN = GetDataDir() / "fncache.dat";
    strMagicMessage = "CoralnodeCache";
}

bool CCoralnodeDB::Write(const CCoralnodeMan& mnodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssCoralnodes(SER_DISK, CLIENT_VERSION);
    ssCoralnodes << strMagicMessage;                   // coralnode cache file specific magic message
    ssCoralnodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssCoralnodes << mnodemanToSave;
    uint256 hash = Hash(ssCoralnodes.begin(), ssCoralnodes.end());
    ssCoralnodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssCoralnodes;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("coralnode", "Written info to mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("coralnode", "  %s\n", mnodemanToSave.ToString());

    return true;
}

CCoralnodeDB::ReadResult CCoralnodeDB::Read(CCoralnodeMan& mnodemanToLoad, bool fDryRun)
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

    CDataStream ssCoralnodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssCoralnodes.begin(), ssCoralnodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (coralnode cache file specific magic message) and ..

        ssCoralnodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid coralnode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssCoralnodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CCoralnodeMan object
        ssCoralnodes >> mnodemanToLoad;
    } catch (std::exception& e) {
        mnodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("coralnode", "Loaded info from mncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("coralnode", "  %s\n", mnodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("coralnode", "Coralnode manager - cleaning....\n");
        mnodemanToLoad.CheckAndRemove(true);
        LogPrint("coralnode", "Coralnode manager - result:\n");
        LogPrint("coralnode", "  %s\n", mnodemanToLoad.ToString());
    }

    return Ok;
}

void DumpCoralnodes()
{
    int64_t nStart = GetTimeMillis();

    CCoralnodeDB mndb;
    CCoralnodeMan tempMnodeman;

    LogPrint("coralnode", "Verifying mncache.dat format...\n");
    CCoralnodeDB::ReadResult readResult = mndb.Read(tempMnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CCoralnodeDB::FileError)
        LogPrint("coralnode", "Missing coralnode cache file - mncache.dat, will try to recreate\n");
    else if (readResult != CCoralnodeDB::Ok) {
        LogPrint("coralnode", "Error reading mncache.dat: ");
        if (readResult == CCoralnodeDB::IncorrectFormat)
            LogPrint("coralnode", "magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("coralnode", "file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("coralnode", "Writting info to mncache.dat...\n");
    mndb.Write(mnodeman);

    LogPrint("coralnode", "Coralnode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CCoralnodeMan::CCoralnodeMan()
{
    nDsqCount = 0;
}

bool CCoralnodeMan::Add(CCoralnode& mn)
{
    LOCK(cs);

    if (!mn.IsEnabled())
        return false;

    CCoralnode* pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("coralnode", "CCoralnodeMan: Adding new Coralnode %s - %i now\n", mn.vin.prevout.hash.ToString(), size() + 1);
        vCoralnodes.push_back(mn);
        return true;
    }

    return false;
}

void CCoralnodeMan::AskForMN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForCoralnodeListEntry.find(vin.prevout);
    if (i != mWeAskedForCoralnodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the fnb info once from the node that sent fnp

    LogPrint("coralnode", "CCoralnodeMan::AskForMN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("obseg", vin);
    int64_t askAgain = GetTime() + CORALNODE_MIN_MNP_SECONDS;
    mWeAskedForCoralnodeListEntry[vin.prevout] = askAgain;
}

void CCoralnodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        mn.Check();
    }
}

void CCoralnodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CCoralnode>::iterator it = vCoralnodes.begin();
    while (it != vCoralnodes.end()) {
        if ((*it).activeState == CCoralnode::CORALNODE_REMOVE ||
            (*it).activeState == CCoralnode::CORALNODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CCoralnode::CORALNODE_EXPIRED) ||
            (*it).protocolVersion < coralnodePayments.GetMinCoralnodePaymentsProto()) {
            LogPrint("coralnode", "CCoralnodeMan: Removing inactive Coralnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new fnb
            map<uint256, CCoralnodeBroadcast>::iterator it3 = mapSeenCoralnodeBroadcast.begin();
            while (it3 != mapSeenCoralnodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    coralnodeSync.mapSeenSyncMNB.erase((*it3).first);
                    mapSeenCoralnodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this coralnode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForCoralnodeListEntry.begin();
            while (it2 != mWeAskedForCoralnodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForCoralnodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vCoralnodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Coralnode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForCoralnodeList.begin();
    while (it1 != mAskedUsForCoralnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForCoralnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Coralnode list
    it1 = mWeAskedForCoralnodeList.begin();
    while (it1 != mWeAskedForCoralnodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForCoralnodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Coralnodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForCoralnodeListEntry.begin();
    while (it2 != mWeAskedForCoralnodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForCoralnodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenCoralnodeBroadcast
    map<uint256, CCoralnodeBroadcast>::iterator it3 = mapSeenCoralnodeBroadcast.begin();
    while (it3 != mapSeenCoralnodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (CORALNODE_REMOVAL_SECONDS * 2)) {
            mapSeenCoralnodeBroadcast.erase(it3++);
            coralnodeSync.mapSeenSyncMNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenCoralnodePing
    map<uint256, CCoralnodePing>::iterator it4 = mapSeenCoralnodePing.begin();
    while (it4 != mapSeenCoralnodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (CORALNODE_REMOVAL_SECONDS * 2)) {
            mapSeenCoralnodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CCoralnodeMan::Clear()
{
    LOCK(cs);
    vCoralnodes.clear();
    mAskedUsForCoralnodeList.clear();
    mWeAskedForCoralnodeList.clear();
    mWeAskedForCoralnodeListEntry.clear();
    mapSeenCoralnodeBroadcast.clear();
    mapSeenCoralnodePing.clear();
    nDsqCount = 0;
}

int CCoralnodeMan::stable_size()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nCoralnode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nCoralnode_Age = 0;

    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        if (mn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (IsSporkActive(SPORK_8_CORALNODE_PAYMENT_ENFORCEMENT)) {
            nCoralnode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nCoralnode_Age) < nCoralnode_Min_Age) {
                continue; // Skip coralnodes younger than (default) 8000 sec (MUST be > CORALNODE_REMOVAL_SECONDS)
            }
        }
        mn.Check();
        if (!mn.IsEnabled())
            continue; // Skip not-enabled coralnodes

        nStable_size++;
    }

    return nStable_size;
}

int CCoralnodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? coralnodePayments.GetMinCoralnodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        mn.Check();
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CCoralnodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? coralnodePayments.GetMinCoralnodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        mn.Check();
        std::string strHost;
        int port;
        SplitHostPort(mn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
        case 1:
            ipv4++;
            break;
        case 2:
            ipv6++;
            break;
        case 3:
            onion++;
            break;
        }
    }
}

void CCoralnodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForCoralnodeList.find(pnode->addr);
            if (it != mWeAskedForCoralnodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("coralnode", "obseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("obseg", CTxIn());
    int64_t askAgain = GetTime() + CORALNODES_DSEG_SECONDS;
    mWeAskedForCoralnodeList[pnode->addr] = askAgain;
}

CCoralnode* CCoralnodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        payee2 = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &mn;
    }
    return NULL;
}

CCoralnode* CCoralnodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        if (mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}


CCoralnode* CCoralnodeMan::Find(const CPubKey& pubKeyCoralnode)
{
    LOCK(cs);

    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        if (mn.pubKeyCoralnode == pubKeyCoralnode)
            return &mn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best coralnode to pay on the network
//
CCoralnode* CCoralnodeMan::GetNextCoralnodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CCoralnode* pBestCoralnode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecCoralnodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        mn.Check();
        if (!mn.IsEnabled()) continue;

        // //check protocol version
        if (mn.protocolVersion < coralnodePayments.GetMinCoralnodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (coralnodePayments.IsScheduled(mn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are coralnodes
        if (mn.GetCoralnodeInputAge() < nMnCount) continue;

        vecCoralnodeLastPaid.push_back(make_pair(mn.SecondsSincePayment(), mn.vin));
    }

    nCount = (int)vecCoralnodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextCoralnodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecCoralnodeLastPaid.rbegin(), vecCoralnodeLastPaid.rend(), CompareLastPaid());

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = CountEnabled() / 10;
    int nCountTenth = 0;
    uint256 nHigh = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecCoralnodeLastPaid) {
        CCoralnode* pmn = Find(s.second);
        if (!pmn) break;

        uint256 n = pmn->CalculateScore(1, nBlockHeight - 100);
        if (n > nHigh) {
            nHigh = n;
            pBestCoralnode = pmn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestCoralnode;
}

CCoralnode* CCoralnodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? coralnodePayments.GetMinCoralnodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("coralnode", "CCoralnodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("coralnode", "CCoralnodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        if (mn.protocolVersion < protocolVersion || !mn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH (CTxIn& usedVin, vecToExclude) {
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

CCoralnode* CCoralnodeMan::GetCurrentCoralNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CCoralnode* winner = NULL;

    // scan for winner
    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        mn.Check();
        if (mn.protocolVersion < minProtocol || !mn.IsEnabled()) continue;

        // calculate the score for each Coralnode
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

int CCoralnodeMan::GetCoralnodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecCoralnodeScores;
    int64_t nCoralnode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nCoralnode_Age = 0;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        if (mn.protocolVersion < minProtocol) {
            LogPrint("coralnode", "Skipping Coralnode with obsolete version %d\n", mn.protocolVersion);
            continue; // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_8_CORALNODE_PAYMENT_ENFORCEMENT)) {
            nCoralnode_Age = GetAdjustedTime() - mn.sigTime;
            if ((nCoralnode_Age) < nCoralnode_Min_Age) {
                if (fDebug) LogPrint("coralnode", "Skipping just activated Coralnode. Age: %ld\n", nCoralnode_Age);
                continue; // Skip coralnodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }
        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecCoralnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecCoralnodeScores.rbegin(), vecCoralnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecCoralnodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CCoralnode> > CCoralnodeMan::GetCoralnodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CCoralnode> > vecCoralnodeScores;
    std::vector<pair<int, CCoralnode> > vecCoralnodeRanks;

    //make sure we know about this block
    uint256 hash = 0;
    if (!GetBlockHash(hash, nBlockHeight)) return vecCoralnodeRanks;

    // scan for winner
    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        mn.Check();

        if (mn.protocolVersion < minProtocol) continue;

        if (!mn.IsEnabled()) {
            vecCoralnodeScores.push_back(make_pair(9999, mn));
            continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecCoralnodeScores.push_back(make_pair(n2, mn));
    }

    sort(vecCoralnodeScores.rbegin(), vecCoralnodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CCoralnode) & s, vecCoralnodeScores) {
        rank++;
        vecCoralnodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecCoralnodeRanks;
}

CCoralnode* CCoralnodeMan::GetCoralnodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecCoralnodeScores;

    // scan for winner
    BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
        if (mn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            mn.Check();
            if (!mn.IsEnabled()) continue;
        }

        uint256 n = mn.CalculateScore(1, nBlockHeight);
        int64_t n2 = n.GetCompact(false);

        vecCoralnodeScores.push_back(make_pair(n2, mn.vin));
    }

    sort(vecCoralnodeScores.rbegin(), vecCoralnodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecCoralnodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

void CCoralnodeMan::ProcessCoralnodeConnections()
{
    //we don't care about this for regtest
    if (Params().NetworkID() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (pnode->fObfuScationMaster) {
            if (obfuScationPool.pSubmittedToCoralnode != NULL && pnode->addr == obfuScationPool.pSubmittedToCoralnode->addr) continue;
            LogPrint("coralnode", "Closing Coralnode connection peer=%i \n", pnode->GetId());
            pnode->fObfuScationMaster = false;
            pnode->Release();
        }
    }
}

void CCoralnodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Obfuscation/Coralnode related functionality
    if (!coralnodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "fnb") { //Coralnode Broadcast
        CCoralnodeBroadcast fnb;
        vRecv >> fnb;
		
        if (mapSeenCoralnodeBroadcast.count(fnb.GetHash())) { //seen
            coralnodeSync.AddedCoralnodeList(fnb.GetHash());
            return;
        }
        mapSeenCoralnodeBroadcast.insert(make_pair(fnb.GetHash(), fnb));

        int nDoS = 0;
        if (!fnb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);

            //failed
            return;
        }

        uint256 hashBlock = 0;
        CTransaction tx;
        // make sure the vout that was signed is related to the transaction that spawned the Coralnode
        //  - this is expensive, so it's only done once per Coralnode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(fnb.vin, fnb.pubKeyCollateralAddress, tx, hashBlock)) {
            LogPrint("coralnode", "fnb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        CValidationState state;
        bool fAcceptable = false;
        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;
            fAcceptable = AcceptableFundamentalTxn(mempool, state, tx);
        }

        if (!fAcceptable) {
            LogPrint("coralnode", "fnb - Got bad vin, doesnt burn collateral amount\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }


        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
        if (fnb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(fnb.addr), pfrom->addr, 2 * 60 * 60);
            coralnodeSync.AddedCoralnodeList(fnb.GetHash());
        } else {
            LogPrint("coralnode", "fnb - Rejected Coralnode entry %s\n", fnb.vin.prevout.hash.ToString());

            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == "fnp") { //Coralnode Ping
        CCoralnodePing fnp;
        vRecv >> fnp;

        LogPrint("coralnode", "fnp - Coralnode ping, vin: %s\n", fnp.vin.prevout.hash.ToString());

        if (mapSeenCoralnodePing.count(fnp.GetHash())) return; //seen
        mapSeenCoralnodePing.insert(make_pair(fnp.GetHash(), fnp));

        int nDoS = 0;
        if (fnp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Coralnode list
            CCoralnode* pmn = Find(fnp.vin);
            // if it's known, don't ask for the fnb, just return
            if (pmn != NULL) return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a coralnode entry once
        AskForMN(pfrom, fnp.vin);

    } else if (strCommand == "obseg") { //Get Coralnode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && Params().NetworkID() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForCoralnodeList.find(pfrom->addr);
                if (i != mAskedUsForCoralnodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrint("coralnode", "obseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + CORALNODES_DSEG_SECONDS;
                mAskedUsForCoralnodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CCoralnode& mn, vCoralnodes) {
            if (mn.addr.IsRFC1918()) continue; //local network

            if (mn.IsEnabled()) {
                LogPrint("coralnode", "obseg - Sending Coralnode entry - %s \n", mn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == mn.vin) {
                    CCoralnodeBroadcast fnb = CCoralnodeBroadcast(mn);
                    uint256 hash = fnb.GetHash();
                    pfrom->PushInventory(CInv(MSG_CORALNODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenCoralnodeBroadcast.count(hash)) mapSeenCoralnodeBroadcast.insert(make_pair(hash, fnb));

                    if (vin == mn.vin) {
                        LogPrint("coralnode", "obseg - Sent 1 Coralnode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", CORALNODE_SYNC_LIST, nInvCount);
            LogPrint("coralnode", "obseg - Sent %d Coralnode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    // Light version for OLD MASSTERNODES - fake pings, no self-activation
    else if (strCommand == "obsee") { //ObfuScation Election Entry

        if (IsSporkActive(SPORK_10_CORALNODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        CService addr;
        CPubKey pubkey;
        CPubKey pubkey2;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        int count;
        int current;
        int64_t lastUpdated;
        int protocolVersion;
        CScript donationAddress;
        int donationPercentage;
        std::string strMessage;

        vRecv >> vin >> addr >> vchSig >> sigTime >> pubkey >> pubkey2 >> count >> current >> lastUpdated >> protocolVersion >> donationAddress >> donationPercentage;

        // make sure signature isn't in the future (past is OK)
        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrint("coralnode", "obsee - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::string vchPubKey(pubkey.begin(), pubkey.end());
        std::string vchPubKey2(pubkey2.begin(), pubkey2.end());

        strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion) + donationAddress.ToString() + boost::lexical_cast<std::string>(donationPercentage);

        if (protocolVersion < coralnodePayments.GetMinCoralnodePaymentsProto()) {
            LogPrint("coralnode", "obsee - ignoring outdated Coralnode %s protocol version %d < %d\n", vin.prevout.hash.ToString(), protocolVersion, coralnodePayments.GetMinCoralnodePaymentsProto());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        CScript pubkeyScript;
        pubkeyScript = GetScriptForDestination(pubkey.GetID());

        if (pubkeyScript.size() != 25) {
            LogPrint("coralnode", "obsee - pubkey the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        CScript pubkeyScript2;
        pubkeyScript2 = GetScriptForDestination(pubkey2.GetID());

        if (pubkeyScript2.size() != 25) {
            LogPrint("coralnode", "obsee - pubkey2 the wrong size\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (!vin.scriptSig.empty()) {
            LogPrint("coralnode", "obsee - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        std::string errorMessage = "";
        if (!obfuScationSigner.VerifyMessage(pubkey, vchSig, strMessage, errorMessage)) {
            LogPrint("coralnode", "obsee - Got bad Coralnode address signature\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (addr.GetPort() != Params().GetDefaultPort()) return;
        } else if (addr.GetPort() == Params().GetDefaultPort())
            return;

        //search existing Coralnode list, this is where we update existing Coralnodes with new obsee broadcasts
        CCoralnode* pmn = this->Find(vin);
        if (pmn != NULL) {
            // count == -1 when it's a new entry
            //   e.g. We don't want the entry relayed/time updated when we're syncing the list
            // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
            //   after that they just need to match
            if (count == -1 && pmn->pubKeyCollateralAddress == pubkey && (GetAdjustedTime() - pmn->nLastDsee > CORALNODE_MIN_MNB_SECONDS)) {
                if (pmn->protocolVersion > GETHEADERS_VERSION && sigTime - pmn->lastPing.sigTime < CORALNODE_MIN_MNB_SECONDS) return;
                if (pmn->nLastDsee < sigTime) { //take the newest entry
                    LogPrint("coralnode", "obsee - Got updated entry for %s\n", vin.prevout.hash.ToString());
                    if (pmn->protocolVersion < GETHEADERS_VERSION) {
                        pmn->pubKeyCoralnode = pubkey2;
                        pmn->sigTime = sigTime;
                        pmn->sig = vchSig;
                        pmn->protocolVersion = protocolVersion;
                        pmn->addr = addr;
                        //fake ping
                        pmn->lastPing = CCoralnodePing(vin);
                    }
                    pmn->nLastDsee = sigTime;
                    pmn->Check();
                    if (pmn->IsEnabled()) {
                        TRY_LOCK(cs_vNodes, lockNodes);
                        if (!lockNodes) return;
                        BOOST_FOREACH (CNode* pnode, vNodes)
                            if (pnode->nVersion >= coralnodePayments.GetMinCoralnodePaymentsProto())
                                pnode->PushMessage("obsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
                    }
                }
            }

            return;
        }

        static std::map<COutPoint, CPubKey> mapSeenobsee;
        if (mapSeenobsee.count(vin.prevout) && mapSeenobsee[vin.prevout] == pubkey) {
            LogPrint("coralnode", "obsee - already seen this vin %s\n", vin.prevout.ToString());
            return;
        }
        mapSeenobsee.insert(make_pair(vin.prevout, pubkey));
        // make sure the vout that was signed is related to the transaction that spawned the Coralnode
        //  - this is expensive, so it's only done once per Coralnode

        uint256 hashBlock = 0;
        CTransaction tx;


        if (!obfuScationSigner.IsVinAssociatedWithPubkey(vin, pubkey, tx, hashBlock)) {
            LogPrint("coralnode", "obsee - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 100);
            return;
        }


        LogPrint("coralnode", "obsee - Got NEW OLD Coralnode entry %s\n", vin.prevout.hash.ToString());

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()

        CValidationState state;
        /*CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(9999.99 * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);*/

        bool fAcceptable = false;
        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;
            fAcceptable = AcceptableFundamentalTxn(mempool, state, tx);
        }

        if (fAcceptable) {
            if (GetInputAge(vin) < CORALNODE_MIN_CONFIRMATIONS) {
                LogPrint("coralnode", "obsee - Input must have least %d confirmations\n", CORALNODE_MIN_CONFIRMATIONS);
                Misbehaving(pfrom->GetId(), 20);
                return;
            }

            // verify that sig time is legit in past
            // should be at least not earlier than block when 1000 CaritasCoin tx got CORALNODE_MIN_CONFIRMATIONS
            uint256 hashBlock = 0;
            CTransaction tx2;
            GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
            BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                CBlockIndex* pMNIndex = (*mi).second;                                                             // block for 10000 CaritasCoin tx -> 1 confirmation
                CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + CORALNODE_MIN_CONFIRMATIONS - 1]; // block where tx got CORALNODE_MIN_CONFIRMATIONS
                if (pConfIndex->GetBlockTime() > sigTime) {
                    LogPrint("coralnode", "fnb - Bad sigTime %d for Coralnode %s (%i conf block is at %d)\n",
                        sigTime, vin.prevout.hash.ToString(), CORALNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
                    return;
                }
            }

            // use this as a peer
            addrman.Add(CAddress(addr), pfrom->addr, 2 * 60 * 60);

            // add Coralnode
            CCoralnode mn = CCoralnode();
            mn.addr = addr;
            mn.vin = vin;
            mn.pubKeyCollateralAddress = pubkey;
            mn.sig = vchSig;
            mn.sigTime = sigTime;
            mn.pubKeyCoralnode = pubkey2;
            mn.protocolVersion = protocolVersion;
            // fake ping
            mn.lastPing = CCoralnodePing(vin);
            mn.Check(true);
            // add v11 coralnodes, v12 should be added by fnb only
            if (protocolVersion < GETHEADERS_VERSION) {
                LogPrint("coralnode", "obsee - Accepted OLD Coralnode entry %i %i\n", count, current);
                Add(mn);
            }
            if (mn.IsEnabled()) {
                TRY_LOCK(cs_vNodes, lockNodes);
                if (!lockNodes) return;
                BOOST_FOREACH (CNode* pnode, vNodes)
                    if (pnode->nVersion >= coralnodePayments.GetMinCoralnodePaymentsProto())
                        pnode->PushMessage("obsee", vin, addr, vchSig, sigTime, pubkey, pubkey2, count, current, lastUpdated, protocolVersion, donationAddress, donationPercentage);
            }
        } else {
            LogPrint("coralnode", "obsee - Rejected Coralnode entry %s\n", vin.prevout.hash.ToString());

            int nDoS = 0;
            if (state.IsInvalid(nDoS)) {
                LogPrint("coralnode", "obsee - %s from %i %s was not accepted into the memory pool\n", tx.GetHash().ToString().c_str(),
                    pfrom->GetId(), pfrom->cleanSubVer.c_str());
                if (nDoS > 0)
                    Misbehaving(pfrom->GetId(), nDoS);
            }
        }
    }

    else if (strCommand == "obseep") { //ObfuScation Election Entry Ping

        if (IsSporkActive(SPORK_10_CORALNODE_PAY_UPDATED_NODES)) return;

        CTxIn vin;
        vector<unsigned char> vchSig;
        int64_t sigTime;
        bool stop;
        vRecv >> vin >> vchSig >> sigTime >> stop;

        //LogPrint("coralnode","obseep - Received: vin: %s sigTime: %lld stop: %s\n", vin.ToString().c_str(), sigTime, stop ? "true" : "false");

        if (sigTime > GetAdjustedTime() + 60 * 60) {
            LogPrint("coralnode", "obseep - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        if (sigTime <= GetAdjustedTime() - 60 * 60) {
            LogPrint("coralnode", "obseep - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
            Misbehaving(pfrom->GetId(), 1);
            return;
        }

        std::map<COutPoint, int64_t>::iterator i = mWeAskedForCoralnodeListEntry.find(vin.prevout);
        if (i != mWeAskedForCoralnodeListEntry.end()) {
            int64_t t = (*i).second;
            if (GetTime() < t) return; // we've asked recently
        }

        // see if we have this Coralnode
        CCoralnode* pmn = this->Find(vin);
        if (pmn != NULL && pmn->protocolVersion >= coralnodePayments.GetMinCoralnodePaymentsProto()) {
            // LogPrint("coralnode","obseep - Found corresponding mn for vin: %s\n", vin.ToString().c_str());
            // take this only if it's newer
            if (sigTime - pmn->nLastDseep > CORALNODE_MIN_MNP_SECONDS) {
                std::string strMessage = pmn->addr.ToString() + boost::lexical_cast<std::string>(sigTime) + boost::lexical_cast<std::string>(stop);

                std::string errorMessage = "";
                if (!obfuScationSigner.VerifyMessage(pmn->pubKeyCoralnode, vchSig, strMessage, errorMessage)) {
                    LogPrint("coralnode", "obseep - Got bad Coralnode address signature %s \n", vin.prevout.hash.ToString());
                    //Misbehaving(pfrom->GetId(), 100);
                    return;
                }

                // fake ping for v11 coralnodes, ignore for v12
                if (pmn->protocolVersion < GETHEADERS_VERSION) pmn->lastPing = CCoralnodePing(vin);
                pmn->nLastDseep = sigTime;
                pmn->Check();
                if (pmn->IsEnabled()) {
                    TRY_LOCK(cs_vNodes, lockNodes);
                    if (!lockNodes) return;
                    LogPrint("coralnode", "obseep - relaying %s \n", vin.prevout.hash.ToString());
                    BOOST_FOREACH (CNode* pnode, vNodes)
                        if (pnode->nVersion >= coralnodePayments.GetMinCoralnodePaymentsProto())
                            pnode->PushMessage("obseep", vin, vchSig, sigTime, stop);
                }
            }
            return;
        }

        LogPrint("coralnode", "obseep - Couldn't find Coralnode entry %s peer=%i\n", vin.prevout.hash.ToString(), pfrom->GetId());

        AskForMN(pfrom, vin);
    }

    /*
     * END OF "REMOVE"
     */
}

void CCoralnodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CCoralnode>::iterator it = vCoralnodes.begin();
    while (it != vCoralnodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("coralnode", "CCoralnodeMan: Removing Coralnode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vCoralnodes.erase(it);
            break;
        }
        ++it;
    }
}

void CCoralnodeMan::UpdateCoralnodeList(CCoralnodeBroadcast fnb)
{
    LOCK(cs);
    mapSeenCoralnodePing.insert(std::make_pair(fnb.lastPing.GetHash(), fnb.lastPing));
    mapSeenCoralnodeBroadcast.insert(std::make_pair(fnb.GetHash(), fnb));

    LogPrint("coralnode", "CCoralnodeMan::UpdateCoralnodeList -- coralnode=%s\n", fnb.vin.prevout.ToStringShort());

    CCoralnode* pmn = Find(fnb.vin);
    if (pmn == NULL) {
        CCoralnode mn(fnb);
        if (Add(mn)) {
            coralnodeSync.AddedCoralnodeList(fnb.GetHash());
        }
    } else if (pmn->UpdateFromNewBroadcast(fnb)) {
        coralnodeSync.AddedCoralnodeList(fnb.GetHash());
    }
}

std::string CCoralnodeMan::ToString() const
{
    std::ostringstream info;

    info << "Coralnodes: " << (int)vCoralnodes.size() << ", peers who asked us for Coralnode list: " << (int)mAskedUsForCoralnodeList.size() << ", peers we asked for Coralnode list: " << (int)mWeAskedForCoralnodeList.size() << ", entries in Coralnode list we asked for: " << (int)mWeAskedForCoralnodeListEntry.size() << ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}
