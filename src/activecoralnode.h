// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The VITAE developers and CaritasCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVECORALNODE_H
#define ACTIVECORALNODE_H

#include "init.h"
#include "key.h"
#include "coralnode.h"
#include "net.h"
#include "obfuscation.h"
#include "sync.h"
#include "wallet.h"

#define ACTIVE_CORALNODE_INITIAL 0 // initial state
#define ACTIVE_CORALNODE_SYNC_IN_PROCESS 1
#define ACTIVE_CORALNODE_INPUT_TOO_NEW 2
#define ACTIVE_CORALNODE_NOT_CAPABLE 3
#define ACTIVE_CORALNODE_STARTED 4

// Responsible for activating the Coralnode and pinging the network
class CActiveCoralnode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Coralnode
    bool SendCoralnodePing(std::string& errorMessage);

    /// Register any Coralnode
    bool Register(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyCoralnode, CPubKey pubKeyCoralnode, std::string& errorMessage);

    /// Get 10000 CaritasCoin input that can be used for the Coralnode
    bool GetCoralNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
    // Initialized by init.cpp
    // Keys for the main Coralnode
    CPubKey pubKeyCoralnode;

    // Initialized while registering Coralnode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveCoralnode()
    {
        status = ACTIVE_CORALNODE_INITIAL;
    }

    /// Manage status of main Coralnode
    void ManageStatus();
    std::string GetStatus();

    /// Register remote Coralnode
    bool Register(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage);

    /// Get 10000 CaritasCoin input that can be used for the Coralnode
    bool GetCoralNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsCoralnode();

    /// Enable cold wallet mode (run a Coralnode with no funds)
    bool EnableHotColdCoralNode(CTxIn& vin, CService& addr);
};

#endif
