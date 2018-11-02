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
#include "WorldPacket.h"
#include "Server/WorldSession.h"
#include "World/World.h"
#include "Globals/ObjectMgr.h"
#include "Log.h"
#include "Server/Opcodes.h"
#include "Guilds/Guild.h"
#include "Guilds/GuildMgr.h"
#include "Social/SocialMgr.h"

//查询公会信息
void WorldSession::HandleGuildQueryOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_QUERY");

    uint32 guildId;
    recvPacket >> guildId;

    //查询公会信息, 查到则返回公会信息
    if (Guild* guild = sGuildMgr.GetGuildById(guildId))
    {
        guild->Query(this);
        return;
    }

    //没有查到公会信息, 发送结果:玩家不在公会中
    SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
}

//工会创建
void WorldSession::HandleGuildCreateOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_CREATE");

    std::string gname;
    recvPacket >> gname;    //工会名称

    //如果玩家已在工会中, 不允许创建
    if (GetPlayer()->GetGuildId())                          // already in guild
        return;

    //新建一个工会, 当前玩家为工会会长, 数据库中创建工会记录
    Guild* guild = new Guild;
    if (!guild->Create(GetPlayer(), gname))
    {
        delete guild;
        return;
    }

    //添加到工会缓存
    sGuildMgr.AddGuild(guild);
}

//工会邀请
void WorldSession::HandleGuildInviteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_INVITE");

    std::string Invitedname, plname;
    Player* player = nullptr;

    recvPacket >> Invitedname;  //被邀请玩家姓名

    //检查姓名格式
    if (normalizePlayerName(Invitedname))
        player = ObjectAccessor::FindPlayerByName(Invitedname.c_str());

    //玩家不存在
    if (!player)
    {
        SendGuildCommandResult(GUILD_INVITE_S, Invitedname, ERR_GUILD_PLAYER_NOT_FOUND_S);
        return;
    }

    //获取当前玩家所在工会
    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    // OK result but not send invite
    //被玩家忽略, 不发送邀请
    if (player->GetSocial()->HasIgnore(GetPlayer()->GetObjectGuid()))
        return;

    // not let enemies sign guild charter
    //没有开配置的情况下, 不允许邀请对立阵营的玩家进工会
    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) && player->GetTeam() != GetPlayer()->GetTeam())
    {
        SendGuildCommandResult(GUILD_INVITE_S, Invitedname, ERR_GUILD_NOT_ALLIED);
        return;
    }

    //如果玩家已存在工会， 则返回失败
    if (player->GetGuildId())
    {
        plname = player->GetName();
        SendGuildCommandResult(GUILD_INVITE_S, plname, ERR_ALREADY_IN_GUILD_S);
        return;
    }

    //已收到工会邀请, 返回
    if (player->GetGuildIdInvited())
    {
        plname = player->GetName();
        SendGuildCommandResult(GUILD_INVITE_S, plname, ERR_ALREADY_INVITED_TO_GUILD_S);
        return;
    }

    //如果当前玩家没有邀请权限, 折返回失败 
    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_INVITE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    DEBUG_LOG("Player %s Invited %s to Join his Guild", GetPlayer()->GetName(), Invitedname.c_str());

    //邀请玩家
    player->SetGuildIdInvited(GetPlayer()->GetGuildId());
    // Put record into guildlog
    guild->LogGuildEvent(GUILD_EVENT_LOG_INVITE_PLAYER, GetPlayer()->GetObjectGuid(), player->GetObjectGuid());

    //返回
    WorldPacket data(SMSG_GUILD_INVITE, (8 + 10));          // guess size
    data << GetPlayer()->GetName();
    data << guild->GetName();
    player->GetSession()->SendPacket(data);

    DEBUG_LOG("WORLD: Sent (SMSG_GUILD_INVITE)");
}

//工会移除玩家
void WorldSession::HandleGuildRemoveOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_REMOVE");

    std::string plName;
    recvPacket >> plName;   //被移除玩家姓名

    //姓名格式检查
    if (!normalizePlayerName(plName))
        return;

    //获取当前玩家所在工会
    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    //当前玩家是否有移除权限
    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_REMOVE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    //根据被移除玩家姓名获取其成员信息
    MemberSlot* slot = guild->GetMemberSlot(plName);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    //管理员不允许被移除
    if (slot->RankId == GR_GUILDMASTER)
    {
        SendGuildCommandResult(GUILD_QUIT_S, "", ERR_GUILD_LEADER_LEAVE);
        return;
    }

    // do not allow to kick player with same or higher rights
    //只能移除比自己级别低的玩家
    if (GetPlayer()->GetRank() >= slot->RankId)
    {
        SendGuildCommandResult(GUILD_QUIT_S, plName, ERR_GUILD_RANK_TOO_HIGH_S);
        return;
    }

    // possible last member removed, do cleanup, and no need events
    //工会移除玩家
    if (guild->DelMember(slot->guid))
    {
        guild->Disband();
        delete guild;
        return;
    }

    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_UNINVITE_PLAYER, GetPlayer()->GetObjectGuid(), slot->guid);

    guild->BroadcastEvent(GE_REMOVED, plName.c_str(), _player->GetName());
}

//接受工会邀请
void WorldSession::HandleGuildAcceptOpcode(WorldPacket& /*recvPacket*/)
{
    Guild* guild;
    Player* player = GetPlayer();   //获取当前玩家信息

    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_ACCEPT");

    //获取玩家的工会邀请
    guild = sGuildMgr.GetGuildById(player->GetGuildIdInvited());
    if (!guild || player->GetGuildId()) 
        return;

    // not let enemies sign guild charter
    //不允许接受对立阵营的工会邀请
    if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD) && player->GetTeam() != sObjectMgr.GetPlayerTeamByGUID(guild->GetLeaderGuid()))
        return;

    //工会添加成员
    if (!guild->AddMember(GetPlayer()->GetObjectGuid(), guild->GetLowestRank()))
        return;
    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_JOIN_GUILD, GetPlayer()->GetObjectGuid());

    guild->BroadcastEvent(GE_JOINED, player->GetObjectGuid(), player->GetName());
}

void WorldSession::HandleGuildDeclineOpcode(WorldPacket& /*recvPacket*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_DECLINE");

    //清除工会邀请
    GetPlayer()->SetGuildIdInvited(0);
    GetPlayer()->SetInGuild(0);
}

//工会信息查询
void WorldSession::HandleGuildInfoOpcode(WorldPacket& /*recvPacket*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_INFO");

    //获取玩家所在工会
    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    //返回应答
    WorldPacket data(SMSG_GUILD_INFO, (5 * 4 + guild->GetName().size() + 1));
    data << guild->GetName();
    data << uint32(guild->GetCreatedDay());     //创建日期
    data << uint32(guild->GetCreatedMonth());   //创建月份
    data << uint32(guild->GetCreatedYear());    //创建年份
    data << uint32(guild->GetMemberSize());     //成员数量              // amount of chars
    data << uint32(guild->GetAccountsNumber()); //账号数量             // amount of accounts
    SendPacket(data);
}

//工会名单
void WorldSession::HandleGuildRosterOpcode(WorldPacket& /*recvPacket*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_ROSTER");

    if (Guild* guild = sGuildMgr.GetGuildById(_player->GetGuildId()))
        guild->Roster(this);    //遍历工会所有成员信息, 把相关信息返回
}

//提升玩家工会级别
void WorldSession::HandleGuildPromoteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_PROMOTE");

    std::string plName;
    recvPacket >> plName;   //玩家姓名

    //检查姓名格式
    if (!normalizePlayerName(plName))
        return;

    //获取当前玩家所在工会
    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    //判断当前玩家是否有提升级别的权利
    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_PROMOTE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    //根据姓名获取被提升玩家成员信息
    MemberSlot* slot = guild->GetMemberSlot(plName);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    //不允许给自己提级别
    if (slot->guid == GetPlayer()->GetObjectGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_NAME_INVALID);
        return;
    }

    // allow to promote only to lower rank than member's rank
    // guildmaster's rank = 0
    // GetPlayer()->GetRank() + 1 is highest rank that current player can promote to
    //提升权限的玩家必须比被提升玩家至少高1级
    if (GetPlayer()->GetRank() + 1 >= slot->RankId)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_RANK_TOO_HIGH_S);
        return;
    }

    uint32 newRankId = slot->RankId - 1;                    // when promoting player, rank is decreased

    //修改成员权限, 权限值越小， 权限越大
    slot->ChangeRank(newRankId);
    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_PROMOTE_PLAYER, GetPlayer()->GetObjectGuid(), slot->guid, newRankId);

    guild->BroadcastEvent(GE_PROMOTION, _player->GetName(), plName.c_str(), guild->GetRankName(newRankId).c_str());
}

//降低工会玩家级别
void WorldSession::HandleGuildDemoteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_DEMOTE");

    std::string plName;
    recvPacket >> plName;   //被降低级别玩家姓名

    //检查姓名格式
    if (!normalizePlayerName(plName))
        return;

    //获取玩家所在工会
    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());

    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    //判断玩家是否有权限降低级别
    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_DEMOTE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    //根据玩家姓名获取成员信息
    MemberSlot* slot = guild->GetMemberSlot(plName);

    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    //不允许给自己降级
    if (slot->guid == GetPlayer()->GetObjectGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_NAME_INVALID);
        return;
    }

    // do not allow to demote same or higher rank
    //不允许降低比自己权限高/相同的玩家级别
    if (GetPlayer()->GetRank() >= slot->RankId)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_RANK_TOO_HIGH_S);
        return;
    }

    // do not allow to demote lowest rank
    //不允许降低到工会最低级别以下
    if (slot->RankId >= guild->GetLowestRank())
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_RANK_TOO_LOW_S);
        return;
    }

    uint32 newRankId = slot->RankId + 1;                    // when demoting player, rank is increased

    //降低玩家级别
    slot->ChangeRank(newRankId);
    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_DEMOTE_PLAYER, GetPlayer()->GetObjectGuid(), slot->guid, newRankId);

    guild->BroadcastEvent(GE_DEMOTION, _player->GetName(), plName.c_str(), guild->GetRankName(slot->RankId).c_str());
}

//离开工会
void WorldSession::HandleGuildLeaveOpcode(WorldPacket& /*recvPacket*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_LEAVE");

    //获取玩家所在工会
    Guild* guild = sGuildMgr.GetGuildById(_player->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    //工会还有其他玩家时, 工会会长不允许退出工会
    if (_player->GetObjectGuid() == guild->GetLeaderGuid() && guild->GetMemberSize() > 1)
    {
        SendGuildCommandResult(GUILD_QUIT_S, "", ERR_GUILD_LEADER_LEAVE);
        return;
    }

    //工会只有会长一时时，推出工会就解散工会
    if (_player->GetObjectGuid() == guild->GetLeaderGuid())
    {
        guild->Disband();
        delete guild;
        return;
    }

    SendGuildCommandResult(GUILD_QUIT_S, guild->GetName(), ERR_PLAYER_NO_MORE_IN_GUILD);

    //工会删除玩家
    if (guild->DelMember(_player->GetObjectGuid()))
    {
        guild->Disband();
        delete guild;
        return;
    }

    // Put record into guild log
    guild->LogGuildEvent(GUILD_EVENT_LOG_LEAVE_GUILD, _player->GetObjectGuid());

    guild->BroadcastEvent(GE_LEFT, _player->GetObjectGuid(), _player->GetName());
}

//解散工会
void WorldSession::HandleGuildDisbandOpcode(WorldPacket& /*recvPacket*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_DISBAND");

    //获取当前玩家所在工会
    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    //不时会长不允许解散工会
    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    //解散工会, 实际删除工会相关的一些表， 清理缓存中工会相关的信息
    guild->Disband();
    delete guild;

    DEBUG_LOG("WORLD: Guild Successfully Disbanded");
}

//工会更换会长
void WorldSession::HandleGuildLeaderOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_LEADER");

    std::string name;
    recvPacket >> name; //新会长姓名

    //获取当前玩家[旧会长]
    Player* oldLeader = GetPlayer();

    //检查新会长姓名
    if (!normalizePlayerName(name))
        return;

    //获取旧会长所在工会
    Guild* guild = sGuildMgr.GetGuildById(oldLeader->GetGuildId());

    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    //如果当前玩家不时旧会长[冒充的], 不允许设置新会长
    if (oldLeader->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    //获取旧会长成员信息
    MemberSlot* oldSlot = guild->GetMemberSlot(oldLeader->GetObjectGuid());
    if (!oldSlot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    //获取新会长成员信息
    MemberSlot* slot = guild->GetMemberSlot(name);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, name, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    //设置新的工会会长
    guild->SetLeader(slot->guid);
    oldSlot->ChangeRank(GR_OFFICER);    //将旧会长的级别设置为管理员

    guild->BroadcastEvent(GE_LEADER_CHANGED, oldLeader->GetName(), name.c_str());
}

//设置公会公告
void WorldSession::HandleGuildMOTDOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_MOTD");

    std::string MOTD;

    //公会公告
    if (!recvPacket.empty())
        recvPacket >> MOTD;
    else
        MOTD.clear();   //没有则置为空

    //获取玩家所在公会
    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    //检查玩家是否有权限修改公告
    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_SETMOTD))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    //设置公会公告
    guild->SetMOTD(MOTD);

    //向所有公会成员广播一下公会公告的修改
    guild->BroadcastEvent(GE_MOTD, MOTD.c_str());
}

void WorldSession::HandleGuildSetPublicNoteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_SET_PUBLIC_NOTE");

    std::string name, PNOTE;
    recvPacket >> name;

    if (!normalizePlayerName(name))
        return;

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());

    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_EPNOTE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    MemberSlot* slot = guild->GetMemberSlot(name);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, name, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    recvPacket >> PNOTE;

    slot->SetPNOTE(PNOTE);

    guild->Roster(this);
}

void WorldSession::HandleGuildSetOfficerNoteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_SET_OFFICER_NOTE");

    std::string plName, OFFNOTE;
    recvPacket >> plName;

    if (!normalizePlayerName(plName))
        return;

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());

    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }
    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_EOFFNOTE))
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    MemberSlot* slot = guild->GetMemberSlot(plName);
    if (!slot)
    {
        SendGuildCommandResult(GUILD_INVITE_S, plName, ERR_GUILD_PLAYER_NOT_IN_GUILD_S);
        return;
    }

    recvPacket >> OFFNOTE;

    slot->SetOFFNOTE(OFFNOTE);

    guild->Roster(this);
}

void WorldSession::HandleGuildRankOpcode(WorldPacket& recvPacket)
{
    std::string rankname;
    uint32 rankId;
    uint32 rights, MoneyPerDay;

    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_RANK");

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        recvPacket.rpos(recvPacket.wpos());                 // set to end to avoid warnings spam
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        recvPacket.rpos(recvPacket.wpos());                 // set to end to avoid warnings spam
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    recvPacket >> rankId;
    recvPacket >> rights;
    recvPacket >> rankname;
    recvPacket >> MoneyPerDay;

    for (int i = 0; i < GUILD_BANK_MAX_TABS; ++i)
    {
        uint32 BankRights;
        uint32 BankSlotPerDay;

        recvPacket >> BankRights;
        recvPacket >> BankSlotPerDay;
        guild->SetBankRightsAndSlots(rankId, uint8(i), uint16(BankRights & 0xFF), uint16(BankSlotPerDay), true);
    }

    DEBUG_LOG("WORLD: Changed RankName to %s , Rights to 0x%.4X", rankname.c_str(), rights);

    guild->SetBankMoneyPerDay(rankId, MoneyPerDay);
    guild->SetRankName(rankId, rankname);

    if (rankId == GR_GUILDMASTER)                           // prevent loss leader rights
        rights = GR_RIGHT_ALL;

    guild->SetRankRights(rankId, rights);

    guild->Query(this);
    guild->Roster();                                        // broadcast for tab rights update
}

void WorldSession::HandleGuildAddRankOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_ADD_RANK");

    std::string rankname;
    recvPacket >> rankname;

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    if (guild->GetRanksSize() >= GUILD_RANKS_MAX_COUNT)     // client not let create more 10 than ranks
        return;

    guild->CreateRank(rankname, GR_RIGHT_GCHATLISTEN | GR_RIGHT_GCHATSPEAK);

    guild->Query(this);
    guild->Roster();                                        // broadcast for tab rights update
}

void WorldSession::HandleGuildDelRankOpcode(WorldPacket& /*recvPacket*/)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_DEL_RANK");

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (GetPlayer()->GetObjectGuid() != guild->GetLeaderGuid())
    {
        SendGuildCommandResult(GUILD_INVITE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    guild->DelRank();

    guild->Query(this);
    guild->Roster();                                        // broadcast for tab rights update
}

void WorldSession::SendGuildCommandResult(uint32 typecmd, const std::string& str, uint32 cmdresult) const
{
    WorldPacket data(SMSG_GUILD_COMMAND_RESULT, (8 + str.size() + 1));
    data << typecmd;
    data << str;
    data << cmdresult;
    SendPacket(data);

    DEBUG_LOG("WORLD: Sent (SMSG_GUILD_COMMAND_RESULT)");
}

void WorldSession::HandleGuildChangeInfoTextOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_GUILD_INFO_TEXT");

    std::string GINFO;
    recvPacket >> GINFO;

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
        return;
    }

    if (!guild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_MODIFY_GUILD_INFO))
    {
        SendGuildCommandResult(GUILD_CREATE_S, "", ERR_GUILD_PERMISSIONS);
        return;
    }

    guild->SetGINFO(GINFO);
}

void WorldSession::HandleSaveGuildEmblemOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode MSG_SAVE_GUILD_EMBLEM");

    ObjectGuid vendorGuid;
    uint32 EmblemStyle, EmblemColor, BorderStyle, BorderColor, BackgroundColor;

    recvPacket >> vendorGuid;
    recvPacket >> EmblemStyle >> EmblemColor >> BorderStyle >> BorderColor >> BackgroundColor;

    Creature* pCreature = GetPlayer()->GetNPCIfCanInteractWith(vendorGuid, UNIT_NPC_FLAG_TABARDDESIGNER);
    if (!pCreature)
    {
        //"That's not an emblem vendor!"
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_INVALIDVENDOR);
        DEBUG_LOG("WORLD: HandleSaveGuildEmblemOpcode - %s not found or you can't interact with him.", vendorGuid.GetString().c_str());
        return;
    }

    Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId());
    if (!guild)
    {
        //"You are not part of a guild!";
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_NOGUILD);
        return;
    }

    if (guild->GetLeaderGuid() != GetPlayer()->GetObjectGuid())
    {
        //"Only guild leaders can create emblems."
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_NOTGUILDMASTER);
        return;
    }

    if (GetPlayer()->GetMoney() < 10 * GOLD)
    {
        //"You can't afford to do that."
        SendSaveGuildEmblem(ERR_GUILDEMBLEM_NOTENOUGHMONEY);
        return;
    }

    GetPlayer()->ModifyMoney(-10 * GOLD);
    guild->SetEmblem(EmblemStyle, EmblemColor, BorderStyle, BorderColor, BackgroundColor);

    //"Guild Emblem saved."
    SendSaveGuildEmblem(ERR_GUILDEMBLEM_SUCCESS);

    guild->Query(this);
}

void WorldSession::HandleGuildEventLogQueryOpcode(WorldPacket& /* recvPacket */)
{
    // empty
    DEBUG_LOG("WORLD: Received (MSG_GUILD_EVENT_LOG_QUERY)");

    if (uint32 GuildId = GetPlayer()->GetGuildId())
        if (Guild* pGuild = sGuildMgr.GetGuildById(GuildId))
            pGuild->DisplayGuildEventLog(this);
}

/******  GUILD BANK  *******/

void WorldSession::HandleGuildBankMoneyWithdrawn(WorldPacket& /* recv_data */)
{
    DEBUG_LOG("WORLD: Received (MSG_GUILD_BANK_MONEY_WITHDRAWN)");

    if (uint32 GuildId = GetPlayer()->GetGuildId())
        if (Guild* pGuild = sGuildMgr.GetGuildById(GuildId))
            pGuild->SendMoneyInfo(this, GetPlayer()->GetGUIDLow());
}

void WorldSession::HandleGuildPermissions(WorldPacket& /* recv_data */)
{
    DEBUG_LOG("WORLD: Received (MSG_GUILD_PERMISSIONS)");

    if (uint32 GuildId = GetPlayer()->GetGuildId())
    {
        if (Guild* pGuild = sGuildMgr.GetGuildById(GuildId))
        {
            uint32 rankId = GetPlayer()->GetRank();

            WorldPacket data(MSG_GUILD_PERMISSIONS, 4 * 15 + 1);
            data << uint32(rankId);                         // guild rank id
            data << uint32(pGuild->GetRankRights(rankId));  // rank rights
            // money per day left
            data << uint32(pGuild->GetMemberMoneyWithdrawRem(GetPlayer()->GetGUIDLow()));
            data << uint8(pGuild->GetPurchasedTabs());      // tabs count
            // why sending all info when not all tabs are purchased???
            for (int i = 0; i < GUILD_BANK_MAX_TABS; ++i)
            {
                data << uint32(pGuild->GetBankRights(rankId, uint8(i)));
                data << uint32(pGuild->GetMemberSlotWithdrawRem(GetPlayer()->GetGUIDLow(), uint8(i)));
            }
            SendPacket(data);
            DEBUG_LOG("WORLD: Sent (MSG_GUILD_PERMISSIONS)");
        }
    }
}

/* Called when clicking on Guild bank gameobject */
void WorldSession::HandleGuildBankerActivate(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANKER_ACTIVATE)");

    ObjectGuid goGuid;
    uint8  unk;
    recv_data >> goGuid >> unk;

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return;

    if (uint32 GuildId = GetPlayer()->GetGuildId())
    {
        if (Guild* pGuild = sGuildMgr.GetGuildById(GuildId))
        {
            pGuild->DisplayGuildBankTabsInfo(this);         // this also will load guild bank if not yet
            return;
        }
    }

    SendGuildCommandResult(GUILD_UNK1, "", ERR_GUILD_PLAYER_NOT_IN_GUILD);
}

/* Called when opening guild bank tab only (first one) */
void WorldSession::HandleGuildBankQueryTab(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_QUERY_TAB)");

    ObjectGuid goGuid;
    uint8 TabId, unk1;
    recv_data >> goGuid >> TabId >> unk1;

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
        return;

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
        return;

    if (TabId >= pGuild->GetPurchasedTabs())
        return;

    // Let's update the amount of gold the player can withdraw before displaying the content
    // This is useful if money withdraw right has changed
    pGuild->SendMoneyInfo(this, GetPlayer()->GetGUIDLow());
    pGuild->DisplayGuildBankContent(this, TabId);
}

void WorldSession::HandleGuildBankDepositMoney(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_DEPOSIT_MONEY)");

    ObjectGuid goGuid;
    uint32 money;
    recv_data >> goGuid >> money;

    if (!money)
        return;

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return;

    if (GetPlayer()->GetMoney() < money)
        return;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
        return;

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
        return;

    if (!pGuild->GetPurchasedTabs())
        return;

    CharacterDatabase.BeginTransaction();

    pGuild->SetBankMoney(pGuild->GetGuildBankMoney() + money);
    GetPlayer()->ModifyMoney(-int(money));
    GetPlayer()->SaveGoldToDB();

    CharacterDatabase.CommitTransaction();

    // logging money
    if (_player->GetSession()->GetSecurity() > SEC_PLAYER && sWorld.getConfig(CONFIG_BOOL_GM_LOG_TRADE))
    {
        sLog.outCommand(_player->GetSession()->GetAccountId(), "GM %s (Account: %u) deposit money (Amount: %u) to guild bank (Guild ID %u)",
                        _player->GetName(), _player->GetSession()->GetAccountId(), money, GuildId);
    }

    // log
    pGuild->LogBankEvent(GUILD_BANK_LOG_DEPOSIT_MONEY, uint8(0), GetPlayer()->GetGUIDLow(), money);

    pGuild->DisplayGuildBankTabsInfo(this);
    pGuild->DisplayGuildBankContent(this, 0);
    pGuild->DisplayGuildBankMoneyUpdate(this);
}

void WorldSession::HandleGuildBankWithdrawMoney(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_WITHDRAW_MONEY)");

    ObjectGuid goGuid;
    uint32 money;
    recv_data >> goGuid >> money;

    if (!money)
        return;

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (GuildId == 0)
        return;

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
        return;

    if (!pGuild->GetPurchasedTabs())
        return;

    if (pGuild->GetGuildBankMoney() < money)                // not enough money in bank
        return;

    if (!pGuild->HasRankRight(GetPlayer()->GetRank(), GR_RIGHT_WITHDRAW_GOLD))
        return;

    CharacterDatabase.BeginTransaction();

    if (!pGuild->MemberMoneyWithdraw(money, GetPlayer()->GetGUIDLow()))
    {
        CharacterDatabase.RollbackTransaction();
        return;
    }

    GetPlayer()->ModifyMoney(money);
    GetPlayer()->SaveGoldToDB();

    CharacterDatabase.CommitTransaction();

    // Log
    pGuild->LogBankEvent(GUILD_BANK_LOG_WITHDRAW_MONEY, uint8(0), GetPlayer()->GetGUIDLow(), money);

    pGuild->SendMoneyInfo(this, GetPlayer()->GetGUIDLow());
    pGuild->DisplayGuildBankTabsInfo(this);
    pGuild->DisplayGuildBankContent(this, 0);
    pGuild->DisplayGuildBankMoneyUpdate(this);
}

void WorldSession::HandleGuildBankSwapItems(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_SWAP_ITEMS)");

    ObjectGuid goGuid;
    uint8 BankToBank;

    uint8 BankTab, BankTabSlot, AutoStore;
    uint8 PlayerSlot = NULL_SLOT;
    uint8 PlayerBag = NULL_BAG;
    uint8 BankTabDst, BankTabSlotDst, unk2;
    uint8 ToChar = 1;
    uint32 ItemEntry, unk1;
    uint8 AutoStoreCount = 0;
    uint8 SplitedAmount = 0;

    recv_data >> goGuid >> BankToBank;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
    {
        recv_data.rpos(recv_data.wpos());                   // prevent additional spam at rejected packet
        return;
    }

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
    {
        recv_data.rpos(recv_data.wpos());                   // prevent additional spam at rejected packet
        return;
    }

    if (BankToBank)
    {
        recv_data >> BankTabDst;
        recv_data >> BankTabSlotDst;
        recv_data >> unk1;                                  // always 0
        recv_data >> BankTab;
        recv_data >> BankTabSlot;
        recv_data >> ItemEntry;
        recv_data >> unk2;                                  // always 0
        recv_data >> SplitedAmount;

        if (BankTabSlotDst >= GUILD_BANK_MAX_SLOTS ||
                (BankTabDst == BankTab && BankTabSlotDst == BankTabSlot) ||
                BankTab >= pGuild->GetPurchasedTabs() ||
                BankTabDst >= pGuild->GetPurchasedTabs())
        {
            recv_data.rpos(recv_data.wpos());               // prevent additional spam at rejected packet
            return;
        }
    }
    else
    {
        recv_data >> BankTab;
        recv_data >> BankTabSlot;
        recv_data >> ItemEntry;
        recv_data >> AutoStore;
        if (AutoStore)
        {
            recv_data >> AutoStoreCount;
            recv_data.read_skip<uint8>();                   // ToChar (?), always and expected to be 1 (autostore only triggered in guild->ToChar)
            recv_data.read_skip<uint8>();                   // unknown, always 0
        }
        else
        {
            recv_data >> PlayerBag;
            recv_data >> PlayerSlot;
            recv_data >> ToChar;
            recv_data >> SplitedAmount;
        }

        if ((BankTabSlot >= GUILD_BANK_MAX_SLOTS && BankTabSlot != 0xFF) ||
                BankTab >= pGuild->GetPurchasedTabs())
        {
            recv_data.rpos(recv_data.wpos());               // prevent additional spam at rejected packet
            return;
        }
    }

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return;

    // Bank <-> Bank
    if (BankToBank)
    {
        pGuild->SwapItems(_player, BankTab, BankTabSlot, BankTabDst, BankTabSlotDst, SplitedAmount);
        return;
    }

    // Player <-> Bank

    // allow work with inventory only
    if (!Player::IsInventoryPos(PlayerBag, PlayerSlot) && !(PlayerBag == NULL_BAG && PlayerSlot == NULL_SLOT))
    {
        _player->SendEquipError(EQUIP_ERR_NONE, nullptr, nullptr);
        return;
    }

    // BankToChar swap or char to bank remaining
    if (ToChar)                                             // Bank -> Char cases
        pGuild->MoveFromBankToChar(_player, BankTab, BankTabSlot, PlayerBag, PlayerSlot, SplitedAmount);
    else                                                    // Char -> Bank cases
        pGuild->MoveFromCharToBank(_player, PlayerBag, PlayerSlot, BankTab, BankTabSlot, SplitedAmount);
}

void WorldSession::HandleGuildBankBuyTab(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_BUY_TAB)");

    ObjectGuid goGuid;
    uint8 TabId;

    recv_data >> goGuid;
    recv_data >> TabId;

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
        return;

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
        return;

    // m_PurchasedTabs = 0 when buying Tab 0, that is why this check can be made
    if (TabId != pGuild->GetPurchasedTabs())
        return;

    uint32 TabCost = GetGuildBankTabPrice(TabId) * GOLD;
    if (!TabCost)
        return;

    if (GetPlayer()->GetMoney() < TabCost)                  // Should not happen, this is checked by client
        return;

    // Go on with creating tab
    pGuild->CreateNewBankTab();
    GetPlayer()->ModifyMoney(-int(TabCost));
    pGuild->SetBankRightsAndSlots(GetPlayer()->GetRank(), TabId, GUILD_BANK_RIGHT_FULL, WITHDRAW_SLOT_UNLIMITED, true);
    pGuild->Roster();                                       // broadcast for tab rights update
    pGuild->DisplayGuildBankTabsInfo(this);
}

void WorldSession::HandleGuildBankUpdateTab(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (CMSG_GUILD_BANK_UPDATE_TAB)");

    ObjectGuid goGuid;
    uint8 TabId;
    std::string Name;
    std::string IconIndex;

    recv_data >> goGuid;
    recv_data >> TabId;
    recv_data >> Name;
    recv_data >> IconIndex;

    if (Name.empty())
        return;

    if (IconIndex.empty())
        return;

    if (!GetPlayer()->GetGameObjectIfCanInteractWith(goGuid, GAMEOBJECT_TYPE_GUILD_BANK))
        return;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
        return;

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
        return;

    if (TabId >= pGuild->GetPurchasedTabs())
        return;

    pGuild->SetGuildBankTabInfo(TabId, Name, IconIndex);
    pGuild->DisplayGuildBankTabsInfo(this);
    pGuild->DisplayGuildBankContent(this, TabId);
}

void WorldSession::HandleGuildBankLogQuery(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received (MSG_GUILD_BANK_LOG_QUERY)");

    uint8 TabId;
    recv_data >> TabId;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
        return;

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
        return;

    // GUILD_BANK_MAX_TABS send by client for money log
    if (TabId >= pGuild->GetPurchasedTabs() && TabId != GUILD_BANK_MAX_TABS)
        return;

    pGuild->DisplayGuildBankLogs(this, TabId);
}

void WorldSession::HandleQueryGuildBankTabText(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received opcode MSG_QUERY_GUILD_BANK_TEXT");

    uint8 TabId;
    recv_data >> TabId;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
        return;

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
        return;

    if (TabId >= pGuild->GetPurchasedTabs())
        return;

    pGuild->SendGuildBankTabText(this, TabId);
}

void WorldSession::HandleSetGuildBankTabText(WorldPacket& recv_data)
{
    DEBUG_LOG("WORLD: Received opcode CMSG_SET_GUILD_BANK_TEXT");

    uint8 TabId;
    std::string Text;
    recv_data >> TabId;
    recv_data >> Text;

    uint32 GuildId = GetPlayer()->GetGuildId();
    if (!GuildId)
        return;

    Guild* pGuild = sGuildMgr.GetGuildById(GuildId);
    if (!pGuild)
        return;

    if (TabId >= pGuild->GetPurchasedTabs())
        return;

    pGuild->SetGuildBankTabText(TabId, Text);
}

void WorldSession::SendSaveGuildEmblem(uint32 msg) const
{
    WorldPacket data(MSG_SAVE_GUILD_EMBLEM, 4);
    data << uint32(msg);                                    // not part of guild
    SendPacket(data);
}
