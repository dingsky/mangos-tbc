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

#include "Globals/ObjectMgr.h"                                      // for normalizePlayerName
#include "Chat/ChannelMgr.h"

//����Ƶ��
void WorldSession::HandleJoinChannelOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());

    uint32 channel_id;              //Ƶ��ID
    uint8 unknown1, unknown2;
    std::string channelname, pass;  //Ƶ������

    recvPacket >> channel_id >> unknown1 >> unknown2;
    recvPacket >> channelname;

    //Ƶ�����ƿ��ж�
    if (channelname.empty())
        return;

    //��Ҽ���Ƶ��, ���������Ļ�, ��ҪУ��Ƶ������
    recvPacket >> pass;
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetJoinChannel(channelname, channel_id))
            chn->Join(_player, pass.c_str());
}

//�뿪Ƶ��
void WorldSession::HandleLeaveChannelOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();

    uint32 unk;
    std::string channelname;    //Ƶ������
    recvPacket >> unk;                                      // channel id?
    recvPacket >> channelname;

    //Ƶ�����ƿ��ж�
    if (channelname.empty())
        return;

    //�뿪Ƶ��
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
    {
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->Leave(_player, true);
        cMgr->LeftChannel(channelname);
    }
}

//��ȡƵ������б�
void WorldSession::HandleChannelListOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname;
    recvPacket >> channelname;  //Ƶ������

    //����Ƶ����Ա, �ѽ�����͸���������
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->List(_player);
}

//����Ƶ������
void WorldSession::HandleChannelPasswordOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname, pass;
    recvPacket >> channelname;  //Ƶ������

    recvPacket >> pass;         //Ƶ������

    //�޸�Ƶ������
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->Password(_player, pass.c_str());
}

//����Ƶ���µ�������
void WorldSession::HandleChannelSetOwnerOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();

    std::string channelname, newp;
    recvPacket >> channelname;  //Ƶ������

    recvPacket >> newp; //�µ�������

    //�µ�����������
    if (!normalizePlayerName(newp))
        return;

    //��ǰ�����Ҫ��Ȩ�޲�������Ƶ��������, GM��֮ǰ��Ƶ������Ա������
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->SetOwner(_player, newp.c_str());
}

//��ȡƵ��������
void WorldSession::HandleChannelOwnerOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname;
    recvPacket >> channelname;  //Ƶ������

    //��Ƶ�������߷��͸����
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->SendWhoOwner(_player);
}

//����Ƶ��������
void WorldSession::HandleChannelModeratorOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname, otp;
    recvPacket >> channelname;

    recvPacket >> otp;

    if (!normalizePlayerName(otp))
        return;

    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->SetModerator(_player, otp.c_str());
}

//ȡ��Ƶ��������
void WorldSession::HandleChannelUnmoderatorOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname, otp;
    recvPacket >> channelname;

    recvPacket >> otp;

    if (!normalizePlayerName(otp))
        return;

    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->UnsetModerator(_player, otp.c_str());
}

//��Ƶ���е�ĳ����ҽ���
void WorldSession::HandleChannelMuteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname, otp;   
    recvPacket >> channelname;      //Ƶ������

    recvPacket >> otp;              //�����Ե����

    if (!normalizePlayerName(otp))
        return;

    //���������Ϊ��ֹ����
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->SetMute(_player, otp.c_str());
}

//���ȡ������
void WorldSession::HandleChannelUnmuteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();

    std::string channelname, otp;
    recvPacket >> channelname;

    recvPacket >> otp;

    if (!normalizePlayerName(otp))
        return;

    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->UnsetMute(_player, otp.c_str());
}

//������Ҽ���Ƶ��
void WorldSession::HandleChannelInviteOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname, otp;   
    recvPacket >> channelname;      //Ƶ������

    recvPacket >> otp;              //���������

    if (!normalizePlayerName(otp))
        return;

    //��������֪ͨ�����
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->Invite(_player, otp.c_str());
}

//������Ƴ�Ƶ��
void WorldSession::HandleChannelKickOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname, otp;
    recvPacket >> channelname;

    recvPacket >> otp;
    if (!normalizePlayerName(otp))
        return;

    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->Kick(_player, otp.c_str());
}

//������Ƴ�Ƶ��, ͬʱ��ֹ��ҽ���Ƶ��
void WorldSession::HandleChannelBanOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname, otp;
    recvPacket >> channelname;

    recvPacket >> otp;

    if (!normalizePlayerName(otp))
        return;

    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->Ban(_player, otp.c_str());
}

//������ҽ���Ƶ��
void WorldSession::HandleChannelUnbanOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();

    std::string channelname, otp;
    recvPacket >> channelname;

    recvPacket >> otp;

    if (!normalizePlayerName(otp))
        return;

    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->UnBan(_player, otp.c_str());
}

//Ƶ��ͨ��
void WorldSession::HandleChannelAnnouncementsOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname;
    recvPacket >> channelname;
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->Announce(_player);
}

void WorldSession::HandleChannelModerateOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname;
    recvPacket >> channelname;
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->Moderate(_player);
}

void WorldSession::HandleChannelDisplayListQueryOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname;
    recvPacket >> channelname;
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
            chn->List(_player);
}

void WorldSession::HandleGetChannelMemberCountOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname;
    recvPacket >> channelname;
    if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
    {
        if (Channel* chn = cMgr->GetChannel(channelname, _player))
        {
            WorldPacket data(SMSG_CHANNEL_MEMBER_COUNT, chn->GetName().size() + 1 + 1 + 4);
            data << chn->GetName();
            data << uint8(chn->GetFlags());
            data << uint32(chn->GetNumPlayers());
            SendPacket(data);
        }
    }
}

void WorldSession::HandleSetChannelWatchOpcode(WorldPacket& recvPacket)
{
    DEBUG_LOG("WORLD: Received opcode %s (%u, 0x%X)", recvPacket.GetOpcodeName(), recvPacket.GetOpcode(), recvPacket.GetOpcode());
    // recvPacket.hexlike();
    std::string channelname;
    recvPacket >> channelname;
    /*if(ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
        if(Channel *chn = cMgr->GetChannel(channelname, _player))
            chn->JoinNotify(_player->GetGUID());*/
}
