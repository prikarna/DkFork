/*+
	Author     : Deka Prikarna A
	Contact    : prikarna@gmail.com
	Subject    : DkFork
	Status     : Experimental
	Desc.      : Simple implementation of POSIX fork() function.

	Remark:
		This function, DkFork(), is one of POSIX fork() implementation by using Windows API.
		This is not a really fork() but close to it and may behave like one. Basicly this done
		by "redirect" child process to certaint "execution point" or "execution state" as the 
		parent dictate. This is possible with the use of debug library (DbgHelp.dll) which is 
		available in Windows system. So the parent process do debug its child process. Parent 
		process and child process are from the same image then it can be said that the process 
		is debugging itself, strange huh ?!
		Parent process create child process with CreateProcess() with debug options enabled and 
		then start debug the child process. At the first break point, it set another break point to 
		main function that is program entry point written by programmer. At second break point it 
		then copy stack frame from parent to child process and then set thread context of child 
		process. 
		Note that i maybe use a bug (or a feature) in DbgHelp.dll, because the library seems to 
		be used to debug a process, not to be used like this (out-of-context usage). And in the 
		future a usage like this may not available. If you find out that this codes (or some 
		portion of it) don't make any sense to you, well then it maybe the bug. 
		DkFork comes with some limitations:
		- You must disable optimization option in the compiler (use /Od options) for the entire 
		  codes, this because StackWalk64() that i use to get start and end of stack frame seems 
		  don't work well on optimized code (i use DbgHelp.dll with version of 5.1, probably higher 
		  version of this library may work on optimized code), and i don't want to write a StackWalk() 
		  like function. After all this is just an idea.
		- May not work on ASLR (Address Space Load Randomization) environment, i assume parent load 
		  address space is the same as child load address space. Load randomization can be disabled 
		  in linker option and Windows should respect this "sign". Windows XP does not have ASLR so
		  this "sign" maybe ignore.
		- Can not carry out process inheritance mechanism of C run-time library, for example: file 
		  descriptor returned by _pipe() do not inherit to child process but Windows API process 
		  inheritance will work. Seems that run-time library has their own process inheritance
		  management.
	    - May conflict with exception of child process, because this function use exception as part 
		  of fork mechanism.
	    - Do not support multi thread.
		- For now it can only support Microsoft Windows XP 32 bit on Intel processor.
		- And maybe other limitations that i couldn't think of right now.
-*/

#include "TChar.h"
#include "Windows.h"
#include "StrSafe.h"
#include "PsApi.h"
#include "DbgHelp.h"

#pragma comment(lib, "PsApi.lib")
#pragma comment(lib, "DbgHelp.lib")

/*+ 
 * I don't know what the name of this exception, so for now just call it
 * DKFRK_VS_DEBUG_EXCEPTION, this exception occur when a project in Visual
 * Studio is set to debug target
-*/
#define DKFRK_VS_DEBUG_EXCEPTION				0x406D1388

/*+
 *	Some debugging function, just send message to debugger
-*/
#ifdef _DEBUG
static void DkOutDbg(const char* szSrc, const char* szMsg, DWORD dwErr)
{
	UCHAR		buf[1024] = {0};
	HRESULT		hr = S_OK;

	if (dwErr != 0) {
		hr = StringCbPrintfA(buf, sizeof(buf), "%s: %s (Code=0x%X)\r\n", szSrc, szMsg, dwErr);
	} else {
		hr = StringCbPrintfA(buf, sizeof(buf), "%s: %s\r\n", szSrc, szMsg);
	}

	if (hr == S_OK) 
		OutputDebugStringA(buf);
}
# define DK_DBG(Src, Msg, Err)		DkOutDbg(Src, Msg, Err)
#else
# define DK_DBG(Src, Msg, Err)
#endif

static DWORD						gdwMainFuncAddr;
static TCHAR						gSzFullImgName[512];
static HANDLE						ghParProc;
static DEBUG_EVENT					gDbgEvt;
static CREATE_PROCESS_DEBUG_INFO	gProcDbgInf;
static BOOL							gfFirstBreakpoint;
static ULONG64						gulStartBaseFrameAddr;
static ULONG64						gulEndBaseFrameAddr;
static BOOL							gfDetachChild;

static void InitStaticVars();
static BOOL CreateProcDbgEvtHandler();
static BOOL ExcDbgEvtHandler();
static BOOL BreakpointExcHandler();
static BOOL GetStartAndEndFrame();
static int ChildForkProc();

/*+
 *	DkFork function take a parameter, that is main funtion address of
 *	the whole program. Return -1 on error otherwise return child process id
 *	on parent process.
-*/
int DkFork(long long lMainProgAddr)
{
	BOOL				fRes = FALSE, fDbgOK = TRUE;
	DWORD				dwRes = 0;
	STARTUPINFO			si = {0};
	PROCESS_INFORMATION	pi = {0};
	SECURITY_ATTRIBUTES	sa = {0};
	TCHAR				szCmd[512] = {0};

	gdwMainFuncAddr = (DWORD) lMainProgAddr;
	InitStaticVars();

	ghParProc = GetCurrentProcess();
	dwRes = GetModuleFileNameEx(
								ghParProc, 
								NULL, 
								gSzFullImgName, 
								sizeof(gSzFullImgName)/sizeof(TCHAR)
								);
	if (dwRes <= 0) return -1;

	fRes = GetStartAndEndFrame();
	if (!fRes) return -1;

	StringCbCopy(szCmd, sizeof(szCmd), gSzFullImgName);
	si.cb = sizeof(STARTUPINFO);
	sa.bInheritHandle = TRUE;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	fRes = CreateProcess(
						 NULL,
						 szCmd,
						 &sa,
						 &sa,
						 TRUE,						// Enable process inheritance
						 DEBUG_ONLY_THIS_PROCESS,	// Enable child to be debugged
						 NULL,
						 NULL,
						 &si,
						 &pi
						 );
	if (!fRes) return -1;

	/*
	 *	Listening to debug events and response with appropriate handler
	 */
	do {
		fRes = WaitForDebugEvent(&gDbgEvt, INFINITE);
		if (fRes) {
			switch (gDbgEvt.dwDebugEventCode)
			{
			case CREATE_PROCESS_DEBUG_EVENT:
				fRes = CreateProcDbgEvtHandler();
				if (!fRes) {
					fDbgOK = FALSE;
				}
				break;

			case EXCEPTION_DEBUG_EVENT:
				fRes = ExcDbgEvtHandler();
				if (!fRes) {
					fDbgOK = FALSE;
				}
				break;

			case EXIT_PROCESS_DEBUG_EVENT:
				fDbgOK = FALSE;
				fRes = FALSE;
				break;

			default:
				break;
			}

			if (fRes) {
				ContinueDebugEvent(pi.dwProcessId, pi.dwThreadId, DBG_CONTINUE);
				if (gfDetachChild) break;
			}
		}

	} while (fRes);

	fRes = DebugActiveProcessStop(pi.dwProcessId);

	if (!fDbgOK) return -1;
	
	return (int) pi.dwProcessId;
}

/*+
 *	Initialization function
-*/
static void InitStaticVars()
{
	RtlZeroMemory(gSzFullImgName, sizeof(gSzFullImgName));
	ghParProc = NULL;
	RtlZeroMemory(&gDbgEvt, sizeof(DEBUG_EVENT));
	RtlZeroMemory(&gProcDbgInf, sizeof(CREATE_PROCESS_DEBUG_INFO));
	gfFirstBreakpoint = FALSE;
	gulEndBaseFrameAddr = 0;
	gulStartBaseFrameAddr = 0;
	gfDetachChild = FALSE;
}

/*+
 *	Get the start and end of stack frame, from entry point function to DkFork function. 
 *	Note that RtlCaptureContext() is called in this function, because if we call it 
 *	in DkFork() the result would be useless in this case. It don't capture stack frame 
 *	of DkFork(). 
 *	A stack frame in i386 should contain: function parameters, return address of
 *	callee, content of processor registers, base frame address and local variables. 
 *	It depends on how compiler compile the codes.
-*/
static BOOL GetStartAndEndFrame()
{
	DWORD				dwRes = 0;
	BOOL				fRes = FALSE, fStackWalkOK = TRUE;
	DWORD64				dwMod = 0, dwDisp = 0;
	STACKFRAME64		StackFrame = {0};
	HANDLE				hCurThread = NULL;
	CONTEXT				Ctx = {0};
	int					iCount = 0;

	Ctx.ContextFlags = CONTEXT_ALL;
	RtlCaptureContext(&Ctx);

	hCurThread = GetCurrentThread();
	StackFrame.AddrFrame.Mode	= AddrModeFlat;
	StackFrame.AddrFrame.Offset	= Ctx.Ebp;
	StackFrame.AddrPC.Mode		= AddrModeFlat;
	StackFrame.AddrPC.Offset	= Ctx.Eip;
	StackFrame.AddrStack.Mode	= AddrModeFlat;
	StackFrame.AddrStack.Offset	= Ctx.Esp;
	do {
		fRes = StackWalk64(
						   IMAGE_FILE_MACHINE_I386,
						   ghParProc,
						   hCurThread,
						   &StackFrame,
						   NULL,
						   NULL,
						   NULL,
						   NULL,
						   NULL
						   );
		if (iCount > 64) {				// For now i limit stack walk only 64 frames (for performance reason)
			fStackWalkOK = FALSE;
			break;
		}
		if (fRes) {
			if (iCount == 0) {
				gulEndBaseFrameAddr = StackFrame.AddrFrame.Offset;
			} else {
				gulStartBaseFrameAddr = StackFrame.AddrFrame.Offset;
			}
			iCount += 1;
		}
	} while (fRes);

	if (fStackWalkOK) fRes = TRUE;

	return fRes;
}

/*+
 *	Handling a create process debug event.
 *	This function copy .data section in parent process to its child. 
 *	Microsoft C/C++ compiler seems use .data section as storage of global
 *	variables.
-*/
static BOOL CreateProcDbgEvtHandler()
{
	BOOL					fRes = FALSE;
	DWORD					dwRes = 0;
	PIMAGE_NT_HEADERS		pNtHdr = NULL;
	PIMAGE_SECTION_HEADER	pSecHdr = NULL;
	DWORD64					dwAddr = 0, dwSize = 0;
	SIZE_T					stRet = 0;

	RtlCopyMemory(&gProcDbgInf, &(gDbgEvt.u.CreateProcessInfo), sizeof(CREATE_PROCESS_DEBUG_INFO));

	fRes = DebugSetProcessKillOnExit(FALSE);
	if (!fRes) return FALSE;

	pNtHdr = ImageNtHeader(gProcDbgInf.lpBaseOfImage);
	if (!pNtHdr) return FALSE;
	pSecHdr = (PIMAGE_SECTION_HEADER) IMAGE_FIRST_SECTION(pNtHdr);
	if (!pSecHdr) return FALSE;
	for (dwRes = 0; dwRes < pNtHdr->FileHeader.NumberOfSections; dwRes++) 
	{
		if (lstrcmpA(".data", (char *) pSecHdr[dwRes].Name) == 0) {
			fRes = TRUE;
			break;
		}
	}
	if (!fRes) return FALSE;

	dwAddr = (DWORD64) ((DWORD64) gProcDbgInf.lpBaseOfImage + pSecHdr[dwRes].VirtualAddress);
	dwSize = (DWORD64) pSecHdr[dwRes].Misc.VirtualSize;
	fRes = WriteProcessMemory(
							  gProcDbgInf.hProcess,
							  (LPVOID) dwAddr,
							  (LPCVOID) dwAddr,
							  (SIZE_T) dwSize,
							  &stRet
							  );
	if  (fRes) {
		if (stRet != (SIZE_T) dwSize) {
			DK_DBG(__FUNCTION__, "Error write result less than expected value!", 0);
			fRes = FALSE;
		}
	} else {
		DK_DBG(__FUNCTION__, "Error WriteProcessMemory()!", GetLastError());
	}

	if (!fRes) {
		TerminateProcess(gProcDbgInf.hProcess, -1);
	}

	return fRes;
}

/*+
 *	Exception debug event handler.
 *	We only interested in break point exception, other exceptions except 
 *	"visual studio exception" simply terminate child process. Visual studio 
 *	exception occur only when debug target is set. It maybe used internally
 *	by "debug mechanism" in visual studio.
-*/
static BOOL ExcDbgEvtHandler()
{
	DWORD		dwExcCode = gDbgEvt.u.Exception.ExceptionRecord.ExceptionCode;
	PVOID		pExcAddr = gDbgEvt.u.Exception.ExceptionRecord.ExceptionAddress;
	BOOL		fRes = FALSE;

	switch (dwExcCode)
	{
	case EXCEPTION_BREAKPOINT:
		fRes = BreakpointExcHandler();
		if (!fRes) {
			TerminateProcess(gProcDbgInf.hProcess, -1);
		}
		break;

	case EXCEPTION_ACCESS_VIOLATION:
		DK_DBG(__FUNCTION__, "EXCEPTION_ACCESS_VIOLATION", 0);
		TerminateProcess(gProcDbgInf.hProcess, EXCEPTION_ACCESS_VIOLATION);
		break;

	case EXCEPTION_ILLEGAL_INSTRUCTION:
		DK_DBG(__FUNCTION__, "EXCEPTION_ILLEGAL_INSTRUCTION", 0);
		TerminateProcess(gProcDbgInf.hProcess, EXCEPTION_ILLEGAL_INSTRUCTION);
		break;
		
	case DKFRK_VS_DEBUG_EXCEPTION:
		DK_DBG(__FUNCTION__, "EXCEPTION_VISUAL_STUDIO_DEBUG", 0);
		fRes = TRUE;
		break;

	default:
		DK_DBG(__FUNCTION__, "Unknown exception!", dwExcCode);
		TerminateProcess(gProcDbgInf.hProcess, (UINT) dwExcCode);
		break;
	}

	return fRes;
}

/*+
 *	Break point exception debug event handler.
 *	First break point is generated when we create child process with debug option
 *	enabled. I am not sure who's generate this break point, it may comming from 
 *	Windows system (as a part of debugging mechanism) or the code "implanted" by 
 *	compiler in this code. Note that compiler "implant" some codes to our code when 
 *	it generate binary, well, maybe "implant" some security code or maybe just to be 
 *	recognized by Windows system. But beside that this first exception address 
 *	is not the address of main function address as written by programmer, and that 
 *	for sure. This first break point exception give us a chance set up a break point 
 *	to main function by writing 0xCC (INT3 Intel processor instructions) to main 
 *	function address. Note that child process must execute some "implanted" codes 
 *	(codes before main function) "naturaly" until it reach main function address, 
 *	if not Windows system or another "implanted" codes (not sure which one) will 
 *	complaint that this application is not properly initialized.
 *	At second break point, we have previously set, we copy stack frames from parent 
 *	to child process, a "blind copy", and then setup child thread context to be same 
 *	as parent process when it reach DkFork() function except Eip. Eip correspond to 
 *	EIP register in Intel processor, and we set this to the address of ChildForkProc(). 
 *	This will enforce child process to "jump" to ChildForkProc() function so child 
 *	process will execute ChildForkProc() rather than DkFork().
-*/
static BOOL BreakpointExcHandler()
{
	BOOL		fRes = FALSE;
	UCHAR		uInt3 = 0xCC;		// INT3 (Processor break point instruction)
	CONTEXT		Ctx = {0};

	if (!gfFirstBreakpoint) {
		DK_DBG(__FUNCTION__, "First break point!", 0);
		gfFirstBreakpoint = TRUE;
		fRes = WriteProcessMemory(
								  gProcDbgInf.hProcess,
								  (LPVOID) gdwMainFuncAddr,
								  (LPCVOID) &uInt3,
								  1,
								  NULL
								  );
	} else {
		DK_DBG(__FUNCTION__, "Main function break point has been reached!", 0);
		fRes = WriteProcessMemory(
								  gProcDbgInf.hProcess,
								  (LPVOID) gulEndBaseFrameAddr,
								  (LPCVOID) gulEndBaseFrameAddr,
								  (SIZE_T) (gulStartBaseFrameAddr - gulEndBaseFrameAddr),
								  NULL
								  );
		if (fRes) {
			Ctx.ContextFlags = CONTEXT_CONTROL;
			fRes = GetThreadContext(gProcDbgInf.hThread, &Ctx);
			if (fRes) {
				Ctx.Eip = (DWORD) &ChildForkProc;
				Ctx.Esp = (DWORD) gulEndBaseFrameAddr;
				fRes = SetThreadContext(gProcDbgInf.hThread, &Ctx);
				if (fRes) {
					gfDetachChild = TRUE;
				}
			}
		}
	}

	return fRes;
}

/*+
 *	This function is executed by child, and return 0.
 *	Because we've already copy and setup stack frames for child process, this function 
 *	don't need a "prolog" thus we need to implement a function without "prolog".
 *	This can be done through the naked function. In this function we set EAX
 *	processor register to 0 (Microsoft C/C++ compiler use EAX register as 
 *	a storage of return value), pop stack value to EBP register (restore stack 
 *	frame so the caller can use its stack frame) and then pop stack value again 
 *	(this value is return address of the callee) and then "jump" to it.
 *	If you want to know the detail of CALL and RET mechanism and also "stack mechanism", 
 *	please see the Intel processor manual.
-*/
__declspec(naked) int ChildForkProc()
{
	__asm {
		mov		eax, 0
		pop		ebp
		ret
	}
}
