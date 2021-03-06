/*
 * Copyright (c) 2009 Jilles Tjoelker, et al
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Reads data from a child process via a pipe.
 */

#include "atheme-compat.h"
#include "datastream.h"

#ifndef _WIN32

struct testprocdata
{
	char dest[COMPAT_NICKLEN + 1];
	connection_t *pip;
};

static struct testprocdata procdata;

static void
testproc_recvqhandler(connection_t *cptr)
{
	char buf[BUFSIZE];
	int count;
	user_t *u;

	if (cptr != procdata.pip)
	{
		slog(LG_INFO, "testproc_recvqhandler(): called with unexpected fd %d", cptr->fd);
		return;
	}

	count = recvq_getline(cptr, buf, sizeof buf - 1);
	if (count <= 0)
		return;
	if (buf[count - 1] == '\n')
		count--;
	if (count > 0 && buf[count - 1] == '\r')
		count--;
	if (count == 0)
		buf[count++] = ' ';
	buf[count] = '\0';
	u = user_find(procdata.dest);
	if (u != NULL)
		notice(service_find("operserv")->me->nick, u->nick, "%s", buf);
}

static void
testproc_closehandler(connection_t *cptr)
{
	if (cptr != procdata.pip)
	{
		slog(LG_INFO, "testproc_closehandler(): called with unexpected fd %d", cptr->fd);
		return;
	}

	slog(LG_DEBUG, "testproc_closehandler(): fd %d closed", cptr->fd);
	procdata.pip = NULL;
}

static void
os_cmd_testproc(sourceinfo_t *si, int parc, char *parv[])
{
	int pipes[2];

	if (si->su == NULL)
	{
		command_fail(si, fault_noprivs, _("\2%s\2 can only be executed via IRC."), "TESTPROC");
		return;
	}

	if (procdata.pip != NULL)
	{
		command_fail(si, fault_toomany, "Another TESTPROC is still in progress");
		return;
	}

	if (pipe(pipes) == -1)
	{
		command_fail(si, fault_toomany, "Failed to create pipe");
		return;
	}
	switch (fork())
	{
		case -1:
			close(pipes[0]);
			close(pipes[1]);
			command_fail(si, fault_toomany, "Failed to fork");
			return;
		case 0:
			connection_close_all_fds();
			close(pipes[0]);
			dup2(pipes[1], 1);
			dup2(pipes[1], 2);
			close(pipes[1]);
			execl("/bin/sh", "sh", "-c", "echo hi; sleep 1; echo hi 2; sleep 0.5; echo hi 3; sleep 4; echo hi 4", (char *)NULL);
			(void)write(2, "Failed to exec /bin/sh\n", 23);
			_exit(255);
			break;
		default:
			close(pipes[1]);
			procdata.pip = connection_add("testproc pipe", pipes[0], 0, recvq_put, NULL);
			procdata.pip->recvq_handler = testproc_recvqhandler;
			procdata.pip->close_handler = testproc_closehandler;
			mowgli_strlcpy(procdata.dest, CLIENT_NAME(si->su), sizeof procdata.dest);
			break;
	}
}

static command_t os_testproc = {
	.name           = "TESTPROC",
	.desc           = N_("Does something with child processes."),
	.access         = AC_NONE,
	.maxparc        = 0,
	.cmd            = &os_cmd_testproc,
	.help           = { .path = "contrib/testproc" },
};

static void
mod_init(module_t *const restrict m)
{
	service_named_bind_command("operserv", &os_testproc);
}

static void
mod_deinit(const module_unload_intent_t intent)
{
	if (procdata.pip != NULL)
		connection_close_soon(procdata.pip);

	service_named_unbind_command("operserv", &os_testproc);
}

SIMPLE_DECLARE_MODULE_V1("contrib/os_testproc", MODULE_UNLOAD_CAPABILITY_OK)

#endif
