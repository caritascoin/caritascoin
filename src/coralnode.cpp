// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coralnode.h"
#include "addrman.h"
#include "coralnodeman.h"
#include "obfuscation.h"
#include "sync.h"
#include "util.h"
#include <boost/lexical_cast.hpp>

// keep track of the scanning errors I've seen
map<uint256, int> mapSeenCoralnodeScanningErrors;
// cache block hashes as we calculate them
std::map<int64_t, uint256> mapCacheBlockHashes;

//Get the last hash that matches the modulus given. Processed in reverse order
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    if (chainActive.Tip() == NULL) return false;

    if (nBlockHeight == 0)
        nBlockHeight = chainActive.Tip()->nHeight;

    if (mapCacheBlockHashes.count(nBlockHeight)) {
        hash = mapCacheBlockHashes[nBlockHeight];
        return true;
    }

    const CBlockIndex* BlockLastSolved = chainActive.Tip();
    const CBlockIndex* BlockReading = chainActive.Tip();

    if (BlockLastSolved == NULL || BlockLastSolved->nHeight == 0 || chainActive.Tip()->nHeight + 1 < nBlockHeight) return false;

    int nBlocksAgo = 0;
    if (nBlockHeight > 0) nBlocksAgo = (chainActive.Tip()->nHeight + 1) - nBlockHeight;
    assert(nBlocksAgo >= 0);

    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nBlocksAgo) {
            hash = BlockReading->GetBlockHash();
            mapCacheBlockHashes[nBlockHeight] = hash;
            return true;
        }
        n++;

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return false;
}

CCoralnode::CCoralnode()
{
    LOCK(cs);
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyCoralnode = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = CORALNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CCoralnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = CORALNODE_ENABLED,
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

CCoralnode::CCoralnode(const CCoralnode& other)
{
    LOCK(cs);
    vin = other.vin;
    addr = other.addr;
    pubKeyCollateralAddress = other.pubKeyCollateralAddress;
    pubKeyCoralnode = other.pubKeyCoralnode;
    sig = other.sig;
    activeState = other.activeState;
    sigTime = other.sigTime;
    lastPing = other.lastPing;
    cacheInputAge = other.cacheInputAge;
    cacheInputAgeBlock = other.cacheInputAgeBlock;
    unitTest = other.unitTest;
    allowFreeTx = other.allowFreeTx;
    nActiveState = CORALNODE_ENABLED,
    protocolVersion = other.protocolVersion;
    nLastDsq = other.nLastDsq;
    nScanningErrorCount = other.nScanningErrorCount;
    nLastScanningErrorBlockHeight = other.nLastScanningErrorBlockHeight;
    lastTimeChecked = 0;
    nLastDsee = other.nLastDsee;   // temporary, do not save. Remove after migration to v12
    nLastDseep = other.nLastDseep; // temporary, do not save. Remove after migration to v12
}

CCoralnode::CCoralnode(const CCoralnodeBroadcast& mnb)
{
    LOCK(cs);
    vin = mnb.vin;
    addr = mnb.addr;
    pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
    pubKeyCoralnode = mnb.pubKeyCoralnode;
    sig = mnb.sig;
    activeState = CORALNODE_ENABLED;
    sigTime = mnb.sigTime;
    lastPing = mnb.lastPing;
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    nActiveState = CORALNODE_ENABLED,
    protocolVersion = mnb.protocolVersion;
    nLastDsq = mnb.nLastDsq;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
    lastTimeChecked = 0;
    nLastDsee = 0;  // temporary, do not save. Remove after migration to v12
    nLastDseep = 0; // temporary, do not save. Remove after migration to v12
}

//
// When a new coralnode broadcast is sent, update our information
//
bool CCoralnode::UpdateFromNewBroadcast(CCoralnodeBroadcast& mnb)
{
    if (mnb.sigTime > sigTime) {
        pubKeyCoralnode = mnb.pubKeyCoralnode;
        pubKeyCollateralAddress = mnb.pubKeyCollateralAddress;
        sigTime = mnb.sigTime;
        sig = mnb.sig;
        protocolVersion = mnb.protocolVersion;
        addr = mnb.addr;
        lastTimeChecked = 0;
        int nDoS = 0;
        if (mnb.lastPing == CCoralnodePing() || (mnb.lastPing != CCoralnodePing() && mnb.lastPing.CheckAndUpdate(nDoS, false))) {
            lastPing = mnb.lastPing;
            mnodeman.mapSeenCoralnodePing.insert(make_pair(lastPing.GetHash(), lastPing));
        }
        return true;
    }
    return false;
}

//
// Deterministically calculate a given "score" for a Coralnode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
uint256 CCoralnode::CalculateScore(int mod, int64_t nBlockHeight)
{
    if (chainActive.Tip() == NULL) return 0;

    uint256 hash = 0;
    uint256 aux = vin.prevout.hash + vin.prevout.n;

    if (!GetBlockHash(hash, nBlockHeight)) {
        LogPrint("coralnode","CalculateScore ERROR - nHeight %d - Returned 0\n", nBlockHeight);
        return 0;
    }

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hash;
    uint256 hash2 = ss.GetHash();

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << hash;
    ss2 << aux;
    uint256 hash3 = ss2.GetHash();

    uint256 r = (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);

    return r;
}

void CCoralnode::Check(bool forceCheck)
{
    if (ShutdownRequested()) return;

    if (!forceCheck && (GetTime() - lastTimeChecked < CORALNODE_CHECK_SECONDS)) return;
    lastTimeChecked = GetTime();


    //once spent, stop doing the checks
    if (activeState == CORALNODE_VIN_SPENT) return;


    if (!IsPingedWithin(CORALNODE_REMOVAL_SECONDS)) {
        activeState = CORALNODE_REMOVE;
        return;
    }

    if (!IsPingedWithin(CORALNODE_EXPIRATION_SECONDS)) {
        activeState = CORALNODE_EXPIRED;
        return;
    }

    if (!unitTest) {
        CValidationState state;
        CMutableTransaction tx = CMutableTransaction();
        CTxOut vout = CTxOut(0.1 * COIN, obfuScationPool.collateralPubKey);
        tx.vin.push_back(vin);
        tx.vout.push_back(vout);

        {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) return;

            if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
                activeState = CORALNODE_VIN_SPENT;
                return;
            }
        }
    }

    activeState = CORALNODE_ENABLED; // OK
}

int64_t CCoralnode::SecondsSincePayment()
{
    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    int64_t sec = (GetAdjustedTime() - GetLastPaid());
    int64_t month = 60 * 60 * 24 * 30;
    if (sec < month) return sec; //if it's less than 30 days, give seconds

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // return some deterministic value for unknown/unpaid but force it to be more than 30 days old
    return month + hash.GetCompact(false);
}

int64_t CCoralnode::GetLastPaid()
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << sigTime;
    uint256 hash = ss.GetHash();

    // use a deterministic offset to break a tie -- 2.5 minutes
    int64_t nOffset = hash.GetCompact(false) % 150;

    if (chainActive.Tip() == NULL) return false;

    const CBlockIndex* BlockReading = chainActive.Tip();

    int nMnCount = mnodeman.CountEnabled() * 1.25;
    int n = 0;
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (n >= nMnCount) {
            return 0;
        }
        n++;

        if (coralnodePayments.mapCoralnodeBlocks.count(BlockReading->nHeight)) {
            /*
                Search for this payee, with at least 2 votes. This will aid in consensus allowing the network
                to converge on the same payees quickly, then keep the same schedule.
            */
            if (coralnodePayments.mapCoralnodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
                return BlockReading->nTime + nOffset;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    return 0;
}

std::string CCoralnode::GetStatus()
{
    switch (nActiveState) {
    case CCoralnode::CORALNODE_PRE_ENABLED:
        return "PRE_ENABLED";
    case CCoralnode::CORALNODE_ENABLED:
        return "ENABLED";
    case CCoralnode::CORALNODE_EXPIRED:
        return "EXPIRED";
    case CCoralnode::CORALNODE_OUTPOINT_SPENT:
        return "OUTPOINT_SPENT";
    case CCoralnode::CORALNODE_REMOVE:
        return "REMOVE";
    case CCoralnode::CORALNODE_WATCHDOG_EXPIRED:
        return "WATCHDOG_EXPIRED";
    case CCoralnode::CORALNODE_POSE_BAN:
        return "POSE_BAN";
    default:
        return "UNKNOWN";
    }
}

bool CCoralnode::IsValidNetAddr()
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkID() == CBaseChainParams::REGTEST ||
           (IsReachable(addr) && addr.IsRoutable());
}

CCoralnodeBroadcast::CCoralnodeBroadcast()
{
    vin = CTxIn();
    addr = CService();
    pubKeyCollateralAddress = CPubKey();
    pubKeyCoralnode1 = CPubKey();
    sig = std::vector<unsigned char>();
    activeState = CORALNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CCoralnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = PROTOCOL_VERSION;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CCoralnodeBroadcast::CCoralnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyCoralnodeNew, int protocolVersionIn)
{
    vin = newVin;
    addr = newAddr;
    pubKeyCollateralAddress = pubKeyCollateralAddressNew;
    pubKeyCoralnode = pubKeyCoralnodeNew;
    sig = std::vector<unsigned char>();
    activeState = CORALNODE_ENABLED;
    sigTime = GetAdjustedTime();
    lastPing = CCoralnodePing();
    cacheInputAge = 0;
    cacheInputAgeBlock = 0;
    unitTest = false;
    allowFreeTx = true;
    protocolVersion = protocolVersionIn;
    nLastDsq = 0;
    nScanningErrorCount = 0;
    nLastScanningErrorBlockHeight = 0;
}

CCoralnodeBroadcast::CCoralnodeBroadcast(const CCoralnode& mn)
{
    vin = mn.vin;
    addr = mn.addr;
    pubKeyCollateralAddress = mn.pubKeyCollateralAddress;
    pubKeyCoralnode = mn.pubKeyCoralnode;
    sig = mn.sig;
    activeState = mn.activeState;
    sigTime = mn.sigTime;
    lastPing = mn.lastPing;
    cacheInputAge = mn.cacheInputAge;
    cacheInputAgeBlock = mn.cacheInputAgeBlock;
    unitTest = mn.unitTest;
    allowFreeTx = mn.allowFreeTx;
    protocolVersion = mn.protocolVersion;
    nLastDsq = mn.nLastDsq;
    nScanningErrorCount = mn.nScanningErrorCount;
    nLastScanningErrorBlockHeight = mn.nLastScanningErrorBlockHeight;
}

bool CCoralnodeBroadcast::Create(std::string strService, std::string strKeyCoralnode, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CCoralnodeBroadcast& mnbRet, bool fOffline)
{
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyCoralnodeNew;
    CKey keyCoralnodeNew;

    //need correct blocks to send ping
    if (!fOffline && !coralnodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Coralnode";
        LogPrint("coralnode","CCoralnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!obfuScationSigner.GetKeysFromSecret(strKeyCoralnode, keyCoralnodeNew, pubKeyCoralnodeNew)) {
        strErrorRet = strprintf("Invalid coralnode key %s", strKeyCoralnode);
        LogPrint("coralnode","CCoralnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetCoralnodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for coralnode %s", strTxHash, strOutputIndex, strService);
        LogPrint("coralnode","CCoralnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    CService service = CService(strService);
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for coralnode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrint("coralnode","CCoralnodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for coralnode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrint("coralnode","CCoralnodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyCoralnodeNew, pubKeyCoralnodeNew, strErrorRet, mnbRet);
}

bool CCoralnodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyCoralnodeNew, CPubKey pubKeyCoralnodeNew, std::string& strErrorRet, CCoralnodeBroadcast& mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("coralnode", "CCoralnodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyCoralnodeNew.GetID() = %s\n",
        CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
        pubKeyCoralnodeNew.GetID().ToString());

    CCoralnodePing mnp(txin);
    if (!mnp.Sign(keyCoralnodeNew, pubKeyCoralnodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, coralnode=%s", txin.prevout.hash.ToString());
        LogPrint("coralnode","CCoralnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CCoralnodeBroadcast();
        return false;
    }

    mnbRet = CCoralnodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyCoralnodeNew, PROTOCOL_VERSION);

//    if (!mnbRet.IsValidNetAddr()) {
//        strErrorRet = strprintf("Invalid IP address %s, coralnode=%s", mnbRet.addr.ToStringIP (), txin.prevout.hash.ToString());
//        LogPrint("coralnode","CCoralnodeBroadcast::Create -- %s\n", strErrorRet);
//        mnbRet = CCoralnodeBroadcast();
//        return false;
//    }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, coralnode=%s", txin.prevout.hash.ToString());
        LogPrint("coralnode","CCoralnodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CCoralnodeBroadcast();
        return false;
    }

    return true;
}

bool CCoralnodeBroadcast::CheckAndUpdate(int& nDos)
{
    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("coralnode","mnb - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyCoralnode.begin(), pubKeyCoralnode.end());
    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (protocolVersion < coralnodePayments.GetMinCoralnodePaymentsProto()) {
        LogPrint("coralnode","mnb - ignoring outdated Coralnode %s protocol version %d\n", vin.prevout.hash.ToString(), protocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrint("coralnode","mnb - pubkey the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyCoralnode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrint("coralnode","mnb - pubkey2 the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrint("coralnode","mnb - Ignore Not Empty ScriptSig %s\n", vin.prevout.hash.ToString());
        return false;
    }

    std::string errorMessage = "";
    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        LogPrint("coralnode","mnb - Got bad Coralnode address signature\n");
        nDos = 100;
        return false;
    }

    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != Params().GetDefaultPort()) return false;
    } else if (addr.GetPort() == Params().GetDefaultPort())
        return false;

    //search existing Coralnode list, this is where we update existing Coralnodes with new mnb broadcasts
    CCoralnode* pmn = mnodeman.Find(vin);

    // no such coralnode, nothing to update
    if (pmn == NULL)
        return true;
    else {
        // this broadcast older than we have, it's bad.
        if (pmn->sigTime > sigTime) {
            LogPrint("coralnode","mnb - Bad sigTime %d for Coralnode %s (existing broadcast is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), pmn->sigTime);
            return false;
        }
        // coralnode is not enabled yet/already, nothing to update
        if (!pmn->IsEnabled()) return true;
    }

    // mn.pubkey = pubkey, IsVinAssociatedWithPubkey is validated once below,
    //   after that they just need to match
    if (pmn->pubKeyCollateralAddress == pubKeyCollateralAddress && !pmn->IsBroadcastedWithin(CORALNODE_MIN_MNB_SECONDS)) {
        //take the newest entry
        LogPrint("coralnode","mnb - Got updated entry for %s\n", vin.prevout.hash.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            if (pmn->IsEnabled()) Relay();
        }
        coralnodeSync.AddedCoralnodeList(GetHash());
    }

    return true;
}

bool CCoralnodeBroadcast::CheckInputsAndAdd(int& nDoS)
{
    // we are a coralnode with the same vin (i.e. already activated) and this mnb is ours (matches our Coralnode privkey)
    // so nothing to do here for us
    if (fCoralNode && vin.prevout == activeCoralnode.vin.prevout && pubKeyCoralnode == activeCoralnode.pubKeyCoralnode)
        return true;

    // search existing Coralnode list
    CCoralnode* pmn = mnodeman.Find(vin);

    if (pmn != NULL) {
        // nothing to do here if we already know about this coralnode and it's enabled
        if (pmn->IsEnabled()) return true;
        // if it's not enabled, remove old MN first and continue
        else
            mnodeman.Remove(pmn->vin);
    }

    CValidationState state;
    CMutableTransaction tx = CMutableTransaction();
    CTxOut vout = CTxOut(0.1 * COIN, obfuScationPool.collateralPubKey);
    tx.vin.push_back(vin);
    tx.vout.push_back(vout);

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            mnodeman.mapSeenCoralnodeBroadcast.erase(GetHash());
            coralnodeSync.mapSeenSyncMNB.erase(GetHash());
            return false;
        }

        if (!AcceptableInputs(mempool, state, CTransaction(tx), false, NULL)) {
            if (nDoS != NULL) nDoS = 10;
				 
            state.IsInvalid(nDoS);
            return false;
        }
    }

    LogPrint("coralnode", "mnb - Accepted Coralnode entry\n");

    if (GetInputAge(vin) < CORALNODE_MIN_CONFIRMATIONS) {
        LogPrint("coralnode","mnb - Input must have at least %d confirmations\n", CORALNODE_MIN_CONFIRMATIONS);
        // maybe we miss few blocks, let this mnb to be checked again later
        mnodeman.mapSeenCoralnodeBroadcast.erase(GetHash());
        coralnodeSync.mapSeenSyncMNB.erase(GetHash());
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 CaritasCoin tx got CORALNODE_MIN_CONFIRMATIONS
    uint256 hashBlock = 0;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, hashBlock, true);
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi != mapBlockIndex.end() && (*mi).second) {
        CBlockIndex* pMNIndex = (*mi).second;                                                        // block for 1000 CaritasCoin tx -> 1 confirmation
        CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + CORALNODE_MIN_CONFIRMATIONS - 1]; // block where tx got CORALNODE_MIN_CONFIRMATIONS
        if (pConfIndex->GetBlockTime() > sigTime) {
            LogPrint("coralnode","mnb - Bad sigTime %d for Coralnode %s (%i conf block is at %d)\n",
                sigTime, vin.prevout.hash.ToString(), CORALNODE_MIN_CONFIRMATIONS, pConfIndex->GetBlockTime());
            return false;
        }
    }

    LogPrint("coralnode","mnb - Got NEW Coralnode entry - %s - %lli \n", vin.prevout.hash.ToString(), sigTime);
    CCoralnode mn(*this);
    mnodeman.Add(mn);

    // if it matches our Coralnode privkey, then we've been remotely activated
    if (pubKeyCoralnode == activeCoralnode.pubKeyCoralnode && protocolVersion == PROTOCOL_VERSION) {
        activeCoralnode.EnableHotColdCoralNode(vin, addr);
    }

    bool isLocal = addr.IsRFC1918() || addr.IsLocal();
    if (Params().NetworkID() == CBaseChainParams::REGTEST) isLocal = false;

    if (!isLocal) Relay();

    return true;
}

void CCoralnodeBroadcast::Relay()
{
    CInv inv(MSG_CORALNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

bool CCoralnodeBroadcast::Sign(CKey& keyCollateralAddress)
{
    std::string errorMessage;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyCoralnode.begin(), pubKeyCoralnode.end());

    sigTime = GetAdjustedTime();

    std::string strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(protocolVersion);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, sig, keyCollateralAddress)) {
        LogPrint("coralnode","CCoralnodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, sig, strMessage, errorMessage)) {
        LogPrint("coralnode","CCoralnodeBroadcast::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

CCoralnodePing::CCoralnodePing()
{
    vin = CTxIn();
    blockHash = uint256(0);
    sigTime = 0;
    vchSig = std::vector<unsigned char>();
}

CCoralnodePing::CCoralnodePing(CTxIn& newVin)
{
    vin = newVin;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector<unsigned char>();
}


bool CCoralnodePing::Sign(CKey& keyCoralnode, CPubKey& pubKeyCoralnode)
{
    std::string errorMessage;
    std::string strCoralNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig, keyCoralnode)) {
        LogPrint("coralnode","CCoralnodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCoralnode, vchSig, strMessage, errorMessage)) {
        LogPrint("coralnode","CCoralnodePing::Sign() - Error: %s\n", errorMessage);
        return false;
    }

    return true;
}

bool CCoralnodePing::CheckAndUpdate(int& nDos, bool fRequireEnabled)
{
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrint("coralnode","CCoralnodePing::CheckAndUpdate - Signature rejected, too far into the future %s\n", vin.prevout.hash.ToString());
        nDos = 1;
        return false;
    }

    if (sigTime <= GetAdjustedTime() - 60 * 60) {
        LogPrint("coralnode","CCoralnodePing::CheckAndUpdate - Signature rejected, too far into the past %s - %d %d \n", vin.prevout.hash.ToString(), sigTime, GetAdjustedTime());
        nDos = 1;
        return false;
    }

    LogPrint("coralnode","CCoralnodePing::CheckAndUpdate - New Ping - %s - %lli\n", blockHash.ToString(), sigTime);

    // see if we have this Coralnode
    CCoralnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL && pmn->protocolVersion >= coralnodePayments.GetMinCoralnodePaymentsProto()) {
        if (fRequireEnabled && !pmn->IsEnabled()) return false;

        // LogPrint("coralnode","mnping - Found corresponding mn for vin: %s\n", vin.ToString());
        // update only if there is no known ping for this coralnode or
        // last ping was more then CORALNODE_MIN_MNP_SECONDS-60 ago comparing to this one
        if (!pmn->IsPingedWithin(CORALNODE_MIN_MNP_SECONDS - 60, sigTime)) {
            std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

            std::string errorMessage = "";
            if (!obfuScationSigner.VerifyMessage(pmn->pubKeyCoralnode, vchSig, strMessage, errorMessage)) {
                LogPrint("coralnode","CCoralnodePing::CheckAndUpdate - Got bad Coralnode address signature %s\n", vin.prevout.hash.ToString());
                nDos = 33;
                return false;
            }

            BlockMap::iterator mi = mapBlockIndex.find(blockHash);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                if ((*mi).second->nHeight < chainActive.Height() - 24) {
                    LogPrint("coralnode","CCoralnodePing::CheckAndUpdate - Coralnode %s block hash %s is too old\n", vin.prevout.hash.ToString(), blockHash.ToString());
                    // Do nothing here (no Coralnode update, no mnping relay)
                    // Let this node to be visible but fail to accept mnping

                    return false;
                }
            } else {
                if (fDebug) LogPrint("coralnode","CCoralnodePing::CheckAndUpdate - Coralnode %s block hash %s is unknown\n", vin.prevout.hash.ToString(), blockHash.ToString());
                // maybe we stuck so we shouldn't ban this node, just fail to accept it
                // TODO: or should we also request this block?

                return false;
            }

            pmn->lastPing = *this;

            //mnodeman.mapSeenCoralnodeBroadcast.lastPing is probably outdated, so we'll update it
            CCoralnodeBroadcast mnb(*pmn);
            uint256 hash = mnb.GetHash();
            if (mnodeman.mapSeenCoralnodeBroadcast.count(hash)) {
                mnodeman.mapSeenCoralnodeBroadcast[hash].lastPing = *this;
            }

            pmn->Check(true);
            if (!pmn->IsEnabled()) return false;

            LogPrint("coralnode", "CCoralnodePing::CheckAndUpdate - Coralnode ping accepted, vin: %s\n", vin.prevout.hash.ToString());

            Relay();
            return true;
        }
        LogPrint("coralnode", "CCoralnodePing::CheckAndUpdate - Coralnode ping arrived too early, vin: %s\n", vin.prevout.hash.ToString());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }
    LogPrint("coralnode", "CCoralnodePing::CheckAndUpdate - Couldn't find compatible Coralnode entry, vin: %s\n", vin.prevout.hash.ToString());

    return false;
}

void CCoralnodePing::Relay()
{
    CInv inv(MSG_CORALNODE_PING, GetHash());
    RelayInv(inv);
}
