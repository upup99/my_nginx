//处理系统配置文件相关的

//系统头文件放上边
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <vector>

//自定义头文件放下边,因为g++中用了-I参数，所以这里用<>也可以
#include "ngx_func.h"     //函数声明
#include "ngx_c_conf.h"   //和配置文件处理相关的类,名字带c_表示和类有关

//静态成员赋值
CConfig* CConfig::m_instance = NULL;

//构造函数
CConfig::CConfig()
{

}

//析构函数
CConfig::~CConfig()
{
    std::vector<LPCConfItem>::iterator pos;
    for (pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
    {
        delete *pos;
    }//end for
    // delete m_instance;
    m_ConfigItemList.clear();
    return;
}

//装载配置文件
bool CConfig::Load(const char* pconfName)
{
    FILE* pf = fopen(pconfName, "r");
    if (nullptr == pf)
    {
        return false;
    }
    char linebuf[512] = { 0 };
    while (!feof(pf))
    {
        if (fgets(linebuf, 511, pf) == nullptr) // 最后有一个结束符'\0'
        {
            continue;
        }
        if (linebuf[0] == 0)
        {
            continue;
        }

        // 处理注释行
        if (*linebuf == ';' || *linebuf == ' ' || *linebuf == '#' || *linebuf == '\t' || *linebuf == '\n')
        {
            continue;
        }
        // 去掉字符串末尾的换行，回车，空格
        while (strlen(linebuf) > 0 &&
            (linebuf[strlen(linebuf) - 1] == 10 ||
                linebuf[strlen(linebuf) - 1] == 13 ||
                linebuf[strlen(linebuf) - 1] == 32))
        {
            linebuf[strlen(linebuf) - 1] = '\0';  // 替换为字符串终止符
        }

        if (linebuf[0] == 0 || *linebuf == '[')
        {
            continue;
        }

        char* pTmp = strchr(linebuf, '=');
        if (pTmp != NULL)
        {
            LPCConfItem p_confitem = new CConfItem;
            memset(p_confitem, 0, sizeof(CConfItem));
            //等号左侧的拷贝到p_confitem->ItemName
            strncpy(p_confitem->ItemName, linebuf, (int)(pTmp - linebuf));
            //等号右侧的拷贝到p_confitem->ItemContent
            strcpy(p_confitem->ItemContent, pTmp + 1);
            Trim(p_confitem->ItemContent);
            Trim(p_confitem->ItemName);

            m_ConfigItemList.push_back(p_confitem);
        }
    }
    fclose(pf);
    return true;
}


//根据ItemName获取配置信息字符串，不修改不用互斥
const char* CConfig::GetString(const char* p_itemname)
{
    std::vector<LPCConfItem>::iterator pos;
    for (pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
    {
        if (strcasecmp((*pos)->ItemName, p_itemname) == 0)
            return (*pos)->ItemContent;
    }//end for
    return NULL;
}
//根据ItemName获取数字类型配置信息，不修改不用互斥
int CConfig::GetIntDefault(const char* p_itemname, const int def)
{
    std::vector<LPCConfItem>::iterator pos;
    for (pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
    {
        if (strcasecmp((*pos)->ItemName, p_itemname) == 0)
            return atoi((*pos)->ItemContent);
    }//end for
    return def;
}



