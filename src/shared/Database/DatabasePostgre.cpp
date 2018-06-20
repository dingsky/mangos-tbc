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

#ifdef DO_POSTGRESQL

#include "Util.h"
#include "Policies/Singleton.h"
#include "Platform/Define.h"
#include "Threading.h"
#include "DatabaseEnv.h"
#include "Database/SqlOperations.h"
#include "Timer.h"

size_t DatabasePostgre::db_count = 0;

DatabasePostgre::DatabasePostgre()
{
    // before first connection
    if (db_count++ == 0)
    {
        if (!PQisthreadsafe())
        {
            sLog.outError("FATAL ERROR: PostgreSQL libpq isn't thread-safe.");
            exit(1);
        }
    }
}

DatabasePostgre::~DatabasePostgre()
{

}

SqlConnection* DatabasePostgre::CreateConnection()
{
    return new PostgreSQLConnection(*this);
}

PostgreSQLConnection::~PostgreSQLConnection()
{
    PQfinish(mPGconn);
}

bool PostgreSQLConnection::Initialize(const char* infoString)
{
    //infoString从配置中获取, 例如"127.0.0.1;3306;root;123456;realmd", 按照分号进行分割
    Tokens tokens = StrSplit(infoString, ";");

    Tokens::iterator iter;

    std::string host, port_or_socket_dir, user, password, database;

    iter = tokens.begin();

    if (iter != tokens.end())
        host = *iter++;                 //数据库IP
    if (iter != tokens.end())
        port_or_socket_dir = *iter++;   //数据库端口
    if (iter != tokens.end())
        user = *iter++;                 //用户名
    if (iter != tokens.end())
        password = *iter++;             //密码
    if (iter != tokens.end())
        database = *iter++;             //数据库名

    //登陆数据库
    if (host == ".")
        mPGconn = PQsetdbLogin(nullptr, port_or_socket_dir == "." ? nullptr : port_or_socket_dir.c_str(), nullptr, nullptr, database.c_str(), user.c_str(), password.c_str());
    else
        mPGconn = PQsetdbLogin(host.c_str(), port_or_socket_dir.c_str(), nullptr, nullptr, database.c_str(), user.c_str(), password.c_str());

    /* check to see that the backend connection was successfully made */
    //检查连接是否正常
    if (PQstatus(mPGconn) != CONNECTION_OK)
    {
        sLog.outError("Could not connect to Postgre database at %s: %s",
                      host.c_str(), PQerrorMessage(mPGconn));
        PQfinish(mPGconn);
        mPGconn = nullptr;
        return false;
    }

    //打印连接日志、数据库版本号
    DETAIL_LOG("Connected to Postgre database %s@%s:%s/%s", user.c_str(), host.c_str(), port_or_socket_dir.c_str(), database.c_str());
    sLog.outString("PostgreSQL server ver: %d", PQserverVersion(mPGconn));  
    
    return true;
}

bool PostgreSQLConnection::_Query(const char* sql, PGresult** pResult, uint64* pRowCount, uint32* pFieldCount)
{
    if (!mPGconn)
        return false;

    uint32 _s = WorldTimer::getMSTime();
    // Send the query
    *pResult = PQexec(mPGconn, sql);
    if (!*pResult)
        return false;

    if (PQresultStatus(*pResult) != PGRES_TUPLES_OK)
    {
        sLog.outErrorDb("SQL : %s", sql);
        sLog.outErrorDb("SQL %s", PQerrorMessage(mPGconn));
        PQclear(*pResult);
        return false;
    }
    else
    {
        DEBUG_FILTER_LOG(LOG_FILTER_SQL_TEXT, "[%u ms] SQL: %s", WorldTimer::getMSTimeDiff(_s, WorldTimer::getMSTime()), sql);
    }

    *pRowCount = PQntuples(*pResult);
    *pFieldCount = PQnfields(*pResult);
    // end guarded block

    if (!*pRowCount)
    {
        PQclear(*pResult);
        return false;
    }

    return true;
}

QueryResult* PostgreSQLConnection::Query(const char* sql)
{
    if (!mPGconn)
        return nullptr;

    PGresult* result = nullptr;
    uint64 rowCount = 0;
    uint32 fieldCount = 0;

    if (!_Query(sql, &result, &rowCount, &fieldCount))
        return nullptr;

    QueryResultPostgre* queryResult = new QueryResultPostgre(result, rowCount, fieldCount);

    queryResult->NextRow();
    return queryResult;
}

QueryNamedResult* PostgreSQLConnection::QueryNamed(const char* sql)
{
    if (!mPGconn)
        return nullptr;

    PGresult* result = nullptr;
    uint64 rowCount = 0;
    uint32 fieldCount = 0;

    if (!_Query(sql, &result, &rowCount, &fieldCount))
        return nullptr;

    QueryFieldNames names(fieldCount);
    for (uint32 i = 0; i < fieldCount; ++i)
        names[i] = PQfname(result, i);

    QueryResultPostgre* queryResult = new QueryResultPostgre(result, rowCount, fieldCount);

    queryResult->NextRow();
    return new QueryNamedResult(queryResult, names);
}

bool PostgreSQLConnection::Execute(const char* sql)
{
    if (!mPGconn)
        return false;

    uint32 _s = WorldTimer::getMSTime();

    PGresult* res = PQexec(mPGconn, sql);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        sLog.outErrorDb("SQL: %s", sql);
        sLog.outErrorDb("SQL %s", PQerrorMessage(mPGconn));
        return false;
    }
    else
    {
        DEBUG_FILTER_LOG(LOG_FILTER_SQL_TEXT, "[%u ms] SQL: %s", WorldTimer::getMSTimeDiff(_s, WorldTimer::getMSTime()), sql);
    }

    PQclear(res);
    return true;
}

bool PostgreSQLConnection::_TransactionCmd(const char* sql)
{
    if (!mPGconn)
        return false;

    PGresult* res = PQexec(mPGconn, sql);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        sLog.outError("SQL: %s", sql);
        sLog.outError("SQL ERROR: %s", PQerrorMessage(mPGconn));
        return false;
    }
    else
    {
        DEBUG_LOG("SQL: %s", sql);
    }
    return true;
}

bool PostgreSQLConnection::BeginTransaction()
{
    return _TransactionCmd("START TRANSACTION");
}

bool PostgreSQLConnection::CommitTransaction()
{
    return _TransactionCmd("COMMIT");
}

bool PostgreSQLConnection::RollbackTransaction()
{
    return _TransactionCmd("ROLLBACK");
}

unsigned long PostgreSQLConnection::escape_string(char* to, const char* from, unsigned long length)
{
    if (!mPGconn || !to || !from || !length)
        return 0;

    return PQescapeString(to, from, length);
}

#endif
