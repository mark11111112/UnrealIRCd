/************************************************************************
 *   IRC - Internet Relay Chat, ircd/ircd.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef lint
static	char sccsid[] = "@(#)ircd.c	2.48 3/9/94 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";
#endif


/* debug --sts (chdir kludge) */
// #define UNREAL_DEBUG

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "userload.h"
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/file.h>
#include <pwd.h>
#include <sys/time.h>
#else
#include <io.h>
#include <direct.h>
#endif
#ifdef HPUX
#define _KERNEL            /* HPUX has the world's worst headers... */
#endif
#ifndef _WIN32
#include <sys/resource.h>
#endif
#ifdef HPUX
#undef _KERNEL
#endif
#include <errno.h>
#include "h.h"
#ifndef NO_FDLIST
#include "fdlist.h"
#endif

ID_CVS("$Id$");
ID_Copyright("(C) 1988 University of Oulu, Computing Center and Jarkko Oikarinen");
ID_Notes("2.48 3/9/94");
#ifdef __FreeBSD__
char *malloc_options="h" MALLOC_FLAGS_EXTRA;
#endif


#ifdef SHOWCONNECTINFO
int	R_do_dns, R_fin_dns, R_fin_dnsc, R_fail_dns,
                R_do_id, R_fin_id, R_fail_id;
#ifdef SOCKSPORT
int	R_do_socks, R_no_socks, R_good_socks;
#endif

char	REPORT_DO_DNS[128], REPORT_FIN_DNS[128], REPORT_FIN_DNSC[128],
	REPORT_FAIL_DNS[128], REPORT_DO_ID[128], REPORT_FIN_ID[128],
        REPORT_FAIL_ID[128];
#ifdef SOCKSPORT
char	REPORT_DO_SOCKS[128], REPORT_NO_SOCKS[128], REPORT_GOOD_SOCKS[128];
#endif
#endif
aClient me;			/* That's me */
aClient *client = &me;		/* Pointer to beginning of Client list */
extern char backupbuf[8192];

#ifndef NO_FDLIST
fdlist default_fdlist;
fdlist busycli_fdlist;
fdlist serv_fdlist;   
fdlist oper_fdlist;
float  currentrate;
float  currentrate2; /* outgoing */
float  highest_rate = 0;
float  highest_rate2 = 0;
int    lifesux = 0;

time_t check_fdlists();
#endif
void	server_reboot(char *);
void	restart PROTO((char *));
static	void	open_debugfile(), setup_signals();
void	check_lusers(void);
extern  void   init_glines(void);

char	**myargv;
int	portnum = -1;		    /* Server port number, listening this */
char	*configfile = CONFIGFILE;	/* Server configuration file */
int	debuglevel = -1;		/* Server debug level */
int	bootopt = 0;			/* Server boot option flags */
char	*debugmode = "";		/*  -"-    -"-   -"-  */
char	*sbrk0;				/* initial sbrk(0) */
static	int	dorehash = 0;
static	char	*dpath = DPATH;
int	booted = FALSE;
time_t	nextconnect = 1;	/* time for next try_connections call */
time_t	nextping = 1;		/* same as above for check_pings() */
time_t	nextdnscheck = 0;	/* next time to poll dns to force timeouts */
time_t	nextexpire = 1;	/* next expire run on the dns cache */
time_t  nextkillcheck = 1;      /* next time to check for nickserv kills */
time_t	lastlucheck = 0;
int	lu_noninv, lu_inv, lu_serv, lu_oper, lu_unknown, lu_channel,
	lu_lu, lu_lulocal, lu_lserv, lu_clu, lu_mlu, lu_cglobalu, lu_mglobalu;
/* */
#ifdef UNREAL_DEBUG
#undef CHROOTDIR
#define CHROOT
#endif
#ifdef CLONE_CHECK
        aClone *Clones = NULL;
        char clonekillhost[100];
#endif

time_t	NOW;
#if	defined(PROFIL) && !defined(_WIN32)
extern	etext();

VOIDSIG	s_monitor()
{
	static	int	mon = 0;
#ifdef	POSIX_SIGNALS
	struct	sigaction act;
#endif

	(void)moncontrol(mon);
	mon = 1 - mon;
#ifdef	POSIX_SIGNALS
	act.sa_handler = s_rehash;
	act.sa_flags = 0;
	(void)sigemptyset(&act.sa_mask);
	(void)sigaddset(&act.sa_mask, SIGUSR1);
	(void)sigaction(SIGUSR1, &act, NULL);
#else
	(void)signal(SIGUSR1, s_monitor);
#endif
}
#endif

VOIDSIG s_die()
{
#ifdef	USE_SYSLOG
	(void)syslog(LOG_CRIT, "Server Killed By SIGTERM");
#endif
	flush_connections(me.fd);
#ifndef _WIN32
	exit(-1);
#endif
}

#ifndef _WIN32
static VOIDSIG s_rehash()
#else
VOIDSIG s_rehash()
#endif
{
#ifdef	POSIX_SIGNALS
	struct	sigaction act;
#endif
	dorehash = 1;
#ifdef	POSIX_SIGNALS
	act.sa_handler = s_rehash;
	act.sa_flags = 0;
	(void)sigemptyset(&act.sa_mask);
	(void)sigaddset(&act.sa_mask, SIGHUP);
	(void)sigaction(SIGHUP, &act, NULL);
#else
# ifndef _WIN32
	(void)signal(SIGHUP, s_rehash);	/* sysV -argv */
# endif
#endif
}

void	restart(mesg)
char	*mesg;
{
#ifdef	USE_SYSLOG
	(void)syslog(LOG_WARNING, "Restarting Server because: %s",mesg);
#endif
	server_reboot(mesg);
}

VOIDSIG s_restart()
{
	static int restarting = 0;

#ifdef	USE_SYSLOG
	(void)syslog(LOG_WARNING, "Server Restarting on SIGINT");
#endif
	if (restarting == 0)
	    {
		/* Send (or attempt to) a dying scream to oper if present */

		restarting = 1;
		server_reboot("SIGINT");
	    }
}

VOIDSIG s_segv()
{
	Reg1	int i;
	FILE	*log;
	Reg2	int p;
	char	corename[512];
#ifdef USE_SYSLOG
	(void)syslog(LOG_WARNING, "Server terminating: Segmention fault!!!");
	(void)closelog();
#endif
	sendto_ops("Aieeee!!! Server terminating: Segmention fault (buf: %s)", backupbuf);
	sendto_serv_butone(&me, ":%s GLOBOPS :AIEEE!!! Server Terminating: Segmention fault (buf: %s)", me.name, backupbuf);
	log = fopen(lPATH, "a");
	if (log)
	{
		fprintf(log,"%li - Aieeee!!! Server terminating: Segmention fault (buf: %s)\n",time(NULL),backupbuf);
		fclose(log);
	}
	
#if !defined(_WIN32) && !defined(_AMIGA)
	p = getpid();
	if (fork()>0) {
		kill(p, 3);
		kill(p, 9);
	}
	write_pidfile();
	(void)sprintf(corename, "core.%d", p);
	(void)rename("core", corename);
	sendto_ops("Dumped core to %s - please read Unreal.nfo on what to do!", corename);
#endif
	flush_connections(me.fd);

#ifndef _WIN32
	for (i = 3; i < MAXCONNECTIONS; i++)
		(void)close(i);
#else
	for (i = 0; i < highest_fd; i++)
		if ( closesocket(i) == -1 ) close(i);
#endif	
	exit(-1);
}
void	server_reboot(mesg)
char	*mesg;
{
	Reg1	int	i;

	sendto_ops("Aieeeee!!!  Restarting server... %s", mesg);
	Debug((DEBUG_NOTICE,"Restarting server... %s", mesg));
	flush_connections(me.fd);
	/*
	** fd 0 must be 'preserved' if either the -d or -i options have
	** been passed to us before restarting.
	*/
#ifdef USE_SYSLOG
	(void)closelog();
#endif
#ifndef _WIN32
	for (i = 3; i < MAXCONNECTIONS; i++)
		(void)close(i);
	if (!(bootopt & (BOOT_TTY|BOOT_DEBUG)))
		(void)close(2);
	(void)close(1);
	if ((bootopt & BOOT_CONSOLE) || isatty(0))
		(void)close(0);
	if (!(bootopt & (BOOT_INETD|BOOT_OPER)))
		(void)execv(MYNAME, myargv);
#else
	for (i = 0; i < highest_fd; i++)
		if ( closesocket(i) == -1 ) close(i);

	(void)execv(myargv[0], myargv);
#endif
#ifdef USE_SYSLOG
	/* Have to reopen since it has been closed above */

	openlog(myargv[0], LOG_PID|LOG_NDELAY, LOG_FACILITY);
	syslog(LOG_CRIT, "execv(%s,%s) failed: %m\n", MYNAME, myargv[0]);
	closelog();
#endif
#ifndef _WIN32
	Debug((DEBUG_FATAL,"Couldn't restart server: %s", strerror(errno)));
#else
	Debug((DEBUG_FATAL,"Couldn't restart server: %s", strerror(GetLastError())));
#endif
	exit(-1);
}


/*
** try_connections
**
**	Scan through configuration and try new connections.
**	Returns the calendar time when the next call to this
**	function should be made latest. (No harm done if this
**	is called earlier or later...)
*/
static	time_t	try_connections(currenttime)
time_t	currenttime;
{
	Reg1	aConfItem *aconf;
	Reg2	aClient *cptr;
	aConfItem **pconf;
	int	connecting, confrq;
	time_t	next = 0;
	aClass	*cltmp;
	aConfItem *cconf, *con_conf;
	int	con_class = 0;

	connecting = FALSE;
	Debug((DEBUG_NOTICE,"Connection check at   : %s",
		myctime(currenttime)));
	for (aconf = conf; aconf; aconf = aconf->next )
	    {
		/* Also when already connecting! (update holdtimes) --SRB */
		if (!(aconf->status & CONF_CONNECT_SERVER) || aconf->port <= 0)
			continue;
		cltmp = Class(aconf);
		/*
		** Skip this entry if the use of it is still on hold until
		** future. Otherwise handle this entry (and set it on hold
		** until next time). Will reset only hold times, if already
		** made one successfull connection... [this algorithm is
		** a bit fuzzy... -- msa >;) ]
		*/

		if ((aconf->hold > currenttime))
		    {
			if ((next > aconf->hold) || (next == 0))
				next = aconf->hold;
			continue;
		    }

		confrq = get_con_freq(cltmp);
		aconf->hold = currenttime + confrq;
		/*
		** Found a CONNECT config with port specified, scan clients
		** and see if this server is already connected?
		*/
		cptr = find_name(aconf->name, (aClient *)NULL);

		if (!cptr && (Links(cltmp) < MaxLinks(cltmp)) &&
		    (!connecting || (Class(cltmp) > con_class)))
		  {
		    /* Check connect rules to see if we're allowed to try */
		    for (cconf = conf; cconf; cconf = cconf->next)
		      if ((cconf->status & CONF_CRULE) &&
			  (match(cconf->host, aconf->name) == 0))
			if (crule_eval (cconf->passwd))
			  break;
		    if (!cconf)
		      {
			con_class = Class(cltmp);
			con_conf = aconf;
			/* We connect only one at time... */
			connecting = TRUE;
		      }
		  }
		if ((next > aconf->hold) || (next == 0))
			next = aconf->hold;
	    }
	if (connecting)
	    {
		if (con_conf->next)  /* are we already last? */
		    {
			for (pconf = &conf; (aconf = *pconf);
			     pconf = &(aconf->next))
				/* put the current one at the end and
				 * make sure we try all connections
				 */
				if (aconf == con_conf)
					*pconf = aconf->next;
			(*pconf = con_conf)->next = 0;
		    }
		if (connect_server(con_conf, (aClient *)NULL,
				   (struct hostent *)NULL) == 0)
			sendto_ops("Connection to %s[%s] activated.",
				   con_conf->name, con_conf->host);
	    }
	Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
	return (next);
}

/* Now find_kill is only called when a kline-related command is used:
   AKILL/RAKILL/KLINE/UNKLINE/REHASH.  Very significant CPU usage decrease.
   I made changes to evm_lusers
ery check_pings call to add new parameter.
   -- Barubary */
extern	time_t	check_pings(time_t currenttime, int check_kills)
{		
	Reg1	aClient	*cptr;
	Reg2	int	killflag;
	int	ping = 0, i, i1, rflag = 0;
	time_t	oldest = 0, timeout;

#ifdef PRECISE_CHECK
    for (i1 = 0; i1 <= 7; i1++) {
	for (i = 0; i <= highest_fd; i++)
	    {
		if (!(cptr = local[i]) || IsMe(cptr) || IsLog(cptr))
			continue;
#else
	for (cptr = &me; cptr; cptr = cptr->prev)
	{
		if (IsMe(cptr) || !MyClient(cptr) || IsLog(cptr))
			continue;
#endif

		/*
		** Note: No need to notify opers here. It's
		** already done when "FLAGS_DEADSOCKET" is set.
		*/
		if (cptr->flags & FLAGS_DEADSOCKET)
		    {
			(void)exit_client(cptr, cptr, &me, "Dead socket");
			continue;
		    }

		if (check_kills)
			killflag = IsPerson(cptr) ? find_kill(cptr) : 0;
		else
			killflag = 0;
		if (check_kills && !killflag && IsPerson(cptr))
			if (find_zap(cptr, 1) || find_tkline_match(cptr, 2)>-1)
				killflag = 1;
		ping = IsRegistered(cptr) ? get_client_ping(cptr) :
					    CONNECTTIMEOUT;
		Debug((DEBUG_DEBUG, "c(%s)=%d p %d k %d r %d a %d",
			cptr->name, cptr->status, ping, killflag, rflag,
			currenttime - cptr->lasttime));
		/*
		 * Ok, so goto's are ugly and can be avoided here but this code
		 * is already indented enough so I think its justified. -avalon
		 */
		if (!killflag && !rflag && IsRegistered(cptr) &&
		    (ping >= currenttime - cptr->lasttime))
			goto ping_timeout;
		/*
		 * If the server hasnt talked to us in 2*ping seconds
		 * and it has a ping time, then close its connection.
		 * If the client is a user and a KILL line was found
		 * to be active, close this connection too.
		 */
		if (killflag || rflag ||
		    ((currenttime - cptr->lasttime) >= (2 * ping) &&
		     (cptr->flags & FLAGS_PINGSENT)) ||
		    (!IsRegistered(cptr) &&
		     (currenttime - cptr->firsttime) >= ping))
		    {
			if (!IsRegistered(cptr) &&
			    (DoingDNS(cptr) || DoingAuth(cptr)
#ifdef SOCKSPORT
|| DoingSocks(cptr)			    
#endif
			    ))
			    {
				if (cptr->authfd >= 0)
				    {
#ifndef _WIN32
					(void)close(cptr->authfd);
#else
					(void)closesocket(cptr->authfd);
#endif
					cptr->authfd = -1;
					cptr->count = 0;
					*cptr->buffer = '\0';
				    }
#ifdef SOCKSPORT
                               if (cptr->socksfd >= 0)
                                   {
#ifndef _WIN32
                                       (void)close(cptr->socksfd);
#else
                                       (void)closesocket(cptr->socksfd);
#endif /* _WIN32 */
                                       cptr->socksfd = -1;
                                   }
#endif /* SOCKSPORT */

				    
#ifdef SHOWCONNECTINFO
				if (DoingDNS(cptr))
#ifndef _WIN32
					write(cptr->fd, REPORT_FAIL_DNS,
						R_fail_dns);
#else
					send(cptr->fd, REPORT_FAIL_DNS,
						R_fail_dns, 0);
#endif
				else if (DoingAuth(cptr))
#ifndef _WIN32
					write(cptr->fd, REPORT_FAIL_ID,
						R_fail_id);
#else
					send(cptr->fd, REPORT_FAIL_ID,
						R_fail_id, 0);
#endif

#ifdef SOCKSPORT
                               else
#ifndef _WIN32
                                       write(cptr->fd, REPORT_NO_SOCKS,
                                               R_no_socks);
#else
					send(cptr->fd, REPORT_NO_SOCKS,
						R_no_socks, 0);

#endif
#endif /* SOCKSPORT */

#endif
				Debug((DEBUG_NOTICE,
					"DNS/AUTH timeout %s",
					get_client_name(cptr,TRUE)));
				del_queries((char *)cptr);
				ClearAuth(cptr);
				ClearDNS(cptr);
#ifdef SOCKSPORT
				ClearSocks(cptr);
#endif
				SetAccess(cptr);
				cptr->firsttime = currenttime;
				cptr->lasttime = currenttime;
				continue;
			    }
			if (IsServer(cptr) || IsConnecting(cptr) ||
			    IsHandshake(cptr)) {
				sendto_ops("No response from %s, closing link",
					   get_client_name(cptr, FALSE));
				sendto_serv_butone(&me, ":%s GNOTICE :No response from %s, closing link",
					me.name, get_client_name(cptr, FALSE));
			}
			/*
			 * this is used for KILL lines with time restrictions
			 * on them - send a messgae to the user being killed
			 * first.
			 */
			if (killflag && IsPerson(cptr))
				sendto_ops("Kill line active for %s",
					   get_client_name(cptr, FALSE));

                         if (killflag)
                                (void)exit_client(cptr, cptr, &me,
                                  "User has been banned");
                         else
                                (void)exit_client(cptr, cptr, &me,
                                  "Ping timeout");
			continue;
		    }
		else if (IsRegistered(cptr) &&
			 (cptr->flags & FLAGS_PINGSENT) == 0)
		    {
			/*
			 * if we havent PINGed the connection and we havent
			 * heard from it in a while, PING it to make sure
			 * it is still alive.
			 */
			cptr->flags |= FLAGS_PINGSENT;
			/* not nice but does the job */
			cptr->lasttime = currenttime - ping;
			sendto_one(cptr, "PING :%s", me.name);
		    }
ping_timeout:
		timeout = cptr->lasttime + ping;
		while (timeout <= currenttime)
			timeout += ping;
		if (timeout < oldest || !oldest)
			oldest = timeout;
	    }
#ifdef PRECISE_CHECK
	}
#endif

	if (!oldest || oldest < currenttime)
		oldest = currenttime + PINGFREQUENCY;
	Debug((DEBUG_NOTICE,"Next check_ping() call at: %s, %d %d %d",
		myctime(oldest), ping, oldest, currenttime));

	return (oldest);
}

/*
** bad_command
**	This is called when the commandline is not acceptable.
**	Give error message and exit without starting anything.
*/
static	int	bad_command()
{
#ifndef _WIN32
  (void)printf(
	 "Usage: ircd %s[-h servername] [-p portnumber] [-x loglevel] [-t] [-H]\n",
#ifdef CMDLINE_CONFIG
	 "[-f config] "
#else
	 ""
#endif
	 );
  (void)printf("Server not started\n\n");
#else
  MessageBox(NULL,
         "Usage: wircd [-h servername] [-p portnumber] [-x loglevel]\n",
         "UnrealIRCD/32", MB_OK);
#endif
  return (-1);
}

#ifndef _WIN32
int	main(argc, argv)
#else
int	InitwIRCD(argc, argv)
#endif
int	argc;
char	*argv[];
{
#ifdef _WIN32
	WORD    wVersionRequested = MAKEWORD(1, 1);
	WSADATA wsaData;
#else
	uid_t	uid, euid;
	time_t	delay = 0;
#endif
	int	portarg = 0;
#ifdef  FORCE_CORE
	struct  rlimit corelim;
#endif
#ifndef NO_FDLIST
       int mainloops=0; /* counter of how many times we have gone through
                                    the main loop */
       time_t nextfdlistcheck=0; /*end of priority code */       
#endif
	static  time_t lastglinecheck = 0;

#if !defined(_WIN32) && !defined(_AMIGA)
	sbrk0 = (char *)sbrk((size_t)0);
	uid = getuid();
	euid = geteuid();
# ifdef	PROFIL
	(void)monstartup(0, etext);
	(void)moncontrol(1);
	(void)signal(SIGUSR1, s_monitor);
# endif
#endif

#ifdef	CHROOTDIR
	if (chdir(dpath))
	    {
		perror("chdir");
		exit(-1);
	    }
	res_init();
	if (chroot(DPATH))
	  {
	    (void)fprintf(stderr,"ERROR:  Cannot chdir/chroot\n");
	    exit(5);
	  }
#endif /*CHROOTDIR*/

	myargv = argv;
#ifndef _WIN32
	(void)umask(077);                /* better safe than sorry --SRB */
#else
	WSAStartup(wVersionRequested, &wsaData);
#endif
	bzero((char *)&me, sizeof(me));

#ifndef _WIN32
	setup_signals();
#endif
	initload();

#ifdef FORCE_CORE
	corelim.rlim_cur = corelim.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_CORE, &corelim))
	  printf("unlimit core size failed; errno = %d\n", errno);
#endif

	/*
	** All command line parameters have the syntax "-fstring"
	** or "-f string" (e.g. the space is optional). String may
	** be empty. Flag characters cannot be concatenated (like
	** "-fxyz"), it would conflict with the form "-fstring".
	*/
	while (--argc > 0 && (*++argv)[0] == '-')
	    {
		char	*p = argv[0]+1;
		int	flag = *p++;

		if (flag == '\0' || *p == '\0')
			if (argc > 1 && argv[1][0] != '-')
			    {
				p = *++argv;
				argc -= 1;
			    }
			else
				p = "";

		switch (flag)
		    {
#ifndef _WIN32
                    case 'a':
			bootopt |= BOOT_AUTODIE;
			break;
		    case 'c':
			bootopt |= BOOT_CONSOLE;
			break;
		    case 'q':
			bootopt |= BOOT_QUICK;
			break;
		    case 'd' :
                        (void)setuid((uid_t)uid);
#else
		    case 'd':
#endif
			dpath = p;
			break;
#ifndef _WIN32
		    case 'o': /* Per user local daemon... */
                        (void)setuid((uid_t)uid);
			bootopt |= BOOT_OPER;
		        break;
#ifdef CMDLINE_CONFIG
		    case 'f':
                        (void)setuid((uid_t)uid);
			configfile = p;
			break;
#endif
		    case 'h':
			strncpyzt(me.name, p, sizeof(me.name));
			break;
			case 'H':
				unrealmanual();
				exit(0);
		    case 'i':
			bootopt |= BOOT_INETD|BOOT_AUTODIE;
		        break;
#endif
		    case 'p':
			if ((portarg = atoi(p)) > 0 )
				portnum = portarg;
			break;
		    case 's':
		    	(void)printf("sizeof(aClient) == %u\n", sizeof(aClient));
		    	(void)printf("sizeof(aChannel) == %u\n", sizeof(aChannel));
		    	(void)printf("sizeof(aServer) == %u\n", sizeof(aServer));
			exit(0);
			break;
#ifndef _WIN32
		    case 't':
                        (void)setuid((uid_t)uid);
			bootopt |= BOOT_TTY;
			break;
		    case 'v':
			(void)printf("ircd %s\n", version);
#else
		    case 'v':
			MessageBox(NULL, version, "UnrealIRCD/Win32 version", MB_OK);
#endif
			exit(0);
		    case 'x':
#ifdef	DEBUGMODE
# ifndef _WIN32
                        (void)setuid((uid_t)uid);
# endif
			debuglevel = atoi(p);
			debugmode = *p ? p : "0";
			bootopt |= BOOT_DEBUG;
			break;
#else
# ifndef _WIN32
			(void)fprintf(stderr,
				"%s: DEBUGMODE must be defined for -x y\n",
				myargv[0]);
# else
			MessageBox(NULL,
				"DEBUGMODE must be defined for -x option",
				"UnrealIRCD/32", MB_OK);
# endif
			exit(0);
#endif
		    default:
			bad_command();
			break;
		    }
	    }

#ifndef	CHROOT
	if (chdir(dpath))
	    {
# ifndef _WIN32
		perror("chdir");
# else
		MessageBox(NULL, strerror(GetLastError()), "UnrealIRCD/32: chdir()",
			MB_OK);
# endif
		exit(-1);
	    }
#endif

#if !defined(IRC_UID) && !defined(_WIN32)
	if ((uid != euid) && !euid)
	    {
		(void)fprintf(stderr,
			"ERROR: do not run ircd setuid root. Make it setuid a\
 normal user.\n");
		exit(-1);
	    }
#endif

#if (!defined(CHROOTDIR) || (defined(IRC_UID) && defined(IRC_GID))) \
    && !defined(_WIN32)
# ifndef	AIX
	(void)setuid((uid_t)uid);
	(void)setuid((uid_t)euid);
# endif

	if ((int)getuid() == 0)
	    {
# if defined(IRC_UID) && defined(IRC_GID)

		/* run as a specified user */
		(void)fprintf(stderr,"WARNING: running ircd with uid = %d\n",
			IRC_UID);
		(void)fprintf(stderr,"         changing to gid %d.\n",IRC_GID);
		(void)setuid(IRC_UID);
		(void)setgid(IRC_GID);
#else
		/* check for setuid root as usual */
		(void)fprintf(stderr,
			"ERROR: do not run ircd setuid root. Make it setuid a\
 normal user.\n");
		exit(-1);
# endif	
	    } 
#endif /*CHROOTDIR/UID/GID/_WIN32*/

#ifndef _WIN32
	/* didn't set debuglevel */
	/* but asked for debugging output to tty */
	if ((debuglevel < 0) &&  (bootopt & BOOT_TTY))
	    {
		(void)fprintf(stderr,
			"you specified -t without -x. use -x <n>\n");
		exit(-1);
	    }
#endif

	if (argc > 0)
		return bad_command(); /* This should exit out */
	fprintf(stderr, "=====================================================================\n");
	fprintf(stderr, "= %s loading..\n", version);
	fprintf(stderr, "=====================================================================\n");

	clear_client_hash_table();
	clear_channel_hash_table();
	clear_notify_hash_table();
	inittoken();
	initlists();
	initclass();
	initwhowas();
	initstats();
	booted = FALSE;
	load_conf(ZCONF, 0);	
	booted = TRUE;
	if (dcc_loadconf() == 0)
	{
		fprintf(stderr, "* Loaded DCC deny configuration file ..\n");
	}
	if (cr_loadconf() == 0)
	{
		fprintf(stderr, "* Loaded Channel Restrict configuration file..\n");
	}
	if (vhost_loadconf() == 0)
	{
		fprintf(stderr, "* Loaded Vhost configuration file..\n");
	}
	load_tunefile();
	make_umodestr();
	make_cmodestr();
	fprintf(stderr, "* Dynamic configuration initialized .. booting IRCd.\n");
	fprintf(stderr, "---------------------------------------------------------------------\n");
	
	open_debugfile();
#ifndef NO_FDLIST
        init_fdlist(&serv_fdlist);
        init_fdlist(&busycli_fdlist);
        init_fdlist(&default_fdlist);
	init_fdlist(&oper_fdlist);
        {
                register int i;
                for (i=MAXCONNECTIONS+1 ; i>0 ; i--)
                        default_fdlist.entry[i] = i-1;
        }                     
#endif
	if (portnum < 0)
		portnum = PORTNUM;
	me.port = portnum;
	(void)init_sys();
	me.flags = FLAGS_LISTEN;
#ifndef _WIN32
	if (bootopt & BOOT_INETD)
	    {
		me.fd = 0;
		local[0] = &me;
		me.flags = FLAGS_LISTEN;
	    }
	else
#endif
		me.fd = -1;

#ifdef USE_SYSLOG
	openlog(myargv[0], LOG_PID|LOG_NDELAY, LOG_FACILITY);
#endif
	if (initconf(bootopt) == -1)
	    {
		Debug((DEBUG_FATAL, "Failed in reading configuration file %s",
			configfile));
#ifndef _WIN32
		(void)printf("Couldn't open configuration file %s\n",
			configfile);
#else
		MessageBox(NULL, "Couldn't open configuration file "CONFIGFILE,
			"UnrealIRCD/32", MB_OK);
#endif
		exit(-1);
	    }
	if (!(bootopt & BOOT_INETD))
	    {
		static	char	star[] = "*";
		aConfItem	*aconf;

		if ((aconf = find_me()) && portarg <= 0 && aconf->port > 0)
			portnum = aconf->port;
		Debug((DEBUG_ERROR, "Port = %d", portnum));
		if (inetport(&me, aconf->passwd, portnum))
			exit(1);
	    }
	else if (inetport(&me, "*", 0))
		exit(1);

	(void)setup_ping();
	(void)get_my_name(&me, me.sockhost, sizeof(me.sockhost)-1);
	if (me.name[0] == '\0')
		strncpyzt(me.name, me.sockhost, sizeof(me.name));
	me.hopcount = 0;
	me.authfd = -1;
	me.confs = NULL;
	me.next = NULL;
	me.user = NULL;
	me.from = &me;
#ifdef SOCKSPORT
	me.socksfd = -1;
#endif
	SetMe(&me);
	make_server(&me);
	(void)strcpy(me.serv->up, me.name);

	me.lasttime = me.since = me.firsttime = TStime();
	(void)add_to_client_hash_table(me.name, &me);
#if !defined(_AMIGA) && !defined(_WIN32)
	if (fork())
		exit(0);
		
#endif

#ifdef SHOWCONNECTINFO
	(void)sprintf(REPORT_DO_DNS, ":%s %s", me.name, BREPORT_DO_DNS);
	(void)sprintf(REPORT_FIN_DNS, ":%s %s", me.name, BREPORT_FIN_DNS);
	(void)sprintf(REPORT_FIN_DNSC, ":%s %s", me.name, BREPORT_FIN_DNSC);
	(void)sprintf(REPORT_FAIL_DNS, ":%s %s", me.name, BREPORT_FAIL_DNS);
	(void)sprintf(REPORT_DO_ID, ":%s %s", me.name, BREPORT_DO_ID);
	(void)sprintf(REPORT_FIN_ID, ":%s %s", me.name, BREPORT_FIN_ID);
	(void)sprintf(REPORT_FAIL_ID, ":%s %s", me.name, BREPORT_FAIL_ID);
#ifdef SOCKSPORT
       (void)sprintf(REPORT_DO_SOCKS, ":%s %s", me.name, BREPORT_DO_SOCKS);
       (void)sprintf(REPORT_NO_SOCKS, ":%s %s", me.name, BREPORT_NO_SOCKS);
       (void)sprintf(REPORT_GOOD_SOCKS, ":%s %s", me.name, BREPORT_GOOD_SOCKS);
#endif

	R_do_dns = strlen(REPORT_DO_DNS);
	R_fin_dns = strlen(REPORT_FIN_DNS);
	R_fin_dnsc = strlen(REPORT_FIN_DNSC);
	R_fail_dns = strlen(REPORT_FAIL_DNS);
	R_do_id = strlen(REPORT_DO_ID);
	R_fin_id = strlen(REPORT_FIN_ID);
	R_fail_id = strlen(REPORT_FAIL_ID);
#ifdef SOCKSPORT
       R_do_socks = strlen(REPORT_DO_SOCKS);
       R_no_socks = strlen(REPORT_NO_SOCKS);
       R_good_socks = strlen(REPORT_GOOD_SOCKS);
#endif /* SOCKSPORT */
#endif
#ifdef SOCKSPORT
	       init_socks(&me);
#endif
	
	check_class();
	if (bootopt & BOOT_OPER)
	    {
		aClient *tmp = add_connection(&me, 0);

		if (!tmp)
			exit(1);
		SetMaster(tmp);
	    }
	else
		write_pidfile();

	Debug((DEBUG_NOTICE,"Server ready..."));
#ifdef USE_SYSLOG
	syslog(LOG_NOTICE, "Server Ready");
#endif
#ifndef NO_FDLIST
        check_fdlists(TStime());
#endif
        nextkillcheck = TStime() + (time_t)1;

#ifdef _WIN32
    return 1;
}

void    SocketLoop(void *dummy)
{
	time_t	delay = 0, now;
	static  time_t lastglinecheck = 0;
#ifndef NO_FDLIST
       int mainloops=0; /* counter of how many times we have gone through
                                    the main loop */
       time_t nextfdlistcheck=0; /*end of priority code */       
#endif

	while (1)
#else
	for (;;)
#endif
	    {
		now = TStime();
		if (now - lastglinecheck > 4)
		{
			tkl_check_expire();
			lastglinecheck = now;
		}
		/*
		** Run through the hashes and check lusers every
		** second
		** also check for expiring glines
		*/
		
#ifndef NO_FDLIST
               {
                       static time_t lasttime=0;
                       static long lastrecvK, lastsendK;
                       static int init=0;
                       static time_t loadcfreq=LOADCFREQ;

                       if (now-lasttime < loadcfreq)
                               goto done_check;
                       lasttime = now;
                       if (me.receiveK - lastrecvK >= LOADRECV)
                       {
                               if (!lifesux)
                               {
                                     loadcfreq *= 2; /* add hysteresis */
                                             lifesux = TRUE;
                                     sendto_ops("Entering high-traffic mode (incoming = %0.2f kb/s, outgoing = %0.2f kb/s currently)", currentrate, currentrate2);
                               }
                       }
                             else
                       {
                             loadcfreq = LOADCFREQ;
                             if (lifesux)
                             {
                                     lifesux = 0;
                                     sendto_ops("Resuming standard operation (incoming = %0.2f kb/s, outgoing = %0.2f kb/s now)", currentrate, currentrate2);
                             }
                        }
                      lastrecvK = me.receiveK;
                      lastsendK = me.sendK;
done_check:
	        if (lasttime != now)
	        {
                        currentrate = ((float)(me.receiveK-lastrecvK))/((float)(now-lasttime));
                	currentrate2 = ((float)(me.sendK-lastsendK))/((float)(now-lasttime));
                	if (currentrate > highest_rate)
                		highest_rate = currentrate;
                	if (currentrate2 > highest_rate2)
                		highest_rate2 = currentrate2;
		}
                        ;
               }     
#endif   
		if(lastlucheck < now) {
			check_lusers();
			lastlucheck=now;
//			check_expire_gline ();
		}
		/*
		** We only want to connect if a connection is due,
		** not every time through.  Note, if there are no
		** active C lines, this call to Tryconnections is
		** made once only; it will return 0. - avalon
		*/
		if (nextconnect && now >= nextconnect)
			nextconnect = try_connections(now);
		/*
		** DNS checks. One to timeout queries, one for cache expiries.
		*/
		if (now >= nextdnscheck)
			nextdnscheck = timeout_query_list(now);
		if (now >= nextexpire)
			nextexpire = expire_cache(now);
		/*
		** take the smaller of the two 'timed' event times as
		** the time of next event (stops us being late :) - avalon
		** WARNING - nextconnect can return 0!
		*/
		if (nextconnect)
			delay = MIN(nextping, nextconnect);
		else
			delay = nextping;
		delay = MIN(nextdnscheck, delay);
		delay = MIN(nextexpire, delay);
		delay -= now;
		/*
		** Adjust delay to something reasonable [ad hoc values]
		** (one might think something more clever here... --msa)
		** We don't really need to check that often and as long
		** as we don't delay too long, everything should be ok.
		** waiting too long can cause things to timeout...
		** i.e. PINGS -> a disconnection :(
		** - avalon
		*/
		if (delay < 1)
			delay = 1;
		else
			delay = MIN(delay, TIMESEC);
#ifdef NO_FDLIST
              (void)read_message(delay);
#else
                (void)read_message(0,&serv_fdlist); /* servers */
                (void)read_message(1,&busycli_fdlist); /* busy clients */
               if (lifesux)          
                       (void)read_message(1,&serv_fdlist);
                               /* read servs more often */
               {
                    static time_t lasttime=0;
                    if ((lasttime + (lifesux +1) * 2)< (now = TStime()))
                    { 
                              read_message(delay,NULL); /*  check everything */
                              lasttime = now;
                    } 
              }

#endif		
		Debug((DEBUG_DEBUG ,"Got message(s)"));
		
		now = TStime();
		/*
		** ...perhaps should not do these loops every time,
		** but only if there is some chance of something
		** happening (but, note that conf->hold times may
		** be changed elsewhere--so precomputed next event
		** time might be too far away... (similarly with
		** ping times) --msa
		*/
#ifdef NO_FDLIST
		if (now >= nextping)
#else
		if (now >= nextping && !lifesux)
#endif
			nextping = check_pings(now, 0);

		if (dorehash)
		    {
			(void)rehash(&me, &me, 1);
			dorehash = 0;
		    }
		/*
		** Flush output buffers on all connections now if they
		** have data in them (or at least try to flush)
		** -avalon
		*/
#ifndef NO_FDLIST
                /* check which clients are active */
                if (now > nextfdlistcheck)
                       nextfdlistcheck = check_fdlists(now);
#endif
		flush_connections(me.fd);
	    }
    }
#ifndef NO_FDLIST
time_t check_fdlists(now)
time_t now;
{
      register aClient *cptr;
      int pri; /* temp. for priority */
      register int i,j;
      j = 0;

      for(i=highest_fd;i>=0; i--)
      {
              if (!(cptr=local[i]))
                      continue;
              if (IsServer(cptr) || IsListening(cptr) || IsOper(cptr) ||
                      DoingAuth(cptr))
              {
                      busycli_fdlist.entry[++j] = i;
                      continue;
              }
              pri = cptr->priority;
              if (cptr->receiveM==cptr->lastrecvM)
                      pri+=2; /* lower a bit */
              else
                      pri-=30;
              if (pri < 0) pri = 0;
              if (pri > 80) pri = 80;  

              cptr->lastrecvM = cptr->receiveM;
              cptr->priority = pri;
              if ((pri <10) || (!lifesux && (pri < 25)))
                      busycli_fdlist.entry[++j] = i;
      }
      busycli_fdlist.last_entry=j; /* rest of the fdlist is garbage */
                                                                        
      return (now + FDLISTCHKFREQ + lifesux * FDLISTCHKFREQ);
}
#endif

/*
 * open_debugfile
 *
 * If the -t option is not given on the command line when the server is
 * started, all debugging output is sent to the file set by LPATH in config.h
 * Here we just open that file and make sure it is opened to fd 2 so that
 * any fprintf's to stderr also goto the logfile.  If the debuglevel is not
 * set from the command line by -x, use /dev/null as the dummy logfile as long
 * as DEBUGMODE has been defined, else dont waste the fd.
 */
static	void	open_debugfile()
{
#ifdef	DEBUGMODE
	int	fd;
	aClient	*cptr;

	if (debuglevel >= 0)
	    {
		cptr = make_client(NULL, NULL);
		cptr->fd = 2;
		SetLog(cptr);
		cptr->port = debuglevel;
		cptr->flags = 0;
		cptr->acpt = cptr;
		local[2] = cptr;
		(void)strcpy(cptr->sockhost, me.sockhost);
# ifndef _WIN32
		(void)printf("isatty = %d ttyname = %#x\n",
			isatty(2), (u_int)ttyname(2));
		if (!(bootopt & BOOT_TTY)) /* leave debugging output on fd 2 */
		    {
			(void)truncate(LOGFILE, 0);
			if ((fd = open(LOGFILE, O_WRONLY | O_CREAT, 0600)) < 0) 
				if ((fd = open("/dev/null", O_WRONLY)) < 0)
					exit(-1);
			if (fd != 2)
			    {
				(void)dup2(fd, 2);
				(void)close(fd); 
			    }
			strncpyzt(cptr->name, LOGFILE, sizeof(cptr->name));
		    }
		else if (isatty(2) && ttyname(2))
			strncpyzt(cptr->name, ttyname(2), sizeof(cptr->name));
		else
# endif
			(void)strcpy(cptr->name, "FD2-Pipe");
		Debug((DEBUG_FATAL, "Debug: File <%s> Level: %d at %s",
			cptr->name, cptr->port, myctime(time(NULL))));
	    }
	else
		local[2] = NULL;
#endif
	return;
}

#ifndef _WIN32
static	void	setup_signals()
{
#ifdef	POSIX_SIGNALS
	struct	sigaction act;

	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	(void)sigemptyset(&act.sa_mask);
	(void)sigaddset(&act.sa_mask, SIGPIPE);
	(void)sigaddset(&act.sa_mask, SIGALRM);
# ifdef	SIGWINCH
	(void)sigaddset(&act.sa_mask, SIGWINCH);
	(void)sigaction(SIGWINCH, &act, NULL);
# endif
	(void)sigaction(SIGPIPE, &act, NULL);
	act.sa_handler = dummy;
	(void)sigaction(SIGALRM, &act, NULL);
	act.sa_handler = s_rehash;
	(void)sigemptyset(&act.sa_mask);
	(void)sigaddset(&act.sa_mask, SIGHUP);
	(void)sigaction(SIGHUP, &act, NULL);
	act.sa_handler = s_restart;
	(void)sigaddset(&act.sa_mask, SIGINT);
	(void)sigaction(SIGINT, &act, NULL);
	act.sa_handler = s_die;
	(void)sigaddset(&act.sa_mask, SIGTERM);
	(void)sigaction(SIGTERM, &act, NULL);
/* handling of SIGSEGV as well -sts */
	act.sa_handler = s_segv;
	(void)sigaddset(&act.sa_mask, SIGSEGV);
	(void)sigaction(SIGSEGV, &act, NULL);
	
#else
# ifndef	HAVE_RELIABLE_SIGNALS
	(void)signal(SIGPIPE, dummy);
#  ifdef	SIGWINCH
	(void)signal(SIGWINCH, dummy);
#  endif
# else
#  ifdef	SIGWINCH
	(void)signal(SIGWINCH, SIG_IGN);
#  endif
	(void)signal(SIGPIPE, SIG_IGN);
# endif
	(void)signal(SIGALRM, dummy);   
	(void)signal(SIGHUP, s_rehash);
	(void)signal(SIGTERM, s_die); 
	(void)signal(SIGINT, s_restart);
	(void)signal(SIGSEGV, s_segv);
#endif

#ifdef RESTARTING_SYSTEMCALLS
	/*
	** At least on Apollo sr10.1 it seems continuing system calls
	** after signal is the default. The following 'siginterrupt'
	** should change that default to interrupting calls.
	*/
	(void)siginterrupt(SIGALRM, 1);
#endif
}
#endif /* !_Win32 */




void check_lusers(void) {
        aClient *acptr;
	lu_noninv=lu_inv=lu_serv=lu_oper=lu_unknown=lu_channel=lu_lu=lu_lserv=
          lu_clu=lu_cglobalu=0;
        for (acptr = client; acptr; acptr = acptr->next) {
                switch (acptr->status) {
                 case STAT_SERVER:
                        if (MyConnect(acptr))
                          lu_lserv++;
                 case STAT_ME:
                        lu_serv++;
                        break;
                 case STAT_CLIENT:
                        if (IsOper(acptr))
                          lu_oper++;
                        if (MyConnect(acptr)) {
                                lu_lu++;
                                if (match(DOMAINNAMEMASK, acptr->sockhost) == 0)
                                  lu_lulocal++;
                        }
                        if (!IsInvisible(acptr))
                          lu_noninv++;
                        else
                          lu_inv++;
                        break;
                 default:
                        lu_unknown++;
                        break;
                }
        }
        lu_clu=lu_lu;
        lu_cglobalu=lu_noninv + lu_inv;
        if (lu_clu > lu_mlu)
          lu_mlu = lu_lu;
        lu_cglobalu = lu_noninv + lu_inv;
        if (lu_cglobalu > lu_mglobalu)
          lu_mglobalu = lu_cglobalu;

        lu_channel=count_channels(&me);
}

