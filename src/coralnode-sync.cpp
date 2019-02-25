// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "net.h"
#include "activecoralnode.h"
#include "coralnode-sync.h"
#include "coralnode-payments.h"
#include "coralnode-budget.h"
#include "coralnode.h"
#include "coralnodeman.h"
#include "spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CCoralnodeSync;
CCoralnodeSync coralnodeSync;

CCoralnodeSync::CCoralnodeSync()
{
    Reset();
}

bool CCoralnodeSync::IsSynced()
{
    return RequestedCoralnodeAssets == CORALNODE_SYNC_FINISHED;
}

bool CCoralnodeSync::IsBlockchainSynced()
{
    static bool fBlockchainSynced = false;
    static int64_t lastProcess = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - lastProcess > 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = GetTime();

    if (fBlockchainSynced) return true;

    if (fImporting || fReindex) return false;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain) return false;

    CBlockIndex* pindex = chainActive.Tip();
    if (pindex == NULL) return false;


    if (pindex->nTime + 60 * 60 < GetTime())
        return false;

    fBlockchainSynced = true;

    return true;
}

void CCoralnodeSync::Reset()
{
    lastCoralnodeList = 0;
    lastCoralnodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncMNB.clear();
    mapSeenSyncMNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumCoralnodeList = 0;
    sumCoralnodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countCoralnodeList = 0;
    countCoralnodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    RequestedCoralnodeAssets = CORALNODE_SYNC_INITIAL;
    RequestedCoralnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CCoralnodeSync::AddedCoralnodeList(uint256 hash)
{
    if (mnodeman.mapSeenCoralnodeBroadcast.count(hash)) {
        if (mapSeenSyncMNB[hash] < CORALNODE_SYNC_THRESHOLD) {
            lastCoralnodeList = GetTime();
            mapSeenSyncMNB[hash]++;
        }
    } else {
        lastCoralnodeList = GetTime();
        mapSeenSyncMNB.insert(make_pair(hash, 1));
    }
}

void CCoralnodeSync::AddedCoralnodeWinner(uint256 hash)
{
    if (coralnodePayments.mapCoralnodePayeeVotes.count(hash)) {
        if (mapSeenSyncMNW[hash] < CORALNODE_SYNC_THRESHOLD) {
            lastCoralnodeWinner = GetTime();
            mapSeenSyncMNW[hash]++;
        }
    } else {
        lastCoralnodeWinner = GetTime();
        mapSeenSyncMNW.insert(make_pair(hash, 1));
    }
}

void CCoralnodeSync::AddedBudgetItem(uint256 hash)
{
    if (budget.mapSeenCoralnodeBudgetProposals.count(hash) || budget.mapSeenCoralnodeBudgetVotes.count(hash) ||
        budget.mapSeenFinalizedBudgets.count(hash) || budget.mapSeenFinalizedBudgetVotes.count(hash)) {
        if (mapSeenSyncBudget[hash] < CORALNODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.insert(make_pair(hash, 1));
    }
}

bool CCoralnodeSync::IsBudgetPropEmpty()
{
    return sumBudgetItemProp == 0 && countBudgetItemProp > 0;
}

bool CCoralnodeSync::IsBudgetFinEmpty()
{
    return sumBudgetItemFin == 0 && countBudgetItemFin > 0;
}

void CCoralnodeSync::GetNextAsset()
{
    switch (RequestedCoralnodeAssets) {
    case (CORALNODE_SYNC_INITIAL):
    case (CORALNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest();
        RequestedCoralnodeAssets = CORALNODE_SYNC_SPORKS;
        break;
    case (CORALNODE_SYNC_SPORKS):
        RequestedCoralnodeAssets = CORALNODE_SYNC_LIST;
        break;
    case (CORALNODE_SYNC_LIST):
        RequestedCoralnodeAssets = CORALNODE_SYNC_MNW;
        break;
    case (CORALNODE_SYNC_MNW):
        RequestedCoralnodeAssets = CORALNODE_SYNC_BUDGET;
        break;
    case (CORALNODE_SYNC_BUDGET):
        LogPrintf("CCoralnodeSync::GetNextAsset - Sync has finished\n");
        RequestedCoralnodeAssets = CORALNODE_SYNC_FINISHED;
        break;
    }
    RequestedCoralnodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CCoralnodeSync::GetSyncStatus()
{
    switch (coralnodeSync.RequestedCoralnodeAssets) {
    case CORALNODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case CORALNODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case CORALNODE_SYNC_LIST:
        return _("Synchronizing coralnodes...");
    case CORALNODE_SYNC_MNW:
        return _("Synchronizing coralnode winners...");
    case CORALNODE_SYNC_BUDGET:
        return _("Synchronizing budgets...");
    case CORALNODE_SYNC_FAILED:
        return _("Synchronization failed");
    case CORALNODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CCoralnodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedCoralnodeAssets >= CORALNODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (CORALNODE_SYNC_LIST):
            if (nItemID != RequestedCoralnodeAssets) return;
            sumCoralnodeList += nCount;
            countCoralnodeList++;
            break;
        case (CORALNODE_SYNC_MNW):
            if (nItemID != RequestedCoralnodeAssets) return;
            sumCoralnodeWinner += nCount;
            countCoralnodeWinner++;
            break;
        case (CORALNODE_SYNC_BUDGET_PROP):
            if (RequestedCoralnodeAssets != CORALNODE_SYNC_BUDGET) return;
            sumBudgetItemProp += nCount;
            countBudgetItemProp++;
            break;
        case (CORALNODE_SYNC_BUDGET_FIN):
            if (RequestedCoralnodeAssets != CORALNODE_SYNC_BUDGET) return;
            sumBudgetItemFin += nCount;
            countBudgetItemFin++;
            break;
        }

        LogPrint("coralnode", "CCoralnodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CCoralnodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("fnsync");
        pnode->ClearFulfilledRequest("fnwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CCoralnodeSync::Process()
{
    static int tick = 0;

    if (tick++ % CORALNODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /*
            Resync if we lose all coralnodes from sleep/wake or failure to sync originally
        */
        if (tick % 60 != 0) return;
        if (mnodeman.CountEnabled() == 0) {
            Reset();
        } else
            return;
    }

    //try syncing again
    if (RequestedCoralnodeAssets == CORALNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedCoralnodeAssets == CORALNODE_SYNC_FAILED) {
        return;
    }

    LogPrint("coralnode", "CCoralnodeSync::Process() - tick %d RequestedCoralnodeAssets %d\n", tick, RequestedCoralnodeAssets);

    if (RequestedCoralnodeAssets == CORALNODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (Params().NetworkID() != CBaseChainParams::REGTEST &&
        !IsBlockchainSynced() && RequestedCoralnodeAssets > CORALNODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (RequestedCoralnodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if (RequestedCoralnodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (RequestedCoralnodeAttempt < 6) {
                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("fnget", nMnCount); //sync payees
                uint256 n = 0;
                pnode->PushMessage("fnvs", n); //sync coralnode votes
            } else {
                RequestedCoralnodeAssets = CORALNODE_SYNC_FINISHED;
            }
            RequestedCoralnodeAttempt++;
            return;
        }

        //set to synced
        if (RequestedCoralnodeAssets == CORALNODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if (RequestedCoralnodeAttempt >= 2) GetNextAsset();
            RequestedCoralnodeAttempt++;

            return;
        }

        if (pnode->nVersion >= coralnodePayments.GetMinCoralnodePaymentsProto()) {
            if (RequestedCoralnodeAssets == CORALNODE_SYNC_LIST) {
                LogPrint("coralnode", "CCoralnodeSync::Process() - lastCoralnodeList %lld (GetTime() - CORALNODE_SYNC_TIMEOUT) %lld\n", lastCoralnodeList, GetTime() - CORALNODE_SYNC_TIMEOUT);
                if (lastCoralnodeList > 0 && lastCoralnodeList < GetTime() - CORALNODE_SYNC_TIMEOUT * 2 && RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("fnsync")) continue;
                pnode->FulfilledRequest("fnsync");

                // timeout
                if (lastCoralnodeList == 0 &&
                    (RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > CORALNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_CORALNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CCoralnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedCoralnodeAssets = CORALNODE_SYNC_FAILED;
                        RequestedCoralnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD * 3) return;

                mnodeman.DsegUpdate(pnode);
                RequestedCoralnodeAttempt++;
                return;
            }

            if (RequestedCoralnodeAssets == CORALNODE_SYNC_MNW) {
                if (lastCoralnodeWinner > 0 && lastCoralnodeWinner < GetTime() - CORALNODE_SYNC_TIMEOUT * 2 && RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("fnwsync")) continue;
                pnode->FulfilledRequest("fnwsync");

                // timeout
                if (lastCoralnodeWinner == 0 &&
                    (RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > CORALNODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_CORALNODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CCoralnodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedCoralnodeAssets = CORALNODE_SYNC_FAILED;
                        RequestedCoralnodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD * 3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return;

                int nMnCount = mnodeman.CountEnabled();
                pnode->PushMessage("fnget", nMnCount); //sync payees
                RequestedCoralnodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= ActiveProtocol()) {
            if (RequestedCoralnodeAssets == CORALNODE_SYNC_BUDGET) {

                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - CORALNODE_SYNC_TIMEOUT * 2 && RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD) {

                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our coralnode if possible
                    activeCoralnode.ManageStatus();

                    return;
                }

                // timeout
                if (lastBudgetItem == 0 &&
                    (RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > CORALNODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset();
                    activeCoralnode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if (RequestedCoralnodeAttempt >= CORALNODE_SYNC_THRESHOLD * 3) return;

                uint256 n = 0;
                pnode->PushMessage("fnvs", n); //sync coralnode votes
                RequestedCoralnodeAttempt++;

                return;
            }
        }
    }
}
