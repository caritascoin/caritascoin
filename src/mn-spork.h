
// Copyright (c) 2009-2012 The Bitsend developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The VITAE developers and CaritasCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MN_SPORK_H
#define MN_SPORK_H

//#include "bignum.h"
#include "key.h"
#include "net.h"
#include "sync.h"
//#include "core.h"
#include "base58.h"
#include "main.h"
#include "script/script.h"
#include "util.h"

using namespace std;
using namespace boost;

// Don't ever reuse these IDs for other mn_sporks
#define MN_SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT 10000
#define MN_SPORK_2_INSTANTX 10001
#define MN_SPORK_3_INSTANTX_BLOCK_FILTERING 10002
#define MN_SPORK_4_NOTUSED 10003
#define MN_SPORK_5_MAX_VALUE 10004
#define MN_SPORK_6_NOTUSED 10005
#define MN_SPORK_7_MASTERNODE_SCANNING 10006

#define MN_SPORK_1_MASTERNODE_PAYMENTS_ENFORCEMENT_DEFAULT 1424217600 //2015-2-18
#define MN_SPORK_2_INSTANTX_DEFAULT 978307200                         //2001-1-1
#define MN_SPORK_3_INSTANTX_BLOCK_FILTERING_DEFAULT 1424217600        //2015-2-18
#define MN_SPORK_5_MAX_VALUE_DEFAULT 10000                            //10000 BSD 01-05-2015   // Sprungmarke BBBBBBBBBB
#define MN_SPORK_7_MASTERNODE_SCANNING_DEFAULT 978307200              //2001-1-1

#define MN_SPORK_START 10000
#define	MN_SPORK_END 10006

class CMNSporkMessage;
class CMNSporkManager;

//#include "bignum.h"
#include "key.h"
#include "net.h"
#include "obfuscation.h"
#include "protocol.h"
#include "util.h"
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;

extern std::map<uint256, CMNSporkMessage> mapMNSporks;
extern std::map<int, CMNSporkMessage> mapMNSporksActive;
extern CMNSporkManager mn_sporkManager;

void ProcessMNSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
int GetMNSporkValue(int nMNSporkID);
bool IsMNSporkActive(int nMNSporkID);
void ExecuteMNSpork(int nMNSporkID, int nValue);

//
// MNSpork Class
// Keeps track of all of the network mn_spork settings
//

class CMNSporkMessage
{
public:
    std::vector<unsigned char> vchSig;
    int nMNSporkID;
    int64_t nValue;
    int64_t nTimeSigned;


    uint256 GetHash()
    {
        uint256 n;

        {
            n = HashQuark(BEGIN(nMNSporkID), END(nTimeSigned));
            return n;
        }
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nMNSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
        READWRITE(vchSig);
    }
};


class CMNSporkManager
{
private:
    std::vector<unsigned char> vchSig;

    std::string strMasterPrivKey;
    std::string strTestPubKey;
    std::string strMainPubKey;

public:
    CMNSporkManager()
    {
        // 100: G=0 101: MK just test
        strMainPubKey = "049CDEDDB66230782D70BC1A94C85EF6EF20222BB14E5CF036C412C7E05F94D60B2C93F16DE64E456490D984C526A6F46D9B511619BE20BFC54D36113FC14B312F"; // bitsenddev 04-2015
        strTestPubKey = "047cb1d068ef01a90200652ee3b350d660b7829d761417496716e41c9722d3b38bf099546a56f6e71c98b58e5616296172726377c6b2082c60c7bc4a1d54159c6b"; // bitsenddev do not use 04-2015
    }

    std::string GetMNSporkNameByID(int id);
    int GetMNSporkIDByName(std::string strName);
    bool UpdateMNSpork(int nMNSporkID, int64_t nValue);
    bool SetPrivKey(std::string strPrivKey);
    bool CheckSignature(CMNSporkMessage& mn_spork);
    bool Sign(CMNSporkMessage& mn_spork);
    void Relay(CMNSporkMessage& msg);
};

#endif
