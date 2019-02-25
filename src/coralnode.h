// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CORALNODE_H
#define CORALNODE_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

#define CORALNODE_MIN_CONFIRMATIONS 15
#define CORALNODE_MIN_MNP_SECONDS (10 * 60)
#define CORALNODE_MIN_MNB_SECONDS (5 * 60)
#define CORALNODE_PING_SECONDS (5 * 60)
#define CORALNODE_EXPIRATION_SECONDS (120 * 60)
#define CORALNODE_REMOVAL_SECONDS (130 * 60)
#define CORALNODE_CHECK_SECONDS 5

static const CAmount CORALNODE_AMOUNT = 10000* COIN;
static const CAmount FN_MAGIC_AMOUNT = 0.1234 *COIN;

using namespace std;

class CCoralnode;
class CCoralnodeBroadcast;
class CCoralnodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);


//
// The Coralnode Ping Class : Contains a different serialize method for sending pings from coralnodes throughout the network
//

class CCoralnodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CCoralnodePing();
    CCoralnodePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true);
    bool Sign(CKey& keyCoralnode, CPubKey& pubKeyCoralnode);
    void Relay();

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CCoralnodePing& first, CCoralnodePing& second) // nothrow
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

    CCoralnodePing& operator=(CCoralnodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CCoralnodePing& a, const CCoralnodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CCoralnodePing& a, const CCoralnodePing& b)
    {
        return !(a == b);
    }
};

//
// The Coralnode Class. For managing the Obfuscation process. It contains the input of the 10000 CaritasCoin, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CCoralnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    int64_t lastTimeChecked;

public:
    enum state {
        CORALNODE_PRE_ENABLED,
        CORALNODE_ENABLED,
        CORALNODE_EXPIRED,
        CORALNODE_OUTPOINT_SPENT,
        CORALNODE_REMOVE,
        CORALNODE_WATCHDOG_EXPIRED,
        CORALNODE_POSE_BAN,
        CORALNODE_VIN_SPENT,
        CORALNODE_POS_ERROR
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyCoralnode;
    CPubKey pubKeyCollateralAddress1;
    CPubKey pubKeyCoralnode1;
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
    CCoralnodePing lastPing;

    int64_t nLastDsee;  // temporary, do not save. Remove after migration to v12
    int64_t nLastDseep; // temporary, do not save. Remove after migration to v12

    CCoralnode();
    CCoralnode(const CCoralnode& other);
    CCoralnode(const CCoralnodeBroadcast& mnb);


    void swap(CCoralnode& first, CCoralnode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyCoralnode, second.pubKeyCoralnode);
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

    CCoralnode& operator=(CCoralnode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CCoralnode& a, const CCoralnode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CCoralnode& a, const CCoralnode& b)
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
        READWRITE(pubKeyCoralnode);
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

    bool UpdateFromNewBroadcast(CCoralnodeBroadcast& mnb);

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

        return (lastPing == CCoralnodePing()) ? false : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CCoralnodePing();
    }

    bool IsEnabled()
    {
        return activeState == CORALNODE_ENABLED;
    }

    int GetCoralnodeInputAge()
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

        if (activeState == CCoralnode::CORALNODE_ENABLED) strStatus = "ENABLED";
        if (activeState == CCoralnode::CORALNODE_EXPIRED) strStatus = "EXPIRED";
        if (activeState == CCoralnode::CORALNODE_VIN_SPENT) strStatus = "VIN_SPENT";
        if (activeState == CCoralnode::CORALNODE_REMOVE) strStatus = "REMOVE";
        if (activeState == CCoralnode::CORALNODE_POS_ERROR) strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid();
    bool IsValidNetAddr();
};


//
// The Coralnode Broadcast Class : Contains a different serialize method for sending coralnodes through the network
//

class CCoralnodeBroadcast : public CCoralnode
{
public:
    CCoralnodeBroadcast();
    CCoralnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CCoralnodeBroadcast(const CCoralnode& mn);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyCoralnode);
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

    /// Create Coralnode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyCoralnodeNew, CPubKey pubKeyCoralnodeNew, std::string& strErrorRet, CCoralnodeBroadcast& mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CCoralnodeBroadcast& mnbRet, bool fOffline = false);
};

#endif
