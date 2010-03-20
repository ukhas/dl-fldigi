#ifndef _EXTRA_H_
#define _EXTRA_H_
#include <string>

//jcoxon
void UpperCase(string& str)
{
	for(int i = 0; i < str.length(); i++)
	{
		str[i] = toupper(str[i]);
	}
	return;
}
//
#endif