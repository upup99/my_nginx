#pragma once
#ifndef __NGX_CONF_H__
#define __NGX_CONF_H__

#include <vector>

#include "ngx_global.h"

class CConfig
{
private:
	CConfig();
	static CConfig* m_instance;

public:
	~CConfig();
	static CConfig* GetInstance()
	{
		if (m_instance == nullptr)
		{
			// 锁
			if (m_instance == nullptr)
			{
				m_instance = new CConfig();
				static CGarhuishou cl;
			}
			// 释放锁
		}
		return m_instance;
	}

	class CGarhuishou // 类中套类，用于释放对象
	{
	public:
		~CGarhuishou()
		{
			if (CConfig::m_instance)
			{
				delete CConfig::m_instance;
				CConfig::m_instance = nullptr;
			}
		}
	};

//--------------------------------------------------
public:
	bool Load(const char* pconfName);
	const char* GetString(const char* p_itemname);
	int GetIntDefault(const char* p_item, const int def);

public:
	std::vector<LPCConfItem> m_ConfigItemList; // 存储配置信息的列表
};

#endif