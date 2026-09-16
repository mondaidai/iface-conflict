#include "signals.h"
#include <libubox/uloop.h>

void handle_signal(int signo) {
	(void)signo;
	uloop_end();
}

Sigfunc * Signal(int signo, Sigfunc *func)
{
	struct sigaction act, oact;

	act.sa_handler = func;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

#ifdef SA_RESTART
	act.sa_flags |= SA_RESTART;
#endif

	if (sigaction(signo, &act, &oact) < 0)
		return SIG_ERR;

	return oact.sa_handler;
}
