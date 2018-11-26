/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "Tools/Language.h"
#include "WorldPacket.h"
#include "Server/WorldSession.h"
#include "World/World.h"
#include "Globals/ObjectMgr.h"
#include "Log.h"
#include "Server/Opcodes.h"
#include "Guilds/Guild.h"
#include "Guilds/GuildMgr.h"
#include "Arena/ArenaTeam.h"

/*enum PetitionType // dbc data
{
    PETITION_TYPE_GUILD      = 1,
    PETITION_TYPE_ARENA_TEAM = 3
};*/

// Charters ID in item_template
#define GUILD_CHARTER               5863
#define GUILD_CHARTER_COST          1000                    // 10 S
#define ARENA_TEAM_CHARTER_2v2      23560
#define ARENA_TEAM_CHARTER_2v2_COST 800000                  // 80 G
#define ARENA_TEAM_CHARTER_3v3      23561
#define ARENA_TEAM_CHARTER_3v3_COST 1200000                 // 120 G
#define ARENA_TEAM_CHARTER_5v5      23562
#define ARENA_TEAM_CHARTER_5v5_COST 2000000                 // 200 G
#define CHARTER_DISPLAY_ID          16161

//购买公会注册表/竞技场注册表
void WorldSession::HandlePetitionBuyOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_PETITION_BUY");
    recv_data.hexlike();

    ObjectGuid guidNPC;
    uint32 clientIndex;                                     // 1 for guild and arenaslot+1 for arenas in client
    std::string name;                                       //公会/竞技场队伍名称

    recv_data >> guidNPC;                                   // NPC GUID
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint64>();                          // 0
    recv_data >> name;                                      // name
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint32>();                          // 0
    recv_data.read_skip<uint16>();                          // 0
    recv_data.read_skip<uint8>();                           // 0

    recv_data >> clientIndex;                               // index
    recv_data.read_skip<uint32>();                          // 0

    DEBUG_LOG("Petitioner %s tried sell petition: name %s", guidNPC.GetString().c_str(), name.c_str());

    // prevent cheating
    //获取NPC信息
    Creature* pCreature = GetPlayer()->GetNPCIfCanInteractWith(guidNPC, UNIT_NPC_FLAG_PETITIONER);
    if (!pCreature)
    {
        DEBUG_LOG("WORLD: HandlePetitionBuyOpcode - %s not found or you can't interact with him.", guidNPC.GetString().c_str());
        return;
    }

    uint32 charterid;
    uint32 cost;
    uint32 type;
    if (pCreature->isTabardDesigner())  //公会管理员
    {
        // if tabard designer, then trying to buy a guild charter.
        // do not let if already in guild.
        if (_player->GetGuildId())      //已在公会中
            return;

        charterid = GUILD_CHARTER;      
        cost = GUILD_CHARTER_COST;
        type = 9;
    }
    else
    {
        // TODO: find correct opcode
        //没有满级的玩家不允许购买竞技场注册表
        if (_player->getLevel() < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        {
            SendNotification(LANG_ARENA_ONE_TOOLOW, sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL));
            return;
        }

        switch (clientIndex)                                // arenaSlot+1 as received from client (1 from 3 case)
        {
            case 1:
                charterid = ARENA_TEAM_CHARTER_2v2;
                cost = ARENA_TEAM_CHARTER_2v2_COST;
                type = 2;                                   // 2v2
                break;
            case 2:
                charterid = ARENA_TEAM_CHARTER_3v3;
                cost = ARENA_TEAM_CHARTER_3v3_COST;
                type = 3;                                   // 3v3
                break;
            case 3:
                charterid = ARENA_TEAM_CHARTER_5v5;
                cost = ARENA_TEAM_CHARTER_5v5_COST;
                type = 5;                                   // 5v5
                break;
            default:
                DEBUG_LOG("unknown selection at buy arena petition: %u", clientIndex);
                return;
        }

        //判断玩家是否已有竞技场队伍
        if (_player->GetArenaTeamId(clientIndex - 1))       // arenaSlot+1 as received from client
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ALREADY_IN_ARENA_TEAM);
            return;
        }
    }

    if (type == 9)  //购买公会注册表
    {
        //判断公会名是否已存在
        if (sGuildMgr.GetGuildByName(name))
        {
            SendGuildCommandResult(GUILD_CREATE_S, name, ERR_GUILD_NAME_EXISTS_S);
            return;
        }

        //公会名字合法性校验
        if (sObjectMgr.IsReservedName(name) || !ObjectMgr::IsValidCharterName(name))
        {
            SendGuildCommandResult(GUILD_CREATE_S, name, ERR_GUILD_NAME_INVALID);
            return;
        }
    }
    else
    {
        //判断竞技场名字是否已存在
        if (sObjectMgr.GetArenaTeamByName(name))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ARENA_TEAM_NAME_EXISTS_S);
            return;
        }

        //判断竞技场名字合法性
        if (sObjectMgr.IsReservedName(name) || !ObjectMgr::IsValidCharterName(name))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ARENA_TEAM_NAME_INVALID);
            return;
        }
    }

    //获取注册表属性
    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(charterid);
    if (!pProto)
    {
        _player->SendBuyError(BUY_ERR_CANT_FIND_ITEM, nullptr, charterid, 0);
        return;
    }

    //判断玩家携带的金钱是否足以购买注册表
    if (_player->GetMoney() < cost)
    {
        // player hasn't got enough money
        _player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, pCreature, charterid, 0);
        return;
    }

    ItemPosCountVec dest;
    InventoryResult msg = _player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, charterid, pProto->BuyCount);
    if (msg != EQUIP_ERR_OK)
    {
        _player->SendEquipError(msg, nullptr, nullptr, charterid);
        return;
    }

    //玩家扣除金钱
    _player->ModifyMoney(-(int32)cost);
    Item* charter = _player->StoreNewItem(dest, charterid, true);
    if (!charter)
        return;

    charter->SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1, charter->GetGUIDLow());
    // ITEM_FIELD_ENCHANTMENT_1_1 is guild/arenateam id
    // ITEM_FIELD_ENCHANTMENT_1_1+1 is current signatures count (showed on item)
    charter->SetState(ITEM_CHANGED, _player);
    _player->SendNewItem(charter, 1, true, false);

    // a petition is invalid, if both the owner and the type matches
    // we checked above, if this player is in an arenateam, so this must be data corruption
    QueryResult* result = CharacterDatabase.PQuery("SELECT petitionguid FROM petition WHERE ownerguid = '%u'  AND type = '%u'", _player->GetGUIDLow(), type);

    std::ostringstream ssInvalidPetitionGUIDs;

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            ssInvalidPetitionGUIDs << "'" << fields[0].GetUInt32() << "' , ";
        }
        while (result->NextRow());

        delete result;
    }

    // delete petitions with the same guid as this one
    ssInvalidPetitionGUIDs << "'" << charter->GetGUIDLow() << "'";

    DEBUG_LOG("Invalid petition GUIDs: %s", ssInvalidPetitionGUIDs.str().c_str());
    CharacterDatabase.escape_string(name);
    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM petition WHERE petitionguid IN ( %s )",  ssInvalidPetitionGUIDs.str().c_str());
    CharacterDatabase.PExecute("DELETE FROM petition_sign WHERE petitionguid IN ( %s )", ssInvalidPetitionGUIDs.str().c_str());
    CharacterDatabase.PExecute("INSERT INTO petition (ownerguid, petitionguid, name, type) VALUES ('%u', '%u', '%s', '%u')",
                               _player->GetGUIDLow(), charter->GetGUIDLow(), name.c_str(), type);
    CharacterDatabase.CommitTransaction();
}

//展示公会注册表/竞技场注册表签名
void WorldSession::HandlePetitionShowSignOpcode(WorldPacket& recv_data)
{
    // ok
    DEBUG_LOG("Received opcode CMSG_PETITION_SHOW_SIGNATURES");
    // recv_data.hexlike();

    uint8 signs = 0;
    ObjectGuid petitionguid;                //注册表guid
    recv_data >> petitionguid;                              // petition guid

    // solve (possible) some strange compile problems with explicit use GUID_LOPART(petitionguid) at some GCC versions (wrong code optimization in compiler?)
    uint32 petitionguid_low = petitionguid.GetCounter();

    //查询注册表信息
    QueryResult* result = CharacterDatabase.PQuery("SELECT type FROM petition WHERE petitionguid = '%u'", petitionguid_low);
    if (!result)
    {
        sLog.outError("any petition on server...");
        return;
    }
    Field* fields = result->Fetch();
    uint32 type = fields[0].GetUInt32();    //注册表类型
    delete result;

    // if guild petition and has guild => error, return;
    //有错误
    if (type == 9 && _player->GetGuildId())
        return;

    //获取注册表签名信息
    result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitionguid_low);

    // result==nullptr also correct in case no sign yet
    if (result)
        signs = (uint8)result->GetRowCount();

    DEBUG_LOG("CMSG_PETITION_SHOW_SIGNATURES petition: %s", petitionguid.GetString().c_str());

    //返回签名相关信息
    WorldPacket data(SMSG_PETITION_SHOW_SIGNATURES, (8 + 8 + 4 + 1 + signs * 12));
    data << ObjectGuid(petitionguid);                       // petition guid
    data << _player->GetObjectGuid();                       // owner guid
    data << uint32(petitionguid_low);                       // guild guid (in mangos always same as GUID_LOPART(petitionguid)
    data << uint8(signs);                                   // sign's count

    for (uint8 i = 1; i <= signs; ++i)
    {
        Field* fields2 = result->Fetch();
        ObjectGuid signerGuid = ObjectGuid(HIGHGUID_PLAYER, fields2[0].GetUInt32());

        data << ObjectGuid(signerGuid);                     // Player GUID
        data << uint32(0);                                  // there 0 ...

        result->NextRow();
    }
    delete result;
    SendPacket(data);
}

void WorldSession::HandlePetitionQueryOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_PETITION_QUERY");
    // recv_data.hexlike();

    uint32 guildguid;
    ObjectGuid petitionguid;
    recv_data >> guildguid;                                 // in mangos always same as GUID_LOPART(petitionguid)
    recv_data >> petitionguid;                              // petition guid
    DEBUG_LOG("CMSG_PETITION_QUERY Petition %s Guild GUID %u", petitionguid.GetString().c_str(), guildguid);

    SendPetitionQueryOpcode(petitionguid);
}

void WorldSession::SendPetitionQueryOpcode(ObjectGuid petitionguid) const
{
    uint32 petitionLowGuid = petitionguid.GetCounter();

    QueryResult* result = CharacterDatabase.PQuery(
                              "SELECT ownerguid, name, "
                              "  (SELECT COUNT(playerguid) FROM petition_sign WHERE petition_sign.petitionguid = '%u') AS signs, "
                              "  type "
                              "FROM petition WHERE petitionguid = '%u'", petitionLowGuid, petitionLowGuid);

    if (!result)
    {
        DEBUG_LOG("CMSG_PETITION_QUERY failed for petition (GUID: %u)", petitionLowGuid);
        return;
    }

    Field* fields = result->Fetch();
    ObjectGuid ownerGuid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
    std::string name = fields[1].GetCppString();
    //uint8 signs = fields[2].GetUInt8();
    uint32 type = fields[3].GetUInt32();
    delete result;

    WorldPacket data(SMSG_PETITION_QUERY_RESPONSE, (4 + 8 + name.size() + 1 + 1 + 4 * 13 + 2));
    data << uint32(petitionLowGuid);                        // guild/team guid (in mangos always same as GUID_LOPART(petition guid)
    data << ObjectGuid(ownerGuid);                          // charter owner guid
    data << name;                                           // name (guild/arena team)
    data << uint8(0);                                       // 1
    if (type == 9)
    {
        data << uint32(9);
        data << uint32(9);
        data << uint32(0);                                  // bypass client - side limitation, a different value is needed here for each petition
    }
    else
    {
        data << uint32(type - 1);
        data << uint32(type - 1);
        data << uint32(type);                               // bypass client - side limitation, a different value is needed here for each petition
    }
    data << uint32(0);                                      // 5
    data << uint32(0);                                      // 6
    data << uint32(0);                                      // 7
    data << uint32(0);                                      // 8
    data << uint16(0);                                      // 9 2 bytes field
    data << uint32(0);                                      // 10
    data << uint32(0);                                      // 11
    data << uint32(0);                                      // 13 count of next strings?
    data << uint32(0);                                      // 14

    if (type == 9)
        data << uint32(0);                                  // 15 0 - guild, 1 - arena team
    else
        data << uint32(1);

    SendPacket(data);
}

void WorldSession::HandlePetitionRenameOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode MSG_PETITION_RENAME");   // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;
    uint32 type;
    std::string newname;

    recv_data >> petitionGuid;                              // guid
    recv_data >> newname;                                   // new name

    Item* item = _player->GetItemByGuid(petitionGuid);
    if (!item)
        return;

    QueryResult* result = CharacterDatabase.PQuery("SELECT type FROM petition WHERE petitionguid = '%u'", petitionGuid.GetCounter());

    if (result)
    {
        Field* fields = result->Fetch();
        type = fields[0].GetUInt32();
        delete result;
    }
    else
    {
        DEBUG_LOG("CMSG_PETITION_QUERY failed for petition: %s", petitionGuid.GetString().c_str());
        return;
    }

    if (type == 9)
    {
        if (sGuildMgr.GetGuildByName(newname))
        {
            SendGuildCommandResult(GUILD_CREATE_S, newname, ERR_GUILD_NAME_EXISTS_S);
            return;
        }
        if (sObjectMgr.IsReservedName(newname) || !ObjectMgr::IsValidCharterName(newname))
        {
            SendGuildCommandResult(GUILD_CREATE_S, newname, ERR_GUILD_NAME_INVALID);
            return;
        }
    }
    else
    {
        if (sObjectMgr.GetArenaTeamByName(newname))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, newname, "", ERR_ARENA_TEAM_NAME_EXISTS_S);
            return;
        }
        if (sObjectMgr.IsReservedName(newname) || !ObjectMgr::IsValidCharterName(newname))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, newname, "", ERR_ARENA_TEAM_NAME_INVALID);
            return;
        }
    }

    std::string db_newname = newname;
    CharacterDatabase.escape_string(db_newname);
    CharacterDatabase.PExecute("UPDATE petition SET name = '%s' WHERE petitionguid = '%u'",
                               db_newname.c_str(), petitionGuid.GetCounter());

    DEBUG_LOG("Petition %s renamed to '%s'", petitionGuid.GetString().c_str(), newname.c_str());

    WorldPacket data(MSG_PETITION_RENAME, (8 + newname.size() + 1));
    data << ObjectGuid(petitionGuid);
    data << newname;
    SendPacket(data);
}

//注册表签名
void WorldSession::HandlePetitionSignOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_PETITION_SIGN");    // ok
    // recv_data.hexlike();

    Field* fields;
    ObjectGuid petitionGuid;    //注册表guid
    uint8 unk;
    recv_data >> petitionGuid;                              // petition guid
    recv_data >> unk;

    uint32 petitionLowGuid = petitionGuid.GetCounter();

    //获取注册表相关信息
    QueryResult* result = CharacterDatabase.PQuery(
                              "SELECT ownerguid, "
                              "  (SELECT COUNT(playerguid) FROM petition_sign WHERE petition_sign.petitionguid = '%u') AS signs, "
                              "  type "
                              "FROM petition WHERE petitionguid = '%u'", petitionLowGuid, petitionLowGuid);

    if (!result)
    {
        sLog.outError("any petition on server...");
        return;
    }

    fields = result->Fetch();
    uint32 ownerLowGuid = fields[0].GetUInt32();
    ObjectGuid ownerGuid = ObjectGuid(HIGHGUID_PLAYER, ownerLowGuid);
    uint8 signs = fields[1].GetUInt8();     //当前签名数量
    uint32 type = fields[2].GetUInt32();    //类型

    delete result;

    //自己不允许签名
    if (ownerGuid == _player->GetObjectGuid())
        return;

    // not let enemies sign guild charter
    //对立阵营不允许签名
    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) &&
            GetPlayer()->GetTeam() != sObjectMgr.GetPlayerTeamByGUID(ownerGuid))
    {
        if (type != 9)
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", "", ERR_ARENA_TEAM_NOT_ALLIED);
        else
            SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_NOT_ALLIED);
        return;
    }

    if (type != 9)  //竞技场注册表
    {
        //不满级玩家不允许签名竞技场注册表
        if (_player->getLevel() < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", _player->GetName(), ERR_ARENA_TEAM_TARGET_TOO_LOW_S);
            return;
        }

        //判断竞技场注册表类型是否合法
        if (!IsArenaTypeValid(ArenaType(type)))
            return;

        uint8 slot = ArenaTeam::GetSlotByType(ArenaType(type));
        if (slot >= MAX_ARENA_SLOT)
            return;

        //玩家已有该类型的竞技场队伍, 不允许签名
        if (_player->GetArenaTeamId(slot))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", _player->GetName(), ERR_ALREADY_IN_ARENA_TEAM_S);
            return;
        }

        //玩家已有该类型竞技场队伍的邀请, 不允许签名
        if (_player->GetArenaTeamIdInvited())
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", _player->GetName(), ERR_ALREADY_INVITED_TO_ARENA_TEAM_S);
            return;
        }
    }
    else    //公会注册表
    {
        //获取公会信息, 已在公会里不允许签名
        if (_player->GetGuildId())
        {
            SendGuildCommandResult(GUILD_INVITE_S, _player->GetName(), ERR_ALREADY_IN_GUILD_S);
            return;
        }
        //已有公会邀请不允许签名
        if (_player->GetGuildIdInvited())
        {
            SendGuildCommandResult(GUILD_INVITE_S, _player->GetName(), ERR_ALREADY_INVITED_TO_GUILD_S);
            return;
        }
    }

    //签名数量大于限制, 返回
    if (++signs > type)                                     // client signs maximum
        return;

    // client doesn't allow to sign petition two times by one character, but not check sign by another character from same account
    // not allow sign another player from already sign player account
    //签名已存在, 不允许再签名
    result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE player_account = '%u' AND petitionguid = '%u'", GetAccountId(), petitionLowGuid);

    if (result)
    {
        delete result;
        WorldPacket data(SMSG_PETITION_SIGN_RESULTS, (8 + 8 + 4));
        data << ObjectGuid(petitionGuid);
        data << ObjectGuid(_player->GetObjectGuid());
        data << uint32(PETITION_SIGN_ALREADY_SIGNED);

        // close at signer side
        SendPacket(data);

        // update for owner if online
        if (Player* owner = sObjectMgr.GetPlayer(ownerGuid))
            owner->GetSession()->SendPacket(data);
        return;
    }

    //创建一条签名信息
    CharacterDatabase.PExecute("INSERT INTO petition_sign (ownerguid,petitionguid, playerguid, player_account) VALUES ('%u', '%u', '%u','%u')",
                               ownerLowGuid, petitionLowGuid, _player->GetGUIDLow(), GetAccountId());

    DEBUG_LOG("PETITION SIGN: %s by %s", petitionGuid.GetString().c_str(), _player->GetGuidStr().c_str());

    //返回应答报文
    WorldPacket data(SMSG_PETITION_SIGN_RESULTS, (8 + 8 + 4));
    data << ObjectGuid(petitionGuid);
    data << ObjectGuid(_player->GetObjectGuid());
    data << uint32(PETITION_SIGN_OK);

    // close at signer side
    SendPacket(data);

    // update signs count on charter, required testing...
    // Item *item = _player->GetItemByGuid(petitionguid));
    // if(item)
    //    item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT_1_1+1, signs);

    // update for owner if online
    if (Player* owner = sObjectMgr.GetPlayer(ownerGuid))
        owner->GetSession()->SendPacket(data);
}

//拒绝在注册表上签名
void WorldSession::HandlePetitionDeclineOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode MSG_PETITION_DECLINE");  // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;
    recv_data >> petitionGuid;                              // petition guid

    DEBUG_LOG("Petition %s declined by %s", petitionGuid.GetString().c_str(), _player->GetGuidStr().c_str());

    uint32 petitionLowGuid = petitionGuid.GetCounter();

    //获取注册表信息
    QueryResult* result = CharacterDatabase.PQuery("SELECT ownerguid FROM petition WHERE petitionguid = '%u'", petitionLowGuid);
    if (!result)
        return;

    Field* fields = result->Fetch();
    ObjectGuid ownerguid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
    delete result;

    //给注册表所有者发个拒绝的信息
    Player* owner = sObjectMgr.GetPlayer(ownerguid);
    if (owner)                                              // petition owner online
    {
        WorldPacket data(MSG_PETITION_DECLINE, 8);
        data << _player->GetObjectGuid();
        owner->GetSession()->SendPacket(data);
    }
}

//提交注册表
void WorldSession::HandleOfferPetitionOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_OFFER_PETITION");   // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;
    ObjectGuid playerGuid;
    uint32 junk;
    recv_data >> junk;                                      // this is not petition type!
    recv_data >> petitionGuid;                              // petition guid
    recv_data >> playerGuid;                                // player guid

    Player* player = ObjectAccessor::FindPlayer(playerGuid);
    if (!player)
        return;

    /// Get petition type and check
    QueryResult* result = CharacterDatabase.PQuery("SELECT type FROM petition WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    if (!result)
        return;

    Field* fields = result->Fetch();
    uint32 type = fields[0].GetUInt32();
    delete result;

    DEBUG_LOG("OFFER PETITION: type %u petition %s to %s", type, petitionGuid.GetString().c_str(), playerGuid.GetString().c_str());

    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) && GetPlayer()->GetTeam() != player->GetTeam())
    {
        if (type != 9)
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", "", ERR_ARENA_TEAM_NOT_ALLIED);
        else
            SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_NOT_ALLIED);
        return;
    }

    if (type != 9)
    {
        if (player->getLevel() < sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL))
        {
            // player is too low level to join an arena team
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", player->GetName(), ERR_ARENA_TEAM_TARGET_TOO_LOW_S);
            return;
        }

        if (!IsArenaTypeValid(ArenaType(type)))
            return;

        uint8 slot = ArenaTeam::GetSlotByType(ArenaType(type));
        if (slot >= MAX_ARENA_SLOT)
            return;

        if (player->GetArenaTeamId(slot))
        {
            // player is already in an arena team
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, "", player->GetName(), ERR_ALREADY_IN_ARENA_TEAM_S);
            return;
        }

        if (player->GetArenaTeamIdInvited())
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_INVITE_SS, "", _player->GetName(), ERR_ALREADY_INVITED_TO_ARENA_TEAM_S);
            return;
        }
    }
    else
    {
        if (player->GetGuildId())
        {
            SendGuildCommandResult(GUILD_INVITE_S, _player->GetName(), ERR_ALREADY_IN_GUILD_S);
            return;
        }

        if (player->GetGuildIdInvited())
        {
            SendGuildCommandResult(GUILD_INVITE_S, _player->GetName(), ERR_ALREADY_INVITED_TO_GUILD_S);
            return;
        }
    }

    /// Get petition signs count
    uint8 signs = 0;
    result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    // result==nullptr also correct charter without signs
    if (result)
        signs = (uint8)result->GetRowCount();

    /// Send response
    WorldPacket data(SMSG_PETITION_SHOW_SIGNATURES, (8 + 8 + 4 + signs + signs * 12));
    data << ObjectGuid(petitionGuid);                       // petition guid
    data << ObjectGuid(_player->GetObjectGuid());           // owner guid
    data << uint32(petitionGuid.GetCounter());              // guild guid (in mangos always same as low part of petition guid)
    data << uint8(signs);                                   // sign's count

    for (uint8 i = 1; i <= signs; ++i)
    {
        Field* fields2 = result->Fetch();
        ObjectGuid signerGuid = ObjectGuid(HIGHGUID_PLAYER, fields2[0].GetUInt32());

        data << ObjectGuid(signerGuid);                     // Player GUID
        data << uint32(0);                                  // there 0 ...

        result->NextRow();
    }

    delete result;
    player->GetSession()->SendPacket(data);
}

void WorldSession::HandleTurnInPetitionOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received opcode CMSG_TURN_IN_PETITION"); // ok
    // recv_data.hexlike();

    ObjectGuid petitionGuid;

    recv_data >> petitionGuid;

    DEBUG_LOG("Petition %s turned in by %s", petitionGuid.GetString().c_str(), _player->GetGuidStr().c_str());

    /// Collect petition info data
    ObjectGuid ownerGuid;
    uint32 type;
    std::string name;

    // data
    QueryResult* result = CharacterDatabase.PQuery("SELECT ownerguid, name, type FROM petition WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    if (result)
    {
        Field* fields = result->Fetch();
        ownerGuid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
        name = fields[1].GetCppString();
        type = fields[2].GetUInt32();
        delete result;
    }
    else
    {
        sLog.outError("CMSG_TURN_IN_PETITION: petition table not have data for guid %u!", petitionGuid.GetCounter());
        return;
    }

    if (type == 9)
    {
        if (_player->GetGuildId())
        {
            WorldPacket data(SMSG_TURN_IN_PETITION_RESULTS, 4);
            data << uint32(PETITION_TURN_ALREADY_IN_GUILD); // already in guild
            _player->GetSession()->SendPacket(data);
            return;
        }
    }
    else
    {
        if (!IsArenaTypeValid(ArenaType(type)))
            return;

        uint8 slot = ArenaTeam::GetSlotByType(ArenaType(type));
        if (slot >= MAX_ARENA_SLOT)
            return;

        if (_player->GetArenaTeamId(slot))
        {
            // data.Initialize(SMSG_TURN_IN_PETITION_RESULTS, 4);
            // data << (uint32)PETITION_TURN_ALREADY_IN_GUILD;   // already in guild
            //_player->GetSession()->SendPacket(data);
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ALREADY_IN_ARENA_TEAM);
            return;
        }
    }

    if (_player->GetObjectGuid() != ownerGuid)
        return;

    // signs
    result = CharacterDatabase.PQuery("SELECT playerguid FROM petition_sign WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    uint8 signs = result ? (uint8)result->GetRowCount() : 0;

    uint32 count = type == 9 ? sWorld.getConfig(CONFIG_UINT32_MIN_PETITION_SIGNS) : type - 1;
    if (signs < count)
    {
        WorldPacket data(SMSG_TURN_IN_PETITION_RESULTS, 4);
        data << uint32(PETITION_TURN_NEED_MORE_SIGNATURES); // need more signatures...
        SendPacket(data);
        delete result;
        return;
    }

    if (type == 9)
    {
        if (sGuildMgr.GetGuildByName(name))
        {
            SendGuildCommandResult(GUILD_CREATE_S, name, ERR_GUILD_NAME_EXISTS_S);
            delete result;
            return;
        }
    }
    else
    {
        if (sObjectMgr.GetArenaTeamByName(name))
        {
            SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, name, "", ERR_ARENA_TEAM_NAME_EXISTS_S);
            delete result;
            return;
        }
    }

    // and at last charter item check
    Item* item = _player->GetItemByGuid(petitionGuid);
    if (!item)
    {
        delete result;
        return;
    }

    // OK!

    // delete charter item
    _player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);

    if (type == 9)                                          // create guild
    {
        Guild* guild = new Guild;
        if (!guild->Create(_player, name))
        {
            delete guild;
            delete result;
            return;
        }

        // register guild and add guildmaster
        sGuildMgr.AddGuild(guild);

        // Send result to GM
        if (WorldSession* session = _player->GetSession())
            session->SendGuildCommandResult(GUILD_CREATE_S, name, 0);

        // add members
        for (uint8 i = 0; i < signs; ++i)
        {
            Field* fields = result->Fetch();

            ObjectGuid signGuid = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
            if (!signGuid)
                continue;

            guild->AddMember(signGuid, guild->GetLowestRank());

            // Put record into guild log
            guild->LogGuildEvent(GUILD_EVENT_LOG_JOIN_GUILD, signGuid);

            // Send result to online signees
            if (Player* signee = sObjectMgr.GetPlayer(signGuid))
            {
                if (WorldSession* session = signee->GetSession())
                    session->SendGuildCommandResult(GUILD_FOUNDER_S, name, 0);
            }

            result->NextRow();
        }
    }
    else                                                    // or arena team
    {
        ArenaTeam* at = new ArenaTeam;
        if (!at->Create(_player->GetObjectGuid(), ArenaType(type), name))
        {
            sLog.outError("PetitionsHandler: arena team create failed.");
            delete at;
            delete result;
            return;
        }

        uint32 icon, iconcolor, border, bordercolor, backgroud;
        recv_data >> backgroud >> icon >> iconcolor >> border >> bordercolor;

        at->SetEmblem(backgroud, icon, iconcolor, border, bordercolor);

        // register team and add captain
        sObjectMgr.AddArenaTeam(at);
        DEBUG_LOG("PetitonsHandler: arena team added to objmrg");

        // add members
        for (uint8 i = 0; i < signs; ++i)
        {
            Field* fields = result->Fetch();
            ObjectGuid memberGUID = ObjectGuid(HIGHGUID_PLAYER, fields[0].GetUInt32());
            if (!memberGUID)
                continue;

            DEBUG_LOG("PetitionsHandler: adding arena member %s", memberGUID.GetString().c_str());
            at->AddMember(memberGUID);
            result->NextRow();
        }
    }

    delete result;

    CharacterDatabase.BeginTransaction();
    CharacterDatabase.PExecute("DELETE FROM petition WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    CharacterDatabase.PExecute("DELETE FROM petition_sign WHERE petitionguid = '%u'", petitionGuid.GetCounter());
    CharacterDatabase.CommitTransaction();

    // created
    DEBUG_LOG("TURN IN PETITION %s", petitionGuid.GetString().c_str());

    WorldPacket data(SMSG_TURN_IN_PETITION_RESULTS, 4);
    data << uint32(PETITION_TURN_OK);
    SendPacket(data);
}

void WorldSession::HandlePetitionShowListOpcode(WorldPacket& recv_data)
{
    DEBUG_LOG("Received CMSG_PETITION_SHOWLIST");
    // recv_data.hexlike();

    ObjectGuid guid;
    recv_data >> guid;

    SendPetitionShowList(guid);
}

//发送公会/竞技场队伍购买列表
void WorldSession::SendPetitionShowList(ObjectGuid guid) const
{
    Creature* pCreature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_PETITIONER);
    if (!pCreature)
    {
        DEBUG_LOG("WORLD: HandlePetitionShowListOpcode - %s not found or you can't interact with him.", guid.GetString().c_str());
        return;
    }

    uint8 count;
    if (pCreature->isTabardDesigner())  //公会注册管理员
        count = 1;
    else                                //竞技场队伍管理员
        count = 3;

    WorldPacket data(SMSG_PETITION_SHOWLIST, 8 + 1 + 4 * 6);
    data << ObjectGuid(guid);                               // npc guid
    data << uint8(count);                                   // count
    if (count == 1)                                         //公会注册表信息
    {
        data << uint32(1);                                  // index
        data << uint32(GUILD_CHARTER);                      // charter entry
        data << uint32(CHARTER_DISPLAY_ID);                 // charter display id
        data << uint32(GUILD_CHARTER_COST);                 // charter cost
        data << uint32(0);                                  // unknown
        data << uint32(9);                                  // required signs?
    }
    else    //竞技场注册表信息
    {
        // 2v2
        data << uint32(1);                                  // index
        data << uint32(ARENA_TEAM_CHARTER_2v2);             // charter entry
        data << uint32(CHARTER_DISPLAY_ID);                 // charter display id
        data << uint32(ARENA_TEAM_CHARTER_2v2_COST);        // charter cost
        data << uint32(2);                                  // unknown
        data << uint32(2);                                  // required signs?
        // 3v3
        data << uint32(2);                                  // index
        data << uint32(ARENA_TEAM_CHARTER_3v3);             // charter entry
        data << uint32(CHARTER_DISPLAY_ID);                 // charter display id
        data << uint32(ARENA_TEAM_CHARTER_3v3_COST);        // charter cost
        data << uint32(3);                                  // unknown
        data << uint32(3);                                  // required signs?
        // 5v5
        data << uint32(3);                                  // index
        data << uint32(ARENA_TEAM_CHARTER_5v5);             // charter entry
        data << uint32(CHARTER_DISPLAY_ID);                 // charter display id
        data << uint32(ARENA_TEAM_CHARTER_5v5_COST);        // charter cost
        data << uint32(5);                                  // unknown
        data << uint32(5);                                  // required signs?
    }
    // for(uint8 i = 0; i < count; ++i)
    //{
    //    data << uint32(i);                        // index
    //    data << uint32(GUILD_CHARTER);            // charter entry
    //    data << uint32(CHARTER_DISPLAY_ID);       // charter display id
    //    data << uint32(GUILD_CHARTER_COST+i);     // charter cost
    //    data << uint32(0);                        // unknown
    //    data << uint32(9);                        // required signs?
    //}
    SendPacket(data);
    DEBUG_LOG("Sent SMSG_PETITION_SHOWLIST");
}
