/*+
	Simple sample demonstrate DkFork() with "cascaded functions"
	Notice that child process will return as parent return

	Compile: cl /Od simple3.c ..\src\DkFork.c /link /DYNAMICBASE:NO
-*/

#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <io.h>

extern int DkFork(long long lMainAddr);

int		i_test = 0;

int dummy_func0();
void dummy_func1(int b);

int main(int argc, char* argv[])
{
	int		i = 555;

	printf("(PID=%d) Simple program demonstrate DkFork().\r\n", _getpid());
	
	i_test = 0xBEBADA55;

	dummy_func1(i);

	printf("(PID=%d) Program end. (i=%d, i_test=0x%X)\r\n", _getpid(), i, i_test);

	return 0;
}

int dummy_func0()
{
	int		i = 0;

	printf("(PID=%d) %s begin...\r\n", _getpid(), __FUNCTION__);
	i = DkFork((long long) &main);
	if (i == -1) {
		printf("(PID=%d) Error DkFork()\r\n", _getpid());
	} else if (i == 0) {
		printf("(PID=%d) Child: %s end.\r\n", _getpid(), __FUNCTION__);
	} else {
		printf("(PID=%d) Parent: %s end.\r\n", _getpid(), __FUNCTION__);
	}

	return i;
}

void dummy_func1(int b)
{
	int i = 0;

	printf("(PID=%d) %s begin... (b=%d)\r\n", _getpid(), __FUNCTION__, b);
	i = dummy_func0();
	if (i == -1) {
		printf("(PID=%d) Error %s\r\n", _getpid(), __FUNCTION__);
	} else if (i == 0) {
		printf("(PID=%d) Child: %s end. (b=%d)\r\n", _getpid(), __FUNCTION__, b);
	} else {
		printf("(PID=%d) Parent: %s end. (b=%d)\r\n", _getpid(), __FUNCTION__, b);
	}
}