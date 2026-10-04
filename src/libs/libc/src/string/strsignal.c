#include <signal.h>
#include <stdio.h>
#include <string.h>

// Signal names for the Linux numbers this kernel delivers. Unknown numbers
// (including realtime) fall back to "Real-time signal"/"Unknown signal".
char *strsignal(int sig)
{
	static const char *const names[] = {
	    [1] = "Hangup",
	    [2] = "Interrupt",
	    [3] = "Quit",
	    [4] = "Illegal instruction",
	    [5] = "Trace/breakpoint trap",
	    [6] = "Aborted",
	    [7] = "Bus error",
	    [8] = "Floating point exception",
	    [9] = "Killed",
	    [10] = "User defined signal 1",
	    [11] = "Segmentation fault",
	    [12] = "User defined signal 2",
	    [13] = "Broken pipe",
	    [14] = "Alarm clock",
	    [15] = "Terminated",
	    [16] = "Stack fault",
	    [17] = "Child exited",
	    [18] = "Continued",
	    [19] = "Stopped (signal)",
	    [20] = "Stopped",
	    [21] = "Stopped (tty input)",
	    [22] = "Stopped (tty output)",
	    [23] = "Urgent I/O condition",
	    [24] = "CPU time limit exceeded",
	    [25] = "File size limit exceeded",
	    [26] = "Virtual timer expired",
	    [27] = "Profiling timer expired",
	    [28] = "Window changed",
	    [29] = "I/O possible",
	    [30] = "Power failure",
	    [31] = "Bad system call",
	};
	if (sig >= 35 && sig < _NSIG)
		return (char *)"Real-time signal";
	if (sig < 1 || sig > 31 || !names[sig])
		return (char *)"Unknown signal";
	return (char *)names[sig];
}
