// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CORALNODE_SYNC_H
#define CORALNODE_SYNC_H

#define CORALNODE_SYNC_INITIAL 0
#define CORALNODE_SYNC_SPORKS 1
#define CORALNODE_SYNC_LIST 2
#define CORALNODE_SYNC_MNW 3
#define CORALNODE_SYNC_BUDGET 4
#define CORALNODE_SYNC_BUDGET_PROP 10
#define CORALNODE_SYNC_BUDGET_FIN 11
#define CORALNODE_SYNC_FAILED 998
#define CORALNODE_SYNC_FINISHED 999

#define CORALNODE_SYNC_TIMEOUT 5
#define CORALNODE_SYNC_THRESHOLD 2

class CCoralnodeSync;
extern CCoralnodeSync coralnodeSync;

//
// CCoralnodeSync : Sync coralnode assets in stages
//

class CCoralnodeSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;
    std::map<uint256, int> mapSeenSyncMNW;
    std::map<uint256, int> mapSeenSyncBudget;

    int64_t lastCoralnodeList;
    int64_t lastCoralnodeWinner;
    int64_t lastBudgetItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumCoralnodeList;
    int sumCoralnodeWinner;
    int sumBudgetItemProp;
    int sumBudgetItemFin;
    // peers that reported counts
    int countCoralnodeList;
    int countCoralnodeWinner;
    int countBudgetItemProp;
    int countBudgetItemFin;

    // Count peers we've requested the list from
    int RequestedCoralnodeAssets;
    int RequestedCoralnodeAttempt;

    // Time when current coralnode asset sync started
    int64_t nAssetSyncStarted;

    CCoralnodeSync();

    void AddedCoralnodeList(uint256 hash);
    void AddedCoralnodeWinner(uint256 hash);
    void AddedBudgetItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsBudgetFinEmpty();
    bool IsBudgetPropEmpty();

    void Reset();
    void Process();
    bool IsSynced();
    bool IsBlockchainSynced();
    bool IsCoralnodeListSynced() { return RequestedCoralnodeAssets > CORALNODE_SYNC_LIST; }
    void ClearFulfilledRequest();
};

#endif
