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

/** \file
    \ingroup mangosd
*/

#ifndef _WIN32
#include "PosixDaemon.h"
#endif

#include "Common.h"
#include "Master.h"
#include "Server/WorldSocket.h"
#include "WorldRunnable.h"
#include "World/World.h"
#include "Log.h"
#include "Timer.h"
#include "SystemConfig.h"
#include "CliRunnable.h"
#include "RASocket.h"
#include "Util.h"
#include "revision_sql.h"
#include "MaNGOSsoap.h"
#include "Mails/MassMailMgr.h"
#include "Server/DBCStores.h"

#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Policies/Singleton.h"
#include "Network/Listener.hpp"
#include "Network/Socket.hpp"

#include <memory>

#ifdef _WIN32
#include "ServiceWin32.h"
extern int m_ServiceStatus;
#endif

INSTANTIATE_SINGLETON_1(Master);

volatile bool Master::m_canBeKilled = false;

class FreezeDetectorRunnable : public MaNGOS::Runnable
{
    public:
        FreezeDetectorRunnable() { _delaytime = 0; }
        uint32 m_loops, m_lastchange;
        uint32 w_loops, w_lastchange;
        uint32 _delaytime;
        void SetDelayTime(uint32 t) { _delaytime = t; }
        void run(void)
        {
            if (!_delaytime)
                return;
            sLog.outString("Starting up anti-freeze thread (%u seconds max stuck time)...", _delaytime / 1000);
            m_loops = 0;
            w_loops = 0;
            m_lastchange = 0;
            w_lastchange = 0;
            while (!World::IsStopped())
            {
                MaNGOS::Thread::Sleep(1000);

                uint32 curtime = WorldTimer::getMSTime();

                // normal work
                if (w_loops != World::m_worldLoopCounter)
                {
                    w_lastchange = curtime;
                    w_loops = World::m_worldLoopCounter;
                }
                // possible freeze
                else if (WorldTimer::getMSTimeDiff(w_lastchange, curtime) > _delaytime)
                {
                    sLog.outError("World Thread hangs, kicking out server!");
                    *((uint32 volatile*)nullptr) = 0;          // bang crash
                }
            }
            sLog.outString("Anti-freeze thread exiting without problems.");
        }
};

/// Main function
int Master::Run()
{
    /// worldd PID file creation
    //获取pid文件名
    //这里实际就往pid文件里写了个进程号, 用处不大
    std::string pidfile = sConfig.GetStringDefault("PidFile");
    if (!pidfile.empty())   //若pid文件名配置不为空
    {
        uint32 pid = CreatePIDFile(pidfile);        //创建pid文件
        if (!pid)                                   
        {
            sLog.outError("Cannot create PID file %s.\n", pidfile.c_str());
            Log::WaitBeforeContinueIfNeed();
            return 1;
        }

        sLog.outString("Daemon PID: %u\n", pid);    //日志打印pid
    }

    ///- Start the databases
    //启动数据库
    if (!_StartDB())
    {
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

    ///- Initialize the World
    //初始化世界服务器配置, 从配置文件和数据库
    sWorld.SetInitialWorldSettings();

#ifndef _WIN32
    detachDaemon();
#endif
    // server loaded successfully => enable async DB requests
    // this is done to forbid any async transactions during server startup!
    //将成员变量允许接收同步处理设置为true
    CharacterDatabase.AllowAsyncTransactions();
    WorldDatabase.AllowAsyncTransactions();
    LoginDatabase.AllowAsyncTransactions();

    ///- Catch termination signals
    //信号处理
    _HookSignals();

    ///- Launch WorldRunnable thread
    //启动世界运服务行线程
    MaNGOS::Thread world_thread(new WorldRunnable);
    world_thread.setPriority(MaNGOS::Priority_Highest);

    // set realmbuilds depend on mangosd expected builds, and set server online
    {
        //将当前支持的版本更新到realmlist数据库, 目前支持8086
        //将世界服务运行状态更新为在线
        std::string builds = AcceptableClientBuildsListStr();
        LoginDatabase.escape_string(builds);
        LoginDatabase.DirectPExecute("UPDATE realmlist SET realmflags = realmflags & ~(%u), population = 0, realmbuilds = '%s'  WHERE id = '%u'", REALM_FLAG_OFFLINE, builds.c_str(), realmID);
    }

    MaNGOS::Thread* cliThread = nullptr;

    //需要命令行的话起一个线程处理
#ifdef _WIN32
    if (sConfig.GetBoolDefault("Console.Enable", true) && (m_ServiceStatus == -1)/* need disable console in service mode*/)
#else
    if (sConfig.GetBoolDefault("Console.Enable", true))
#endif
    {
        ///- Launch CliRunnable thread
        cliThread = new MaNGOS::Thread(new CliRunnable);
    }

    ///- Handle affinity for multiple processors and process priority on Windows
#ifdef _WIN32
    {
        HANDLE hProcess = GetCurrentProcess();

        uint32 Aff = sConfig.GetIntDefault("UseProcessors", 0);
        if (Aff > 0)
        {
            ULONG_PTR appAff;
            ULONG_PTR sysAff;

            if (GetProcessAffinityMask(hProcess, &appAff, &sysAff))
            {
                ULONG_PTR curAff = Aff & appAff;            // remove non accessible processors

                if (!curAff)
                {
                    sLog.outError("Processors marked in UseProcessors bitmask (hex) %x not accessible for mangosd. Accessible processors bitmask (hex): %x", Aff, appAff);
                }
                else
                {
                    if (SetProcessAffinityMask(hProcess, curAff))
                        sLog.outString("Using processors (bitmask, hex): %x", curAff);
                    else
                        sLog.outError("Can't set used processors (hex): %x", curAff);
                }
            }
            sLog.outString();
        }

        bool Prio = sConfig.GetBoolDefault("ProcessPriority", false);

//        if(Prio && (m_ServiceStatus == -1)/* need set to default process priority class in service mode*/)
        if (Prio)
        {
            if (SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS))
                sLog.outString("mangosd process priority class set to HIGH");
            else
                sLog.outError("Can't set mangosd process priority class.");
            sLog.outString();
        }
    }
#endif

    ///- Start up freeze     thread
    // 如果配置了冻结时间, 则启动冻结线程, 当前配置的是0, 不冻结
    MaNGOS::Thread* freeze_thread = nullptr;
    if (uint32 freeze_delay = sConfig.GetIntDefault("MaxCoreStuckTime", 0))
    {
        FreezeDetectorRunnable* fdr = new FreezeDetectorRunnable();
        fdr->SetDelayTime(freeze_delay * 1000);
        freeze_thread = new MaNGOS::Thread(fdr);
        freeze_thread->setPriority(MaNGOS::Priority_Highest);
    }

    {
        //绑定世界服务端口, 当前用的是8085
        MaNGOS::Listener<WorldSocket> listener(sConfig.GetStringDefault("BindIP", "0.0.0.0"), int32(sWorld.getConfig(CONFIG_UINT32_PORT_WORLD)), 8);

        std::unique_ptr<MaNGOS::Listener<RASocket>> raListener;
        if (sConfig.GetBoolDefault("Ra.Enable", false))
            raListener.reset(new MaNGOS::Listener<RASocket>(sConfig.GetStringDefault("Ra.IP", "0.0.0.0"), sConfig.GetIntDefault("Ra.Port", 3443), 1));

        std::unique_ptr<SOAPThread> soapThread;
        if (sConfig.GetBoolDefault("SOAP.Enabled", false))
            soapThread.reset(new SOAPThread(sConfig.GetStringDefault("SOAP.IP", "127.0.0.1"), sConfig.GetIntDefault("SOAP.Port", 7878)));

        // wait for shut down and then let things go out of scope to close them down
        //每毫秒一次循环, 检查m_stopEvent是否为true, 是则退出
        while (!World::IsStopped())
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    ///- Stop freeze protection before shutdown tasks
    //在退出之前关闭冻结线程
    if (freeze_thread)
    {
        freeze_thread->destroy();
        delete freeze_thread;
    }

    ///- Set server offline in realmlist
    //将realmlist中世界服务运行状态更新为离线
    LoginDatabase.DirectPExecute("UPDATE realmlist SET realmflags = realmflags | %u WHERE id = '%u'", REALM_FLAG_OFFLINE, realmID);

    ///- Remove signal handling before leaving
    //取消信号处理
    _UnhookSignals();

    // when the main thread closes the singletons get unloaded
    // since worldrunnable uses them, it will crash if unloaded after master
    //等待世界服务线程退出
    world_thread.wait();

    ///- Clean account database before leaving
    //将在线的用户状态都设置为离线
    clearOnlineAccounts();

    // send all still queued mass mails (before DB connections shutdown)
    sMassMailMgr.Update(true);

    ///- Wait for DB delay threads to end
    //等待数据库线程退出
    CharacterDatabase.HaltDelayThread();
    WorldDatabase.HaltDelayThread();
    LoginDatabase.HaltDelayThread();

    sLog.outString("Halting process...");

    if (cliThread)
    {
#ifdef _WIN32

        // this only way to terminate CLI thread exist at Win32 (alt. way exist only in Windows Vista API)
        //_exit(1);
        // send keyboard input to safely unblock the CLI thread
        INPUT_RECORD b[5];
        HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
        b[0].EventType = KEY_EVENT;
        b[0].Event.KeyEvent.bKeyDown = TRUE;
        b[0].Event.KeyEvent.uChar.AsciiChar = 'X';
        b[0].Event.KeyEvent.wVirtualKeyCode = 'X';
        b[0].Event.KeyEvent.wRepeatCount = 1;

        b[1].EventType = KEY_EVENT;
        b[1].Event.KeyEvent.bKeyDown = FALSE;
        b[1].Event.KeyEvent.uChar.AsciiChar = 'X';
        b[1].Event.KeyEvent.wVirtualKeyCode = 'X';
        b[1].Event.KeyEvent.wRepeatCount = 1;

        b[2].EventType = KEY_EVENT;
        b[2].Event.KeyEvent.bKeyDown = TRUE;
        b[2].Event.KeyEvent.dwControlKeyState = 0;
        b[2].Event.KeyEvent.uChar.AsciiChar = '\r';
        b[2].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        b[2].Event.KeyEvent.wRepeatCount = 1;
        b[2].Event.KeyEvent.wVirtualScanCode = 0x1c;

        b[3].EventType = KEY_EVENT;
        b[3].Event.KeyEvent.bKeyDown = FALSE;
        b[3].Event.KeyEvent.dwControlKeyState = 0;
        b[3].Event.KeyEvent.uChar.AsciiChar = '\r';
        b[3].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        b[3].Event.KeyEvent.wVirtualScanCode = 0x1c;
        b[3].Event.KeyEvent.wRepeatCount = 1;
        DWORD numb;
        BOOL ret = WriteConsoleInput(hStdIn, b, 4, &numb);

        cliThread->wait();

#else

        cliThread->destroy();

#endif

        delete cliThread;
    }

    // mark this can be killable
    //允许被杀死
    m_canBeKilled = true;

    ///- Exit the process with specified return value
    //返回退出码
    return World::GetExitCode();
}

/// Initialize connection to the databases
bool Master::_StartDB()
{
    ///- Get world database info from configuration file
    //获取数据库连接配置
    std::string dbstring = sConfig.GetStringDefault("WorldDatabaseInfo");

    //获取数据库库连接数量配置, 没有配则默认为1
    int nConnections = sConfig.GetIntDefault("WorldDatabaseConnections", 1);

    //数据库连接配置空值校验
    if (dbstring.empty())
    {
        sLog.outError("Database not specified in configuration file");
        return false;
    }
    sLog.outString("World Database total connections: %i", nConnections + 1);

    ///- Initialise the world database
    //初始化世界服务器数据库, 内部链接数据库, 设置字符编码为utf-8
    if (!WorldDatabase.Initialize(dbstring.c_str(), nConnections))
    {
        sLog.outError("Cannot connect to world database %s", dbstring.c_str());
        return false;
    }

    //校验世界服务器数据库版本
    if (!WorldDatabase.CheckRequiredField("db_version", REVISION_DB_MANGOS))
    {
        ///- Wait for already started DB delay threads to end
        WorldDatabase.HaltDelayThread();
        return false;
    }

    //获取角色服务器数据库配置
    dbstring = sConfig.GetStringDefault("CharacterDatabaseInfo");

    //获取连接数量, 默认为1 
    nConnections = sConfig.GetIntDefault("CharacterDatabaseConnections", 1);

    //数据库库配置空值校验
    if (dbstring.empty())
    {
        sLog.outError("Character Database not specified in configuration file");

        ///- Wait for already started DB delay threads to end
        WorldDatabase.HaltDelayThread();
        return false;
    }
    sLog.outString("Character Database total connections: %i", nConnections + 1);

    ///- Initialise the Character database
    //角色数据库初始化, 内部链接数据库, 设置字符编码为utf-8
    if (!CharacterDatabase.Initialize(dbstring.c_str(), nConnections))
    {
        sLog.outError("Cannot connect to Character database %s", dbstring.c_str());

        ///- Wait for already started DB delay threads to end
        WorldDatabase.HaltDelayThread();
        return false;
    }

    //角色数据库库版本校验
    if (!CharacterDatabase.CheckRequiredField("character_db_version", REVISION_DB_CHARACTERS))
    {
        ///- Wait for already started DB delay threads to end
        WorldDatabase.HaltDelayThread();
        CharacterDatabase.HaltDelayThread();
        return false;
    }

    ///- Get login database info from configuration file
    //获取登录数据库配置, 实际是realmd库
    dbstring = sConfig.GetStringDefault("LoginDatabaseInfo");

    //获取登录数据库连接数
    nConnections = sConfig.GetIntDefault("LoginDatabaseConnections", 1);

    //数据库配置空值校验
    if (dbstring.empty())
    {
        sLog.outError("Login database not specified in configuration file");

        ///- Wait for already started DB delay threads to end
        WorldDatabase.HaltDelayThread();
        CharacterDatabase.HaltDelayThread();
        return false;
    }

    ///- Initialise the login database
    sLog.outString("Login Database total connections: %i", nConnections + 1);

    //登录数据库库初始化, 内部链接数据库, 设置字符编码为utf-8
    if (!LoginDatabase.Initialize(dbstring.c_str(), nConnections))
    {
        sLog.outError("Cannot connect to login database %s", dbstring.c_str());

        ///- Wait for already started DB delay threads to end
        WorldDatabase.HaltDelayThread();
        CharacterDatabase.HaltDelayThread();
        return false;
    }

    //登录数据库版本校验
    if (!LoginDatabase.CheckRequiredField("realmd_db_version", REVISION_DB_REALMD))
    {
        ///- Wait for already started DB delay threads to end
        WorldDatabase.HaltDelayThread();
        CharacterDatabase.HaltDelayThread();
        LoginDatabase.HaltDelayThread();
        return false;
    }

    sLog.outString();

    ///- Get the realm Id from the configuration file
    //获取realm ID, 作用....
    realmID = sConfig.GetIntDefault("RealmID", 0);
    if (!realmID)
    {
        sLog.outError("Realm ID not defined in configuration file");

        ///- Wait for already started DB delay threads to end
        WorldDatabase.HaltDelayThread();
        CharacterDatabase.HaltDelayThread();
        LoginDatabase.HaltDelayThread();
        return false;
    }

    sLog.outString("Realm running as realm ID %d", realmID);
    sLog.outString();

    ///- Clean the database before starting
    //把当前是登录状态的数据更新为非登陆状态
    clearOnlineAccounts();

    sWorld.LoadDBVersion();

    sLog.outString("Using World DB: %s", sWorld.GetDBVersion());
    sLog.outString("Using creature EventAI: %s", sWorld.GetCreatureEventAIVersion());
    sLog.outString();
    return true;
}

/// Clear 'online' status for all accounts with characters in this realm
void Master::clearOnlineAccounts()
{
    // Cleanup online status for characters hosted at current realm
    /// \todo Only accounts with characters logged on *this* realm should have online status reset. Move the online column from 'account' to 'realmcharacters'?
    LoginDatabase.PExecute("UPDATE account SET active_realm_id = 0 WHERE active_realm_id = '%u'", realmID);

    CharacterDatabase.Execute("UPDATE characters SET online = 0 WHERE online<>0");

    // Battleground instance ids reset at server restart
    CharacterDatabase.Execute("UPDATE character_battleground_data SET instance_id = 0");
}

/// Handle termination signals
void Master::_OnSignal(int s)
{
    switch (s)
    {
        case SIGINT:
            World::StopNow(RESTART_EXIT_CODE);
            break;
        case SIGTERM:
#ifdef _WIN32
        case SIGBREAK:
#endif
            World::StopNow(SHUTDOWN_EXIT_CODE);
            break;
    }

    // give a 30 sec timeout in case of Master cannot finish properly
    int32 timeOut = 200;
    while (!m_canBeKilled && timeOut > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        --timeOut;
    }

    signal(s, _OnSignal);
}

/// Define hook '_OnSignal' for all termination signals
void Master::_HookSignals()
{
    signal(SIGINT, _OnSignal);
    signal(SIGTERM, _OnSignal);
#ifdef _WIN32
    signal(SIGBREAK, _OnSignal);
#endif
}

/// Unhook the signals before leaving
void Master::_UnhookSignals()
{
    signal(SIGINT, 0);
    signal(SIGTERM, 0);
#ifdef _WIN32
    signal(SIGBREAK, 0);
#endif
}
