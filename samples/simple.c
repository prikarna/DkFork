/*+
	Simple sample demonstrate DkFork()

	Compile: cl /Od simple.c ..\src\DkFork.c /link /DYNAMICBASE:NO
-*/

#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <io.h>

extern int DkFork(long long lMainAddr);

int main(int argc, char* argv[])
{
	int		i = 0;

	printf("(PID=%d) Simple program demonstrate DkFork().\r\n", _getpid());

	i = DkFork((long long) &main);
	if (i == -1) {
		// Error if DkFork() return -1
		printf("(PID=%d) Error DkFork()\r\n", _getpid());
		return -1;
	}
	if (i == 0) {
		// If DkFork() return 0, this is child process
		printf("(PID=%d) This is child process!\r\n", _getpid());
		return 0;
	} else {
		// If not then this must be parent process
		printf("(PID=%d) This is parent proces!\r\n", _getpid());
	}

	return 0;
}