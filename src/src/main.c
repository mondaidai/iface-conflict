#include "ifconf.h"
#include "signals.h"
#include <syslog.h>

enum {
	APP_OK = 0,
	APP_INITIALIZATION_FAILURE,
	APP_SYNC_FAILURE
};

int main(void)
{   
	Signal(SIGINT, handle_signal);
	Signal(SIGTERM, handle_signal);
	Signal(SIGHUP, handle_signal);
	Signal(SIGQUIT, handle_signal);

	openlog("iface-conflict", LOG_PID | LOG_CONS, LOG_DAEMON);
	int rc = APP_OK;
	
	uloop_init();
	
	struct ifconf_ctx ifconfctx = {0};

	ifconf_status_t ret = ifconf_initialize(&ifconfctx);
	if(ret != IFCONF_OK) {
		syslog(LOG_ERR, "Initialization failed: %s", ifconf_strerr(ret));
		rc = APP_INITIALIZATION_FAILURE;
		goto cleanup;
	}
	syslog(LOG_INFO, "Initialization success");
	if((ret = ifconf_sync(&ifconfctx)) != IFCONF_OK) {
		syslog(LOG_ERR, "Sync failed: %s", ifconf_strerr(ret));
		rc = APP_SYNC_FAILURE;
		goto cleanup;
	}
	uloop_run();

cleanup:
	ifconf_cleanup(&ifconfctx);
	uloop_done();
	syslog(rc ? LOG_ERR : LOG_INFO, "Exiting with code: %d", rc);
	closelog();
	return rc;
}