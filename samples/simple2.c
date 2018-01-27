/*+
	Simple sample demonstrate DkFork() with global and local variable
	Notice that child process will see the changes of local and global variables 
	changed by parent process before call DkFork(). You can play arround to change 
	local and/or global variables and then check if child process see this change.
	
	Compile: cl /Od simple2.c ..\src\DkFork.c /link /DYNAMICBASE:NO
-*/

#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <io.h>

extern int DkFork(long long lMainAddr);

int		i_test = 0;
static int	i_stat_test = 0;

int sim_add(int a, int b)
{
	return (a + b);
}

int main(int argc, char* argv[])
{
	int		i = 0, c = 0;
	static int i_stat = 0xDEAFBEE;

	printf("(PID=%d) Simple program demonstrate DkFork().\r\n", _getpid());
	c = sim_add(7, 8);
	i_test = 0xBADF00D;
	i_stat_test = 0x5EAF00D;

	i = DkFork((long long) &main);
	if (i == -1) {
		printf("(PID=%d) Error DkFork()\r\n", _getpid());
		return -1;
	}
	if (i == 0) {
		printf("(PID=%d) This is child process!, c=%d, i_test=0x%X, i_stat_test=0x%X, i_stat=0x%X\r\n", 
			_getpid(), c, i_test, i_stat_test, i_stat);
		return 0;
	} else {
		printf("(PID=%d) This is parent process!, c=%d, i_test=0x%X, i_stat_test=0x%X, i_stat=0x%X\r\n", 
			_getpid(), c, i_test, i_stat_test, i_stat);
	}

	return 0;
}