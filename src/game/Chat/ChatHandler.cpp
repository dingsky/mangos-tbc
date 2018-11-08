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
#include "Log.h"
#include "WorldPacket.h"
#include "Server/WorldSession.h"
#include "World/World.h"
#include "Server/Opcodes.h"
#include "Globals/ObjectMgr.h"
#include "Chat/Chat.h"
#include "Chat/ChannelMgr.h"
#include "Groups/Group.h"
#include "Guilds/Guild.h"
#include "Guilds/GuildMgr.h"
#include "Entities/Player.h"
#include "Spells/SpellAuras.h"
#include "Tools/Language.h"
#include "Util.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"

bool WorldSession::processChatmessageFurtherAfterSecurityChecks(std::string& msg, uint32 lang)
{
    if (lang != LANG_ADDON)
    {
        // strip invisible characters for non-addon messages
        if (sWorld.getConfig(CONFIG_BOOL_CHAT_FAKE_MESSAGE_PREVENTING))
            stripLineInvisibleChars(msg);

        if (sWorld.getConfig(CONFIG_UINT32_CHAT_STRICT_LINK_CHECKING_SEVERITY) && GetSecurity() < SEC_MODERATOR
                && !ChatHandler(this).isValidChatMessage(msg.c_str()))
        {
            sLog.outError("Player %s (GUID: %u) sent a chatmessage with an invalid link: %s", GetPlayer()->GetName(),
                          GetPlayer()->GetGUIDLow(), msg.c_str());
            if (sWorld.getConfig(CONFIG_UINT32_CHAT_STRICT_LINK_CHECKING_KICK))
                KickPlayer();
            return false;
        }
    }

    return true;
}

//消息聊天
void WorldSession::HandleMessagechatOpcode(WorldPacket& recv_data)
{
    uint32 type;
    uint32 lang;

    recv_data >> type;  //消息类型
    recv_data >> lang;  //消息语言ID

    //检查消息类型
    if (type >= MAX_CHAT_MSG_TYPE)
    {
        sLog.outError("CHAT: Wrong message type received: %u", type);
        return;
    }

    DEBUG_LOG("CHAT: packet received. type %u, lang %u", type, lang);

    // prevent talking at unknown language (cheating)
    //根据语言ID获取语言描述
    LanguageDesc const* langDesc = GetLanguageDescByID(lang);
    if (!langDesc)
    {
        SendNotification(LANG_UNKNOWN_LANGUAGE);
        return;
    }

    //如果玩家能够识别该语言, 兽人语啥的?
    if (langDesc->skill_id != 0 && !_player->HasSkill(langDesc->skill_id))
    {
        // also check SPELL_AURA_COMPREHEND_LANGUAGE (client offers option to speak in that language)
        //获取玩家的语言列表, 检查该语言是否在列表里
        Unit::AuraList const& langAuras = _player->GetAurasByType(SPELL_AURA_COMPREHEND_LANGUAGE);
        bool foundAura = false;
        for (Unit::AuraList::const_iterator i = langAuras.begin(); i != langAuras.end(); ++i)
        {
            if ((*i)->GetModifier()->m_miscvalue == int32(lang))
            {
                foundAura = true;
                break;
            }
        }
        if (!foundAura) //如果不在, 返回错误
        {
            SendNotification(LANG_NOT_LEARNED_LANGUAGE);
            return;
        }
    }

    if (lang == LANG_ADDON)
    {
        // Disabled addon channel?
        if (!sWorld.getConfig(CONFIG_BOOL_ADDON_CHANNEL))
            return;
    }
    // LANG_ADDON should not be changed nor be affected by flood control
    else
    {
        // send in universal language if player in .gmon mode (ignore spell effects)
        if (_player->isGameMaster())    //如果玩家是GM, 则语言是大家都能识别的
            lang = LANG_UNIVERSAL;
        else
        {
            // send in universal language in two side iteration allowed mode
            //如果配置了允许对立阵营对话, 那么语言也是大家都能识别的
            if (sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_CHAT))
                lang = LANG_UNIVERSAL;
            else
            {
                switch (type)   //判断语言类型, 团队/公会聊天类型处理
                {
                    case CHAT_MSG_PARTY:
                    case CHAT_MSG_RAID:
                    case CHAT_MSG_RAID_LEADER:
                    case CHAT_MSG_RAID_WARNING:
                        // allow two side chat at group channel if two side group allowed
                        if (sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GROUP))
                            lang = LANG_UNIVERSAL;
                        break;
                    case CHAT_MSG_GUILD:
                    case CHAT_MSG_OFFICER:
                        // allow two side chat at guild channel if two side guild allowed
                        if (sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD))
                            lang = LANG_UNIVERSAL;
                        break;
                }
            }

            // but overwrite it by SPELL_AURA_MOD_LANGUAGE auras (only single case used)
            Unit::AuraList const& ModLangAuras = _player->GetAurasByType(SPELL_AURA_MOD_LANGUAGE);
            if (!ModLangAuras.empty())
                lang = ModLangAuras.front()->GetModifier()->m_miscvalue;
        }

        if (type != CHAT_MSG_AFK && type != CHAT_MSG_DND)
        {
            if (!_player->CanSpeak())
            {
                std::string timeStr = secsToTimeString(m_muteTime - time(nullptr));
                SendNotification(GetMangosString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
                return;
            }

            GetPlayer()->UpdateSpeakTime();
        }
    }

    //判断消息类型
    switch (type)
    {
        case CHAT_MSG_SAY:          //说话
        case CHAT_MSG_EMOTE:        //表情
        case CHAT_MSG_YELL:         //大喊
        {
            std::string msg;
            recv_data >> msg;       //消息内容

            if (msg.empty())        //消息空判断
                break;

            if (ChatHandler(this).ParseCommands(msg.c_str()))
                break;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            if (type == CHAT_MSG_SAY)
                GetPlayer()->Say(msg, lang);    //说话
            else if (type == CHAT_MSG_EMOTE)
                GetPlayer()->TextEmote(msg);    //表情
            else if (type == CHAT_MSG_YELL)     
                GetPlayer()->Yell(msg, lang);   //大喊
        } break;

        case CHAT_MSG_WHISPER:  //私聊
        {
            std::string to, msg;
            recv_data >> to;    //私聊对象
            recv_data >> msg;   //私聊消息

            if (msg.empty())
                break;

            if (ChatHandler(this).ParseCommands(msg.c_str()))
                break;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (!normalizePlayerName(to))
            {
                SendPlayerNotFoundNotice(to);
                break;
            }

            //获取私聊玩家信息
            Player* player = sObjectMgr.GetPlayer(to.c_str());
            uint32 tSecurity = GetSecurity();
            uint32 pSecurity = player ? player->GetSession()->GetSecurity() : SEC_PLAYER;
            if (!player || (tSecurity == SEC_PLAYER && pSecurity > SEC_PLAYER && !player->isAcceptWhispers()))
            {
                SendPlayerNotFoundNotice(to);
                return;
            }

            if (!sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_CHAT) && tSecurity == SEC_PLAYER && pSecurity == SEC_PLAYER)
            {
                if (GetPlayer()->GetTeam() != player->GetTeam())
                {
                    SendWrongFactionNotice();
                    return;
                }
            }

            //给私聊玩家发消息
            GetPlayer()->Whisper(msg, lang, player->GetObjectGuid());
        } break;

        case CHAT_MSG_PARTY:    //队伍聊天
        {
            std::string msg;
            recv_data >> msg;

            if (msg.empty())
                break;

            if (ChatHandler(this).ParseCommands(msg.c_str()))
                break;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            // if player is in battleground, he cannot say to battleground members by /p
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = _player->GetGroup();
                if (!group || group->isBGGroup())
                    return;
            }

            //把消息广播给队伍里成员
            WorldPacket data;
            ChatHandler::BuildChatPacket(data, ChatMsg(type), msg.c_str(), Language(lang), _player->GetChatTag(), _player->GetObjectGuid(), _player->GetName());
            group->BroadcastPacket(data, false, group->GetMemberGroup(GetPlayer()->GetObjectGuid()));

            break;
        }
        case CHAT_MSG_GUILD:    //公会聊天
        {
            std::string msg;
            recv_data >> msg;

            if (msg.empty())
                break;

            if (ChatHandler(this).ParseCommands(msg.c_str()))
                break;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            //把消息广播给公会的成员
            if (GetPlayer()->GetGuildId())
                if (Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId()))
                    guild->BroadcastToGuild(this, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);

            break;
        }
        case CHAT_MSG_OFFICER:  //官员聊天
        {
            std::string msg;
            recv_data >> msg;

            if (msg.empty())
                break;

            if (ChatHandler(this).ParseCommands(msg.c_str()))
                break;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            //把消息广播给公会的官员
            if (GetPlayer()->GetGuildId())  
                if (Guild* guild = sGuildMgr.GetGuildById(GetPlayer()->GetGuildId()))
                    guild->BroadcastToOfficers(this, msg, lang == LANG_ADDON ? LANG_ADDON : LANG_UNIVERSAL);

            break;
        }
        case CHAT_MSG_RAID: //团队聊天
        {
            std::string msg;
            recv_data >> msg;

            if (msg.empty())
                break;

            if (ChatHandler(this).ParseCommands(msg.c_str()))
                break;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = GetPlayer()->GetGroup();
                if (!group || group->isBGGroup() || !group->isRaidGroup())
                    return;
            }

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID, msg.c_str(), Language(lang), _player->GetChatTag(), _player->GetObjectGuid(), _player->GetName());
            group->BroadcastPacket(data, false);
        } break;
        case CHAT_MSG_RAID_LEADER:  //团队领袖聊天
        {
            std::string msg;
            recv_data >> msg;

            if (msg.empty())
                break;

            if (ChatHandler(this).ParseCommands(msg.c_str()))
                break;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            // if player is in battleground, he cannot say to battleground members by /ra
            Group* group = GetPlayer()->GetOriginalGroup();
            if (!group)
            {
                group = GetPlayer()->GetGroup();
                if (!group || group->isBGGroup() || !group->isRaidGroup() || !group->IsLeader(_player->GetObjectGuid()))
                    return;
            }

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID_LEADER, msg.c_str(), Language(lang), _player->GetChatTag(), _player->GetObjectGuid(), _player->GetName());
            group->BroadcastPacket(data, false);
        } break;

        case CHAT_MSG_RAID_WARNING: //团队警告聊天
        {
            std::string msg;
            recv_data >> msg;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isRaidGroup() ||
                    !(group->IsLeader(GetPlayer()->GetObjectGuid()) || group->IsAssistant(GetPlayer()->GetObjectGuid())))
                return;

            WorldPacket data;
            // in battleground, raid warning is sent only to players in battleground - code is ok
            ChatHandler::BuildChatPacket(data, CHAT_MSG_RAID_WARNING, msg.c_str(), Language(lang), _player->GetChatTag(), _player->GetObjectGuid(), _player->GetName());
            group->BroadcastPacket(data, false);
        } break;

        case CHAT_MSG_BATTLEGROUND: //战场聊天
        {
            std::string msg;
            recv_data >> msg;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            // battleground raid is always in Player->GetGroup(), never in GetOriginalGroup()
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isBGGroup())  //如果不是战场团队, 则直接返回
                return;

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_BATTLEGROUND, msg.c_str(), Language(lang), _player->GetChatTag(), _player->GetObjectGuid(), _player->GetName());
            group->BroadcastPacket(data, false);
        } break;

        case CHAT_MSG_BATTLEGROUND_LEADER:  //战场领袖聊天
        {
            std::string msg;
            recv_data >> msg;

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            // battleground raid is always in Player->GetGroup(), never in GetOriginalGroup()
            Group* group = GetPlayer()->GetGroup();
            if (!group || !group->isBGGroup() || !group->IsLeader(GetPlayer()->GetObjectGuid()))
                return;

            WorldPacket data;
            ChatHandler::BuildChatPacket(data, CHAT_MSG_BATTLEGROUND_LEADER, msg.c_str(), Language(lang), _player->GetChatTag(), _player->GetObjectGuid(), _player->GetName());
            group->BroadcastPacket(data, false);
        } break;

        case CHAT_MSG_CHANNEL:  //频道聊天
        {
            std::string channel, msg;
            recv_data >> channel;   //频道
            recv_data >> msg;       //消息

            if (!processChatmessageFurtherAfterSecurityChecks(msg, lang))
                return;

            if (msg.empty())
                break;

            //把消息广播给频道中的成员
            if (ChannelMgr* cMgr = channelMgr(_player->GetTeam()))
                if (Channel* chn = cMgr->GetChannel(channel, _player))
                    chn->Say(_player, msg.c_str(), lang);
        } break;

        case CHAT_MSG_AFK:      //AFK消息, 玩家退出
        {
            std::string msg;
            recv_data >> msg;

            if (!_player->isInCombat())
            {
                if (_player->isAFK())                       // Already AFK
                {
                    if (msg.empty())
                        _player->ToggleAFK();               // Remove AFK
                    else
                        _player->autoReplyMsg = msg;        // Update message
                }
                else                                        // New AFK mode
                {
                    _player->autoReplyMsg = msg.empty() ? GetMangosString(LANG_PLAYER_AFK_DEFAULT) : msg;

                    if (_player->isDND())
                        _player->ToggleDND();

                    _player->ToggleAFK();
                }
            }
            break;
        }
        case CHAT_MSG_DND:  //请勿打扰消息?
        {
            std::string msg;
            recv_data >> msg;

            if (_player->isDND())                           // Already DND
            {
                if (msg.empty())
                    _player->ToggleDND();                   // Remove DND
                else
                    _player->autoReplyMsg = msg;            // Update message
            }
            else                                            // New DND mode
            {
                _player->autoReplyMsg = msg.empty() ? GetMangosString(LANG_PLAYER_DND_DEFAULT) : msg;

                if (_player->isAFK())
                    _player->ToggleAFK();

                _player->ToggleDND();
            }
            break;
        }

        default:
            sLog.outError("CHAT: unknown message type %u, lang: %u", type, lang);
            break;
    }
}

//表情
void WorldSession::HandleEmoteOpcode(WorldPacket& recv_data)
{
    //如果玩家死亡或者假死状态, 不允许使用表情
    if (!GetPlayer()->isAlive() || GetPlayer()->IsFeigningDeath())
        return;

    uint32 emote;
    recv_data >> emote;
    GetPlayer()->HandleEmoteCommand(emote);
}

namespace MaNGOS
{
    class EmoteChatBuilder
    {
        public:
            EmoteChatBuilder(Player const& pl, uint32 text_emote, uint32 emote_num, Unit const* target)
                : i_player(pl), i_text_emote(text_emote), i_emote_num(emote_num), i_target(target) {}

            void operator()(WorldPacket& data, int32 loc_idx)
            {
                char const* nam = i_target ? i_target->GetNameForLocaleIdx(loc_idx) : nullptr;
                uint32 namlen = (nam ? strlen(nam) : 0) + 1;

                data.Initialize(SMSG_TEXT_EMOTE, (20 + namlen));
                data << ObjectGuid(i_player.GetObjectGuid());
                data << uint32(i_text_emote);
                data << uint32(i_emote_num);
                data << uint32(namlen);
                if (namlen > 1)
                    data.append(nam, namlen);
                else
                    data << uint8(0x00);
            }

        private:
            Player const& i_player;
            uint32        i_text_emote;
            uint32        i_emote_num;
            Unit const*   i_target;
    };
}                                                           // namespace MaNGOS

//文本表情
void WorldSession::HandleTextEmoteOpcode(WorldPacket& recv_data)
{
    //如果玩家死亡了, 不允许发文本表情
    if (!GetPlayer()->isAlive())
        return;

    //如果玩家不能说话, 禁止发文本表情
    if (!GetPlayer()->CanSpeak())
    {
        std::string timeStr = secsToTimeString(m_muteTime - time(nullptr));
        SendNotification(GetMangosString(LANG_WAIT_BEFORE_SPEAKING), timeStr.c_str());
        return;
    }

    uint32 text_emote, emoteNum;
    ObjectGuid guid;

    recv_data >> text_emote;    //文本表情
    recv_data >> emoteNum;      //文本表情
    recv_data >> guid;          //玩家guid

    //根据表情ID, 查找表情信息
    EmotesTextEntry const* em = sEmotesTextStore.LookupEntry(text_emote);
    if (!em)
        return;

    //表情ID
    uint32 emote_id = em->textid;

    switch (emote_id)
    {
        case EMOTE_STATE_SLEEP:
        case EMOTE_STATE_SIT:
        case EMOTE_STATE_KNEEL:
        case EMOTE_ONESHOT_NONE:
            break;
        default:
        {
            // in feign death state allowed only text emotes.
            if (GetPlayer()->IsFeigningDeath())
                break;

            GetPlayer()->HandleEmoteCommand(emote_id);
            break;
        }
    }

    Unit* unit = GetPlayer()->GetMap()->GetUnit(guid);

    MaNGOS::EmoteChatBuilder emote_builder(*GetPlayer(), text_emote, emoteNum, unit);
    MaNGOS::LocalizedPacketDo<MaNGOS::EmoteChatBuilder > emote_do(emote_builder);
    MaNGOS::CameraDistWorker<MaNGOS::LocalizedPacketDo<MaNGOS::EmoteChatBuilder > > emote_worker(GetPlayer(), sWorld.getConfig(CONFIG_FLOAT_LISTEN_RANGE_TEXTEMOTE), emote_do);
    Cell::VisitWorldObjects(GetPlayer(), emote_worker, sWorld.getConfig(CONFIG_FLOAT_LISTEN_RANGE_TEXTEMOTE));

    // Send scripted event call
    if (unit && unit->AI())
        unit->AI()->ReceiveEmote(GetPlayer(), text_emote);
}

void WorldSession::HandleChatIgnoredOpcode(WorldPacket& recv_data)
{
    ObjectGuid iguid;
    uint8 unk;
    // DEBUG_LOG("WORLD: Received opcode CMSG_CHAT_IGNORED");

    recv_data >> iguid;
    recv_data >> unk;                                       // probably related to spam reporting

    Player* player = sObjectMgr.GetPlayer(iguid);
    if (!player || !player->GetSession())
        return;

    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_IGNORED, _player->GetName(), LANG_UNIVERSAL, CHAT_TAG_NONE, _player->GetObjectGuid());
    player->GetSession()->SendPacket(data);
}

void WorldSession::SendPlayerNotFoundNotice(const std::string& name) const
{
    WorldPacket data(SMSG_CHAT_PLAYER_NOT_FOUND, name.size() + 1);
    data << name;
    SendPacket(data);
}

void WorldSession::SendWrongFactionNotice() const
{
    WorldPacket data(SMSG_CHAT_WRONG_FACTION, 0);
    SendPacket(data);
}

void WorldSession::SendChatRestrictedNotice(ChatRestrictionType restriction) const
{
    WorldPacket data(SMSG_CHAT_RESTRICTED, 1);
    data << uint8(restriction);
    SendPacket(data);
}
