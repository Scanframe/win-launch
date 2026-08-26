#include "../main.h"
#include <stdio.h>

#ifndef SF_NO_MAIN
	#error Cannot have a double entry point.
#endif

int main(int argc, char* argv[])
{
	int fails = 0;
	//
	if (!isAbsolutePath("C:\\my-dir\\sub"))
		fails++;
	//
	if (isAbsolutePath("\\my-dir\\sub"))
		fails++;
	//
	if (isAbsolutePath("my-dir\\sub"))
		fails++;
	//
	return fails > 0 ? 1 : 0;
}