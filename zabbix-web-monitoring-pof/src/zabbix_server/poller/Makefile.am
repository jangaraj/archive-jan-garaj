## Process this file with automake to produce Makefile.in

noinst_LIBRARIES = libzbxpoller.a

if WITH_ODBC
CHECKS_DB_SRC = checks_db.c checks_db.h
else
CHECKS_DB_SRC =
endif

libzbxpoller_a_SOURCES = \
	checks_agent.c checks_agent.h \
	checks_internal.c checks_internal.h \
	checks_simple.c checks_simple.h \
	checks_simple_vmware.c checks_simple_vmware.h \
	checks_snmp.c checks_snmp.h \
	${CHECKS_DB_SRC} \
	checks_aggregate.c checks_aggregate.h \
	checks_external.c checks_external.h \
	checks_ipmi.c checks_ipmi.h \
	checks_ssh.c checks_ssh.h \
	checks_telnet.c checks_telnet.h \
	checks_java.c checks_java.h \
	checks_calculated.c checks_calculated.h \
	poller.c poller.h

libzbxpoller_a_CFLAGS = -I@top_srcdir@/src/libs/zbxsysinfo/simple -I@top_srcdir@/src/libs/zbxdbcache
