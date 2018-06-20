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

/// \addtogroup realmd Realm Daemon
/// @{
/// \file

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "RealmList.h"

#include "Config/Config.h"
#include "Log.h"
#include "AuthSocket.h"
#include "SystemConfig.h"
#include "revision.h"
#include "revision_sql.h"
#include "Util.h"
#include "Network/Listener.hpp"

#include <openssl/opensslv.h>
#include <openssl/crypto.h>

#include <boost/program_options.hpp>
#include <boost/version.hpp>

#include <iostream>
#include <string>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include "ServiceWin32.h"
char serviceName[] = "realmd";
char serviceLongName[] = "MaNGOS realmd service";
char serviceDescription[] = "Massive Network Game Object Server";
/*
 * -1 - not in service mode
 *  0 - stopped
 *  1 - running
 *  2 - paused
 */
int m_ServiceStatus = -1;
#else
#include "PosixDaemon.h"
#endif

bool StartDB();
void UnhookSignals();
void HookSignals();

bool stopEvent = false;                                     ///< Setting it to true stops the server

DatabaseType LoginDatabase;                                 ///< Accessor to the realm server database

/// Print out the usage string for this program on the console.
void usage(const char* prog)
{
    sLog.outString("Usage: \n %s [<options>]\n"
                   "    -v, --version            print version and exist\n\r"
                   "    -c config_file           use config_file as configuration file\n\r"
#ifdef _WIN32
                   "    Running as service functions:\n\r"
                   "    -s run                   run as service\n\r"
                   "    -s install               install service\n\r"
                   "    -s uninstall             uninstall service\n\r"
#else
                   "    Running as daemon functions:\n\r"
                   "    -s run                   run as daemon\n\r"
                   "    -s stop                  stop daemon\n\r"
#endif
                   , prog);
}

/// Launch the realm server
int main(int argc, char* argv[])
{    
    std::string configFile, serviceParameter;   //定义变量, 配置文件、服务参数

    //构造选项描述器
    boost::program_options::options_description desc("Allowed options");    //这里的Allowed options会作为命令行提示的抬头
        
    //添加选项
    //可使用--config或-c指定配置文件路径, 默认为../etc/realmd.conf, 存放到configFile变量中, 提示语为configuration file
    //可使用--Version或-V打印版本号, 提示语为print version and exit
    desc.add_options()
    ("config,c", boost::program_options::value<std::string>(&configFile)->default_value(_REALMD_CONFIG), "configuration file")
    ("version,v", "print version and exit")
#ifdef _WIN32
    ("s", boost::program_options::value<std::string>(&serviceParameter), "<run, install, uninstall> service");
#else
    ("s", boost::program_options::value<std::string>(&serviceParameter), "<run, stop> service");
#endif

    boost::program_options::variables_map vm;

    try
    {
        //解析命令行参数, 保存到vm中
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
            
        //通知vm更新所有外部变量
        boost::program_options::notify(vm);
    }
    catch (boost::program_options::error const& e)
    {
        //异常处理
        std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
        std::cerr << desc << std::endl;

        return 1;
    }

    //windows下 --s 选项处理
#ifdef _WIN32                                                  // windows service command need execute before config read
    if (vm.count("s"))  //如果--s选项被输入
    {
        switch (::tolower(serviceParameter[0])) //服务参数转换为小写
        {
            case 'i':   //如果第一位为i, 对应提示的install,则调用WinServiceInstall
                if (WinServiceInstall())
                    sLog.outString("Installing service");
                return 1;
            case 'u': //如果第一位为u, 对应提示的uninstall,则调用WinServiceUninstall
                if (WinServiceUninstall())
                    sLog.outString("Uninstalling service");
                return 1;
            case 'r': //如果第一位为r, 对应提示的run,则调用WinServiceRun
                WinServiceRun();
                break;
        }
    }
#endif

    //赋值配置文件名称给sConfig的成员变量m_filename,加载配置文件到sConfig的成员变量 map:m_entries中
    //sConfig在config.h中定义, #define sConfig MaNGOS::Singleton<Config>::Instance()
    if (!sConfig.SetSource(configFile))
    {
        sLog.outError("Could not find configuration file %s.", configFile.c_str());
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

//linux 下的--s选项
#ifndef _WIN32                                               // posix daemon commands need apply after config read
    if (vm.count("s"))
    {
        switch (::tolower(serviceParameter[0])) //转为小写
        {
            case 'r':       //第一位为r, 对应的参数为run, 则启动守护进程, 使当前进程在后台执行
                startDaemon();
                break;
            case 's':       //第一位为s, 对应的参数为stop, 则停止守护进程, 实测报错No pid file specified
                stopDaemon();
                break;
        }
    }
#endif

    //初始化日志
    //sLog定义 #define sLog MaNGOS::Singleton<Log>::Instance(), 这个类Singleton抽时间需要细看一下
    sLog.Initialize();
    
    //打印日志
    sLog.outString("%s [realm-daemon]", _FULLVERSION(REVISION_DATE, REVISION_ID));
    sLog.outString("<Ctrl-C> to stop.\n");
    sLog.outString("Using configuration file %s.", configFile.c_str());

    ///- Check the version of the configuration file
    //判断配置的版本号, 如果版本过低, 则抛出警告
    uint32 confVersion = sConfig.GetIntDefault("ConfVersion", 0);
    if (confVersion < _REALMDCONFVERSION)
    {
        sLog.outError("*****************************************************************************");
        sLog.outError(" WARNING: Your realmd.conf version indicates your conf file is out of date!");
        sLog.outError("          Please check for updates, as your current default values may cause");
        sLog.outError("          strange behavior.");
        sLog.outError("*****************************************************************************");
        Log::WaitBeforeContinueIfNeed();
    }

    //打印openssl和SSLeasy的版本
    DETAIL_LOG("%s (Library: %s)", OPENSSL_VERSION_TEXT, SSLeay_version(SSLEAY_VERSION));
    if (SSLeay() < 0x009080bfL)
    {
        DETAIL_LOG("WARNING: Outdated version of OpenSSL lib. Logins to server may not work!");
        DETAIL_LOG("WARNING: Minimal required version [OpenSSL 0.9.8k]");
    }

    /// realmd PID file creation
    //从readmd.conf中获取Pid文件路径
    std::string pidfile = sConfig.GetStringDefault("PidFile");
    if (!pidfile.empty())   //如果pid文件不为空
    {
        //创建pid文件, 将当前进程id写入到pid文件中, 同时返回pid
        uint32 pid = CreatePIDFile(pidfile);
        if (!pid)
        {
            sLog.outError("Cannot create PID file %s.\n", pidfile.c_str());
            Log::WaitBeforeContinueIfNeed();
            return 1;
        }

        //打印当前进程pid
        sLog.outString("Daemon PID: %u\n", pid);
    }

    ///- Initialize the database connection
    //启动数据库,包含创建一个连接池和一个用于同步的连接, 连接信息是从配置中获取的
    if (!StartDB())
    {
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

    ///- Get the list of realms for the server
    //从数据库realmlist表中获取信息, 加载到缓存成员变量m_realms,这是一个MAP
    sRealmList.Initialize(sConfig.GetIntDefault("RealmsStateUpdateDelay", 20));
    if (sRealmList.size() == 0)
    {
        sLog.outError("No valid realms specified.");
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

    // cleanup query
    // set expired bans to inactive
    LoginDatabase.BeginTransaction();
    LoginDatabase.Execute("UPDATE account_banned SET active = 0 WHERE unbandate<=UNIX_TIMESTAMP() AND unbandate<>bandate");
    LoginDatabase.Execute("DELETE FROM ip_banned WHERE unbandate<=UNIX_TIMESTAMP() AND unbandate<>bandate");
    LoginDatabase.CommitTransaction();

    // FIXME - more intelligent selection of thread count is needed here.  config option?
    MaNGOS::Listener<AuthSocket> listener(sConfig.GetStringDefault("BindIP", "0.0.0.0"), sConfig.GetIntDefault("RealmServerPort", DEFAULT_REALMSERVER_PORT), 1);

    ///- Catch termination signals
    HookSignals();

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
                    sLog.outError("Processors marked in UseProcessors bitmask (hex) %x not accessible for realmd. Accessible processors bitmask (hex): %x", Aff, appAff);
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

        if (Prio)
        {
            if (SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS))
                sLog.outString("realmd process priority class set to HIGH");
            else
                sLog.outError("Can't set realmd process priority class.");
            sLog.outString();
        }
    }
#endif

    // server has started up successfully => enable async DB requests
    LoginDatabase.AllowAsyncTransactions();

    // maximum counter for next ping
    auto const numLoops = sConfig.GetIntDefault("MaxPingTime", 30) * MINUTE * 10;
    uint32 loopCounter = 0;

#ifndef _WIN32
    detachDaemon();
#endif
    ///- Wait for termination signal
    while (!stopEvent)
    {
        if ((++loopCounter) == numLoops)
        {
            loopCounter = 0;
            DETAIL_LOG("Ping MySQL to keep connection alive");
            LoginDatabase.Ping();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#ifdef _WIN32
        if (m_ServiceStatus == 0) stopEvent = true;
        while (m_ServiceStatus == 2) Sleep(1000);
#endif
    }


    ///- Wait for the delay thread to exit
    LoginDatabase.HaltDelayThread();

    ///- Remove signal handling before leaving
    UnhookSignals();

    sLog.outString("Halting process...");
    return 0;
}

/// Handle termination signals
/** Put the global variable stopEvent to 'true' if a termination signal is caught **/
void OnSignal(int s)
{
    switch (s)
    {
        case SIGINT:
        case SIGTERM:
            stopEvent = true;
            break;
#ifdef _WIN32
        case SIGBREAK:
            stopEvent = true;
            break;
#endif
    }

    signal(s, OnSignal);
}

/// Initialize connection to the database
bool StartDB()
{
    //从配置文件中获取数据库名称
    std::string dbstring = sConfig.GetStringDefault("LoginDatabaseInfo");
    if (dbstring.empty())
    {
        sLog.outError("Database not specified");
        return false;
    }

    sLog.outString("Login Database total connections: %i", 1 + 1);

    //初始化数据库, 传入数据库连接,例如“127.0.0.1;3306;root;123456;realmd”
    if (!LoginDatabase.Initialize(dbstring.c_str()))
    {
        sLog.outError("Cannot connect to database");
        return false;
    }

    //检查数据库版本
    if (!LoginDatabase.CheckRequiredField("realmd_db_version", REVISION_DB_REALMD))
    {
        ///- Wait for already started DB delay threads to end
        LoginDatabase.HaltDelayThread();
        return false;
    }

    return true;
}

/// Define hook 'OnSignal' for all termination signals
void HookSignals()
{
    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
#ifdef _WIN32
    signal(SIGBREAK, OnSignal);
#endif
}

/// Unhook the signals before leaving
void UnhookSignals()
{
    signal(SIGINT, 0);
    signal(SIGTERM, 0);
#ifdef _WIN32
    signal(SIGBREAK, 0);
#endif
}

/// @}
