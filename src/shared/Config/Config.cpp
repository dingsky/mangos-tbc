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

#include "Config.h"
#include "Policies/Singleton.h"
#include <mutex>

#include <boost/algorithm/string.hpp>

#include <unordered_map>
#include <string>
#include <fstream>

INSTANTIATE_SINGLETON_1(Config);

bool Config::SetSource(const std::string& file)
{
    //设置配置文件名称
    m_filename = file;

    //重新加载配置文件
    return Reload();
}

bool Config::Reload()
{
    //打开配置文件
    std::ifstream in(m_filename, std::ifstream::in);

    if (in.fail())
        return false;

    //定义map和互斥量
    std::unordered_map<std::string, std::string> newEntries;
    std::lock_guard<std::mutex> guard(m_configLock);

    //逐行处理文件内容
    do
    {
        //获取一行
        std::string line;
        std::getline(in, line);

        //去除左空格
        boost::algorithm::trim_left(line);

        //大小为0则重来
        if (!line.length())
            continue;

        //如果行首为#或[则忽略该行
        if (line[0] == '#' || line[0] == '[')
            continue;

        //寻找等号的位置
        auto const equals = line.find('=');
        if (equals == std::string::npos)    //找不到则报失败
            return false;
        
        //拷贝Key, 从0到等号前一位, Key内容转小写, 内容去空格
        auto const entry = boost::algorithm::trim_copy(boost::algorithm::to_lower_copy(line.substr(0, equals)));
        
        //拷贝内容
        auto const value = boost::algorithm::trim_copy_if(boost::algorithm::trim_copy(line.substr(equals + 1)), boost::algorithm::is_any_of("\""));

        //把Key=Val添加到map
        newEntries[entry] = value;
    }
    while (in.good());

    //拷贝map到成员变量
    m_entries = std::move(newEntries);

    return true;
}

bool Config::IsSet(const std::string& name) const
{
    auto const nameLower = boost::algorithm::to_lower_copy(name);
    return m_entries.find(nameLower) != m_entries.cend();
}

const std::string Config::GetStringDefault(const std::string& name, const std::string& def) const
{
    auto const nameLower = boost::algorithm::to_lower_copy(name);

    auto const entry = m_entries.find(nameLower);

    return entry == m_entries.cend() ? def : entry->second;
}

bool Config::GetBoolDefault(const std::string& name, bool def) const
{
    auto const value = GetStringDefault(name, def ? "true" : "false");

    std::string valueLower;
    std::transform(value.cbegin(), value.cend(), std::back_inserter(valueLower), ::tolower);

    return valueLower == "true" || valueLower == "1" || valueLower == "yes";
}

int32 Config::GetIntDefault(const std::string& name, int32 def) const
{
    auto const value = GetStringDefault(name, std::to_string(def));

    return std::stoi(value);
}

float Config::GetFloatDefault(const std::string& name, float def) const
{
    auto const value = GetStringDefault(name, std::to_string(def));

    return std::stof(value);
}

