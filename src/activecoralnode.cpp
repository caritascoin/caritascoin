// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The VITAE developers and CaritasCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activecoralnode.h"
#include "addrman.h"
#include "coralnode.h"
#include "coralnodeconfig.h"
#include "coralnodeman.h"
#include "protocol.h"
#include "spork.h"

//
// Bootup the Coralnode, look for a 10000 CaritasCoin input and register on the network
//
void CActiveCoralnode::ManageStatus()
{
    std::string errorMessage;

    if (!fCoralNode) return;

    if (fDebug) LogPrintf("CActiveCoralnode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkID() != CBaseChainParams::REGTEST && !coralnodeSync.IsBlockchainSynced()) {
        status = ACTIVE_CORALNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveCoralnode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_CORALNODE_SYNC_IN_PROCESS) status = ACTIVE_CORALNODE_INITIAL;

    if (status == ACTIVE_CORALNODE_INITIAL) {
        CCoralnode* pmn;
        pmn = mnodeman.Find(pubKeyCoralnode);
        if (pmn != NULL) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) EnableHotColdCoralNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_CORALNODE_STARTED) {
        // Set defaults
        status = ACTIVE_CORALNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveCoralnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetBalance() == 0) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveCoralnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strCoralNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the coralnodeaddr configuration option.";
                LogPrintf("CActiveCoralnode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strCoralNodeAddr);
        }

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (service.GetPort() != Params().GetDefaultPort()) {
                notCapableReason = strprintf("Invalid port: %u - only 16180 is supported on mainnet.", service.GetPort());
                LogPrintf("CActiveCoralnode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else if (service.GetPort() == Params().GetDefaultPort()) {
            notCapableReason = strprintf("Invalid port: %u - 16180 is only supported on mainnet.", service.GetPort());
            LogPrintf("CActiveCoralnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        LogPrintf("CActiveCoralnode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CNode* pnode = ConnectNode((CAddress)service, NULL, false);
        if (!pnode) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveCoralnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
        pnode->Release();

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetCoralNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetInputAge(vin) < CORALNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_CORALNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetInputAge(vin));
                LogPrintf("CActiveCoralnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyCoralnode;
            CKey keyCoralnode;

            if (!obfuScationSigner.SetKey(strCoralNodePrivKey, errorMessage, keyCoralnode, pubKeyCoralnode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            if (!Register(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyCoralnode, pubKeyCoralnode, errorMessage)) {
                notCapableReason = "Error on Register: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LogPrintf("CActiveCoralnode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_CORALNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveCoralnode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendCoralnodePing(errorMessage)) {
        LogPrintf("CActiveCoralnode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveCoralnode::GetStatus()
{
    switch (status) {
    case ACTIVE_CORALNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_CORALNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Coralnode";
    case ACTIVE_CORALNODE_INPUT_TOO_NEW:
        return strprintf("Coralnode input must have at least %d confirmations", CORALNODE_MIN_CONFIRMATIONS);
    case ACTIVE_CORALNODE_NOT_CAPABLE:
        return "Not capable coralnode: " + notCapableReason;
    case ACTIVE_CORALNODE_STARTED:
        return "Coralnode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveCoralnode::SendCoralnodePing(std::string& errorMessage)
{
    if (status != ACTIVE_CORALNODE_STARTED) {
        errorMessage = "Coralnode is not in a running status";
        return false;
    }

    CPubKey pubKeyCoralnode;
    CKey keyCoralnode;

    if (!obfuScationSigner.SetKey(strCoralNodePrivKey, errorMessage, keyCoralnode, pubKeyCoralnode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveCoralnode::SendCoralnodePing() - Relay Coralnode Ping vin = %s\n", vin.ToString());

    CCoralnodePing mnp(vin);
    if (!mnp.Sign(keyCoralnode, pubKeyCoralnode)) {
        errorMessage = "Couldn't sign Coralnode Ping";
        return false;
    }

    // Update lastPing for our coralnode in Coralnode list
    CCoralnode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(CORALNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Coralnode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenCoralnodePing.insert(make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenCoralnodeBroadcast.lastPing is probably outdated, so we'll update it
        CCoralnodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenCoralnodeBroadcast.count(hash)) mnodeman.mapSeenCoralnodeBroadcast[hash].lastPing = mnp;

        mnp.Relay();

        /*
         * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
         * AFTER MIGRATION TO V12 IS DONE
         */

        if (IsSporkActive(SPORK_10_CORALNODE_PAY_UPDATED_NODES)) return true;
        // for migration purposes ping our node on old coralnodes network too
        std::string retErrorMessage;
        std::vector<unsigned char> vchCoralNodeSignature;
        int64_t coralNodeSignatureTime = GetAdjustedTime();

        std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(coralNodeSignatureTime) + boost::lexical_cast<std::string>(false);

        if (!obfuScationSigner.SignMessage(strMessage, retErrorMessage, vchCoralNodeSignature, keyCoralnode)) {
            errorMessage = "dseep sign message failed: " + retErrorMessage;
            return false;
        }

        if (!obfuScationSigner.VerifyMessage(pubKeyCoralnode, vchCoralNodeSignature, strMessage, retErrorMessage)) {
            errorMessage = "dseep verify message failed: " + retErrorMessage;
            return false;
        }

        LogPrint("coralnode", "dseep - relaying from active mn, %s \n", vin.ToString().c_str());
        LOCK(cs_vNodes);
        BOOST_FOREACH (CNode* pnode, vNodes)
            pnode->PushMessage("obseep", vin, vchCoralNodeSignature, coralNodeSignatureTime, false);

        /*
         * END OF "REMOVE"
         */

        return true;
    } else {
        // Seems like we are trying to send a ping while the Coralnode is not registered in the network
        errorMessage = "Obfuscation Coralnode List doesn't include our Coralnode, shutting down Coralnode pinging service! " + vin.ToString();
        status = ACTIVE_CORALNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

bool CActiveCoralnode::Register(std::string strService, std::string strKeyCoralnode, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage)
{
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyCoralnode;
    CKey keyCoralnode;

    //need correct blocks to send ping
    if (!coralnodeSync.IsBlockchainSynced()) {
        errorMessage = GetStatus();
        LogPrintf("CActiveCoralnode::Register() - %s\n", errorMessage);
        return false;
    }

    if (!obfuScationSigner.SetKey(strKeyCoralnode, errorMessage, keyCoralnode, pubKeyCoralnode)) {
        errorMessage = strprintf("Can't find keys for coralnode %s - %s", strService, errorMessage);
        LogPrintf("CActiveCoralnode::Register() - %s\n", errorMessage);
        return false;
    }

    if (!GetCoralNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, strTxHash, strOutputIndex)) {
        errorMessage = strprintf("Could not allocate vin %s:%s for coralnode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CActiveCoralnode::Register() - %s\n", errorMessage);
        return false;
    }

    CService service = CService(strService);
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        if (service.GetPort() != Params().GetDefaultPort()) {
            errorMessage = strprintf("Invalid port %u for coralnode %s - only 16180 is supported on mainnet.", service.GetPort(), strService);
            LogPrintf("CActiveCoralnode::Register() - %s\n", errorMessage);
            return false;
        }
    } else if (service.GetPort() == Params().GetDefaultPort()) {
        errorMessage = strprintf("Invalid port %u for coralnode %s - 16180 is only supported on mainnet.", service.GetPort(), strService);
        LogPrintf("CActiveCoralnode::Register() - %s\n", errorMessage);
        return false;
    }

    addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);

    return Register(vin, CService(strService), keyCollateralAddress, pubKeyCollateralAddress, keyCoralnode, pubKeyCoralnode, errorMessage);
}

bool CActiveCoralnode::Register(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyCoralnode, CPubKey pubKeyCoralnode, std::string& errorMessage)
{
    CCoralnodeBroadcast mnb;
    CCoralnodePing mnp(vin);
    if (!mnp.Sign(keyCoralnode, pubKeyCoralnode)) {
        errorMessage = strprintf("Failed to sign ping, vin: %s", vin.ToString());
        LogPrintf("CActiveCoralnode::Register() -  %s\n", errorMessage);
        return false;
    }
    mnodeman.mapSeenCoralnodePing.insert(make_pair(mnp.GetHash(), mnp));

    LogPrintf("CActiveCoralnode::Register() - Adding to Coralnode list\n    service: %s\n    vin: %s\n", service.ToString(), vin.ToString());
    mnb = CCoralnodeBroadcast(service, vin, pubKeyCollateralAddress, pubKeyCoralnode, PROTOCOL_VERSION);
    mnb.lastPing = mnp;
    if (!mnb.Sign(keyCollateralAddress)) {
        errorMessage = strprintf("Failed to sign broadcast, vin: %s", vin.ToString());
        LogPrintf("CActiveCoralnode::Register() - %s\n", errorMessage);
        return false;
    }
    mnodeman.mapSeenCoralnodeBroadcast.insert(make_pair(mnb.GetHash(), mnb));
    coralnodeSync.AddedCoralnodeList(mnb.GetHash());

    CCoralnode* pmn = mnodeman.Find(vin);
    if (pmn == NULL) {
        CCoralnode mn(mnb);
        mnodeman.Add(mn);
    } else {
        pmn->UpdateFromNewBroadcast(mnb);
    }

    //send to all peers
    LogPrintf("CActiveCoralnode::Register() - RelayElectionEntry vin = %s\n", vin.ToString());
    mnb.Relay();

    /*
     * IT'S SAFE TO REMOVE THIS IN FURTHER VERSIONS
     * AFTER MIGRATION TO V12 IS DONE
     */

    if (IsSporkActive(SPORK_10_CORALNODE_PAY_UPDATED_NODES)) return true;
    // for migration purposes inject our node in old coralnodes' list too
    std::string retErrorMessage;
    std::vector<unsigned char> vchCoralNodeSignature;
    int64_t coralNodeSignatureTime = GetAdjustedTime();
    std::string donationAddress = "";
    int donationPercantage = 0;

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyCoralnode.begin(), pubKeyCoralnode.end());

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(coralNodeSignatureTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(PROTOCOL_VERSION) + donationAddress + boost::lexical_cast<std::string>(donationPercantage);

    if (!obfuScationSigner.SignMessage(strMessage, retErrorMessage, vchCoralNodeSignature, keyCollateralAddress)) {
        errorMessage = "dsee sign message failed: " + retErrorMessage;
        LogPrintf("CActiveCoralnode::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!obfuScationSigner.VerifyMessage(pubKeyCollateralAddress, vchCoralNodeSignature, strMessage, retErrorMessage)) {
        errorMessage = "dsee verify message failed: " + retErrorMessage;
        LogPrintf("CActiveCoralnode::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    LOCK(cs_vNodes);
    BOOST_FOREACH (CNode* pnode, vNodes)
        pnode->PushMessage("obsee", vin, service, vchCoralNodeSignature, coralNodeSignatureTime, pubKeyCollateralAddress, pubKeyCoralnode, -1, -1, coralNodeSignatureTime, PROTOCOL_VERSION, donationAddress, donationPercantage);

    /*
     * END OF "REMOVE"
     */

    return true;
}

bool CActiveCoralnode::GetCoralNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    return GetCoralNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveCoralnode::GetCoralNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex)
{
    // Find possible candidates
    TRY_LOCK(pwalletMain->cs_wallet, fWallet);
    if (!fWallet) return false;

    vector<COutput> possibleCoins = SelectCoinsCoralnode();
    COutput* selectedOutput;

    // Find the vin
    if (!strTxHash.empty()) {
        // Let's find it
        uint256 txHash(strTxHash);
        int outputIndex;
        try {
            outputIndex = std::stoi(strOutputIndex.c_str());
        } catch (const std::exception& e) {
            LogPrintf("%s: %s on strOutputIndex\n", __func__, e.what());
            return false;
        }

        bool found = false;
        BOOST_FOREACH (COutput& out, possibleCoins) {
            if (out.tx->GetHash() == txHash && out.i == outputIndex) {
                selectedOutput = &out;
                found = true;
                break;
            }
        }
        if (!found) {
            LogPrintf("CActiveCoralnode::GetCoralNodeVin - Could not locate valid vin\n");
            return false;
        }
    } else {
        // No output specified,  Select the first one
        if (possibleCoins.size() > 0) {
            selectedOutput = &possibleCoins[0];
        } else {
            LogPrintf("CActiveCoralnode::GetCoralNodeVin - Could not locate specified vin from possible list\n");
            return false;
        }
    }

    // At this point we have a selected output, retrieve the associated info
    return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract Coralnode vin information from output
bool CActiveCoralnode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey)
{
    CScript pubScript;

    vin = CTxIn(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActiveCoralnode::GetCoralNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf("CActiveCoralnode::GetCoralNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running Coralnode
vector<COutput> CActiveCoralnode::SelectCoinsCoralnode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;
    vector<COutPoint> confLockedCoins;

    // Temporary unlock MN coins from coralnode.conf
    if (GetBoolArg("-mnconflock", true)) {
        uint256 mnTxHash;
        BOOST_FOREACH (CCoralnodeConfig::CCoralnodeEntry mne, coralnodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());

            int nIndex;
            if(!mne.castOutputIndex(nIndex))
                continue;

            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            confLockedCoins.push_back(outpoint);
            pwalletMain->UnlockCoin(outpoint);
        }
    }

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Lock MN coins from coralnode.conf back if they where temporary unlocked
    if (!confLockedCoins.empty()) {
        BOOST_FOREACH (COutPoint outpoint, confLockedCoins)
            pwalletMain->LockCoin(outpoint);
    }

    // Filter
    BOOST_FOREACH (const COutput& out, vCoins) {
        if (out.tx->vout[out.i].nValue == FN_MAGIC_AMOUNT) { //exactly
            filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a Coralnode, this can enable to run as a hot wallet with no funds
bool CActiveCoralnode::EnableHotColdCoralNode(CTxIn& newVin, CService& newService)
{
    if (!fCoralNode) return false;

    status = ACTIVE_CORALNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveCoralnode::EnableHotColdCoralNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
