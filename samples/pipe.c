/*+
	Simple sample demonstrate DkFork() with pipe.
	DkFork() can't carry out C run-time library process inheritance but can carry out 
	Windows API process inheritance so this sample use CreatePipe() of Windows API 
	instead of _pipe() of C run-time library. CreatePipe() call must enable option of inherit 
	in the security attributes parameter, if not handles returned by CreatePipe() are 
	not inherited. And also, handles returned by CreatePipe() can be accessed through out 
	ReadFile() and WriteFile() Windows API, not _read() or _write() of C run-time library.
	Notice that this sample don't protect a dead lock that may occur when WriteFile() in 
	child process returned an error.

	Compile: cl /Od pipe.c ..\src\DkFork.c /link /DYNAMICBASE:NO
-*/

#define MAX_PIPE_BUF_SIZE			512

#include <stdio.h>
#include <stdlib.h>
#include <process.h>

#include "Windows.h"

extern int DkFork(long long lMainAddr);

int main(int argc, char* argv[])
{
	int						i = 0;
	HANDLE					hIn = NULL, hOut = NULL;
	BOOL					fRes = FALSE;
	SECURITY_ATTRIBUTES		sa = {0};
	DWORD					dwRes = 0;
	char					buf[MAX_PIPE_BUF_SIZE] = {0};

	printf("(PID=%d) Simple program demonstrate DkFork() and pipe.\r\n", _getpid());

	sa.bInheritHandle = TRUE;					// Enable handle inheritance
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);	// Size of security attributes
	fRes = CreatePipe(&hIn, &hOut, &sa, MAX_PIPE_BUF_SIZE);
	if (!fRes) {
		printf("(PID=%d) Error CreatePipe()! (%d)\r\n", _getpid(), GetLastError());
		return -1;
	}

	i = DkFork((long long) &main);
	if (i == -1) {
		printf("(PID=%d) Error DkFork()\r\n", _getpid());
		return -1;
	}
	if (i == 0) {
		// Child process is the "pipe writer"
		printf("(PID=%d) This is child process!\r\n", _getpid());
		sprintf_s(buf, sizeof(buf), "This is comming from child.");
		fRes = WriteFile(hOut, (LPCVOID) &buf[0], MAX_PIPE_BUF_SIZE, &dwRes, NULL);
		if (!fRes) {
			printf("(PID=%d) Error write to pipe! (%d)\r\n", _getpid(), GetLastError());
		} else {
			printf("(PID=%d) Success write to pipe.\r\n", _getpid());
		}
		fRes = CloseHandle(hOut);
		fRes = CloseHandle(hIn);
		return 0;
	} else {
		// Parent process is the "pipe reader"
		printf("(PID=%d) This is parent proces!\r\n", _getpid());
		fRes = ReadFile(hIn, (LPVOID) &buf[0], MAX_PIPE_BUF_SIZE, &dwRes, NULL);
		if (!fRes) {
			printf("(PID=%d) Error read from pipe! (%d)\r\n", GetLastError());
		} else {
			printf("(PID=%d) Success read from file, receive %d bytes.\r\n", _getpid(), dwRes);
			if (dwRes > 0) {
				printf("(PID=%d) Data from child: %s\r\n", _getpid(), buf);
			}
		}
		fRes = CloseHandle(hIn);
		fRes = CloseHandle(hOut);
	}

	return 0;
}