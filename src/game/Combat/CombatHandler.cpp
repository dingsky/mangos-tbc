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
#include "Entities/ObjectGuid.h"
#include "Entities/Player.h"

//开始攻击
void WorldSession::HandleAttackSwingOpcode(WorldPacket& recv_data)
{
    ObjectGuid guid;
    recv_data >> guid;  //被攻击对象

    DEBUG_FILTER_LOG(LOG_FILTER_COMBAT, "WORLD: Received opcode CMSG_ATTACKSWING %s", guid.GetString().c_str());

    //如果被攻击的对象既不是生物也不是玩家, 则返回失败
    if (!guid.IsUnit())
    {
        sLog.outError("WORLD: %s isn't unit", guid.GetString().c_str());
        return;
    }

    //根据guid获取被攻击对象信息
    Unit* pEnemy = _player->GetMap()->GetUnit(guid);

    //玩家不能攻击目标
    if (!_player->CanAttackNow(pEnemy))
    {
        // stop attack state at client
        SendAttackStop(nullptr);
        return;
    }

    //攻击目标
    _player->Attack(pEnemy, true);
}

//停止攻击
void WorldSession::HandleAttackStopOpcode(WorldPacket& /*recv_data*/)
{
    GetPlayer()->AttackStop();
}

void WorldSession::HandleSetSheathedOpcode(WorldPacket& recv_data)
{
    uint32 sheathed;
    recv_data >> sheathed;

    DEBUG_LOG("WORLD: Received opcode CMSG_SETSHEATHED for %s - value: %u", GetPlayer()->GetGuidStr().c_str(), sheathed);

    if (sheathed >= MAX_SHEATH_STATE)
    {
        sLog.outError("Unknown sheath state %u ??", sheathed);
        return;
    }

    GetPlayer()->SetSheath(SheathState(sheathed));
}

void WorldSession::SendAttackStop(Unit const* enemy) const
{
    WorldPacket data(SMSG_ATTACKSTOP, (4 + 20));            // we guess size
    data << GetPlayer()->GetPackGUID();
    data << (enemy ? enemy->GetPackGUID() : PackedGuid());  // must be packed guid
    data << uint32(0);                                      // unk, can be 1 also
    SendPacket(data);
}
