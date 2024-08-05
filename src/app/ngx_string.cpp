//一些和字符串处理相关的函数

#include <stdio.h>
#include <string>

//截取字符串首部和尾部空格
void Trim(char* strc)
{
	std::string str(strc);
	int idx = str.find_first_not_of(' ');
	if (idx != -1)
	{
		str = str.substr(idx, str.size() - idx);
	}
	idx = str.find_last_not_of(' ');
	if (idx != -1)
	{
		str = str.substr(0, idx + 1);
	}
	const char* p_tmp = str.c_str();
	while (*p_tmp != '\0')
	{
		*strc = *p_tmp;
		p_tmp++;
		*strc++;
	}
	*strc = '\0';
}
