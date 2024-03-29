/*
** Zabbix
** Copyright (C) 2001-2013 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"
#include "sysinfo.h"
#include "zbxregexp.h"

#include <sys/sysctl.h>

#define DO_SUM 0
#define DO_MAX 1
#define DO_MIN 2
#define DO_AVG 3

#define ZBX_PROC_STAT_ALL 0
#define ZBX_PROC_STAT_RUN 1
#define ZBX_PROC_STAT_SLEEP 2
#define ZBX_PROC_STAT_ZOMB 3

static kvm_t	*kd = NULL;

static char	*proc_argv(pid_t pid)
{
	size_t		sz = 0;
	int		mib[4], ret;
	int		i, len;
	static char	*argv = NULL;
	static size_t	argv_alloc = 0;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC_ARGS;
	mib[2] = (int)pid;
	mib[3] = KERN_PROC_ARGV;

	if (0 != sysctl(mib, 4, NULL, &sz, NULL, 0))
		return NULL;

	if (argv_alloc < sz)
	{
		argv_alloc = sz;
		if (NULL == argv)
			argv = zbx_malloc(argv, argv_alloc);
		else
			argv = zbx_realloc(argv, argv_alloc);
	}

	sz = argv_alloc;
	if (0 != sysctl(mib, 4, argv, &sz, NULL, 0))
		return NULL;

	for (i = 0; i < (int)(sz - 1); i++ )
		if (argv[i] == '\0')
			argv[i] = ' ';

	return argv;
}

int     PROC_MEM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*procname, *proccomm, *param, *args;
	int	do_task, pagesize, count, i,
		proc_ok, comm_ok,
		op, arg;

	double	value = 0.0,
		memsize = 0;
	int	proccount = 0;

	size_t	sz;

	struct kinfo_proc2	*proc, *pproc;
	struct passwd		*usrinfo;

	if (4 < request->nparam)
		return SYSINFO_RET_FAIL;

	procname = get_rparam(request, 0);
	param = get_rparam(request, 1);

	if (NULL != param && '\0' != *param)
	{
		if (NULL == (usrinfo = getpwnam(param)))	/* incorrect user name */
			return SYSINFO_RET_FAIL;
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "sum"))
		do_task = DO_SUM;
	else if (0 == strcmp(param, "avg"))
		do_task = DO_AVG;
	else if (0 == strcmp(param, "max"))
		do_task = DO_MAX;
	else if (0 == strcmp(param, "min"))
		do_task = DO_MIN;
	else
		return SYSINFO_RET_FAIL;

	proccomm = get_rparam(request, 3);

	pagesize = getpagesize();

	if (NULL == kd && NULL == (kd = kvm_open(NULL, NULL, NULL, KVM_NO_FILES, NULL)))
		return SYSINFO_RET_FAIL;

	if (NULL != usrinfo)
	{
		op = KERN_PROC_UID;
		arg = (int)usrinfo->pw_uid;
	}
	else
	{
		op = KERN_PROC_ALL;
		arg = 0;
	}

	if (NULL == (proc = kvm_getproc2(kd, op, arg, sizeof(struct kinfo_proc2), &count)))
		return SYSINFO_RET_FAIL;

	for (pproc = proc, i = 0; i < count; pproc++, i++)
	{
		proc_ok = 0;
		comm_ok = 0;

		if (NULL == procname || '\0' == *procname || 0 == strcmp(procname, pproc->p_comm))
			proc_ok = 1;

		if (NULL != proccomm && '\0' != *proccomm)
		{
			if (NULL != (args = proc_argv(pproc->p_pid)))
			{
				if (NULL != zbx_regexp_match(args, proccomm, NULL))
					comm_ok = 1;
			}
		}
		else
			comm_ok = 1;

		if (proc_ok && comm_ok)
		{
			value = pproc->p_vm_tsize
				+ pproc->p_vm_dsize
				+ pproc->p_vm_ssize;
			value *= pagesize;

			if (0 == proccount++)
				memsize = value;
			else
			{
				if (do_task == DO_MAX)
					memsize = MAX(memsize, value);
				else if (do_task == DO_MIN)
					memsize = MIN(memsize, value);
				else
					memsize += value;
			}
		}
	}

	if (do_task == DO_AVG)
		SET_DBL_RESULT(result, proccount == 0 ? 0 : memsize / proccount);
	else
		SET_UI64_RESULT(result, memsize);

	return SYSINFO_RET_OK;
}

int	PROC_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*procname, *proccomm, *param, *args;
	int	zbx_proc_stat, count, i,
		proc_ok, stat_ok, comm_ok,
		op, arg;

	int	proccount = 0;

	size_t	sz;

	struct kinfo_proc2	*proc, *pproc;
	struct passwd		*usrinfo;

	if (4 < request->nparam)
		return SYSINFO_RET_FAIL;

	procname = get_rparam(request, 0);
	param = get_rparam(request, 1);

	if (NULL != param && '\0' != *param)
	{
		if (NULL == (usrinfo = getpwnam(param)))	/* incorrect user name */
			return SYSINFO_RET_FAIL;
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "all"))
		zbx_proc_stat = ZBX_PROC_STAT_ALL;
	else if (0 == strcmp(param, "run"))
		zbx_proc_stat = ZBX_PROC_STAT_RUN;
	else if (0 == strcmp(param, "sleep"))
		zbx_proc_stat = ZBX_PROC_STAT_SLEEP;
	else if (0 == strcmp(param, "zomb"))
		zbx_proc_stat = ZBX_PROC_STAT_ZOMB;
	else
		return SYSINFO_RET_FAIL;

	proccomm = get_rparam(request, 3);

	if (NULL == kd && NULL == (kd = kvm_open(NULL, NULL, NULL, KVM_NO_FILES, NULL)))
		return SYSINFO_RET_FAIL;

	if (NULL != usrinfo)
	{
		op = KERN_PROC_UID;
		arg = (int)usrinfo->pw_uid;
	}
	else
	{
		op = KERN_PROC_ALL;
		arg = 0;
	}

	if (NULL == (proc = kvm_getproc2(kd, op, arg, sizeof(struct kinfo_proc2), &count)))
		return SYSINFO_RET_FAIL;

	for (pproc = proc, i = 0; i < count; pproc++, i++)
	{
		proc_ok = 0;
		stat_ok = 0;
		comm_ok = 0;

		if (NULL == procname || '\0' == *procname || 0 == strcmp(procname, pproc->p_comm))
			proc_ok = 1;

		if (zbx_proc_stat != ZBX_PROC_STAT_ALL)
		{
			switch (zbx_proc_stat) {
			case ZBX_PROC_STAT_RUN:
				if (pproc->p_stat == LSRUN || pproc->p_stat == LSONPROC)
					stat_ok = 1;
				break;
			case ZBX_PROC_STAT_SLEEP:
				if (pproc->p_stat == LSSLEEP)
					stat_ok = 1;
				break;
			case ZBX_PROC_STAT_ZOMB:
				if (pproc->p_stat == SZOMB || pproc->p_stat == LSDEAD)
					stat_ok = 1;
				break;
			}
		}
		else
			stat_ok = 1;

		if (NULL != proccomm && '\0' != *proccomm)
		{
			if (NULL != (args = proc_argv(pproc->p_pid)))
			{
				if (NULL != zbx_regexp_match(args, proccomm, NULL))
					comm_ok = 1;
			}
		}
		else
			comm_ok = 1;

		if (proc_ok && stat_ok && comm_ok)
			proccount++;
	}

	SET_UI64_RESULT(result, proccount);

	return SYSINFO_RET_OK;
}
