/*
 * Module skeleton, by Carsten V. Munk 2001 <stskeeps@tspre.org>
 * May be used, modified, or changed by anyone, no license applies.
 * You may relicense this, to any license
 */
#include "config.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <fcntl.h>
#include "h.h"
#ifdef STRIPBADWORDS
#include "badwords.h"
#endif
#ifdef _WIN32
#include "version.h"
#endif

DLLFUNC int m_kline(aClient *cptr, aClient *sptr, int parc, char *parv[]);

/* Place includes here */
#define MSG_KLINE       "KLINE" /* KLINE */
#define TOK_KLINE       "W"     /* 87 */



#ifndef DYNAMIC_LINKING
ModuleInfo m_kline_info
#else
#define m_kline_info mod_header
ModuleInfo mod_header
#endif
  = {
  	2,
	"kline",	/* Name of module */
	"$Id$", /* Version */
	"command /kline", /* Short description of module */
	NULL, /* Pointer to our dlopen() return value */
	NULL 
    };


/* The purpose of these ifdefs, are that we can "static" link the ircd if we
 * want to
*/

/* This is called on module init, before Server Ready */
#ifdef DYNAMIC_LINKING
DLLFUNC int	mod_init(int module_load)
#else
int    m_kline_init(int module_load)
#endif
{
	/*
	 * We call our add_Command crap here
	*/
	add_Command(MSG_KLINE, TOK_KLINE, m_kline, MAXPARA);
}

/* Is first run when server is 100% ready */
#ifdef DYNAMIC_LINKING
DLLFUNC int	mod_load(int module_load)
#else
int    m_kline_load(int module_load)
#endif
{
}


/* Called when module is unloaded */
#ifdef DYNAMIC_LINKING
DLLFUNC void	mod_unload(void)
#else
void	m_kline_unload(void)
#endif
{
	if (del_Command(MSG_KLINE, TOK_KLINE, m_kline) < 0)
	{
		sendto_realops("Failed to delete commands when unloading %s",
				m_kline_info.name);
	}
}

/*
** m_kline;
**	parv[0] = sender prefix
**	parv[1] = nickname
**	parv[2] = comment or filename
*/
DLLFUNC int m_kline(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
	char *host, *tmp, *hosttemp;
	char uhost[80], name[80];
	int  ip1, ip2, ip3, temp;
	aClient *acptr;
	ConfigItem_ban *bconf;

	if (!MyClient(sptr) || !OPCanKline(sptr))
	{
		sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
		return 0;
	}


	if (parc < 2)
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "KLINE");
		return 0;
	}


/* This patch allows opers to quote kline by address as well as nick
 * --Russell
 */
	if (hosttemp = (char *)strchr((char *)parv[1], '@'))
	{
		*hosttemp = 0;
		hosttemp++;
		bzero(name, sizeof(name));
		bzero(uhost, sizeof(uhost));
		
		strncpy(name, parv[1], sizeof(name) - 1);
		strncpy(uhost, hosttemp, sizeof(uhost) - 1);
		
		if (name[0] == '\0' || uhost[0] == '\0')
		{
			Debug((DEBUG_INFO, "KLINE: Bad field!"));
			sendto_one(sptr,
			    "NOTICE %s :*** If you're going to add a userhost, at LEAST specify both fields",
			    parv[0]);
			return 0;
		}
		if (!strcmp(uhost, "*") || !strchr(uhost, '.'))
		{
			sendto_one(sptr,
			    "NOTICE %s :*** What a sweeping K:Line.  If only your admin knew you tried that..",
			    parv[0]);
			sendto_realops("%s attempted to /kline *@*", parv[0]);
			return 0;
		}
	}

/* by nick */
	else
	{
		if (!(acptr = find_client(parv[1], NULL)))
		{
			if (!(acptr =
			    get_history(parv[1], (long)KILLCHASETIMELIMIT)))
			{
				sendto_one(sptr,
				    "NOTICE %s :*** Can't find user %s to add KLINE",
				    parv[0], parv[1]);
				return 0;
			}
		}

		if (!acptr->user)
			return 0;

		strcpy(name, acptr->user->username);
		if (MyClient(acptr))
			host = acptr->sockhost;
		else
			host = acptr->user->realhost;

		/* Sanity checks */

		if (name == '\0' || host == '\0')
		{
			Debug((DEBUG_INFO, "KLINE: Bad field"));
			sendto_one(sptr, "NOTICE %s :*** Bad field!", parv[0]);
			return 0;
		}

		/* Add some wildcards */


		strcpy(uhost, host);
		if (isdigit(host[strlen(host) - 1]))
		{
			if (sscanf(host, "%d.%d.%d.%*d", &ip1, &ip2, &ip3))
				ircsprintf(uhost, "%d.%d.%d.*", ip1, ip2, ip3);
		}
		else if (sscanf(host, "%*[^.].%*[^.].%s", uhost))
		{		/* Not really... */
			tmp = (char *)strchr(host, '.');
			ircsprintf(uhost, "*%s", tmp);
		}
	}

	sendto_realops("%s added a temporary user ban for %s@%s %s", parv[0], name, uhost,
	    parv[2] ? parv[2] : "");
	ircd_log(LOG_KLINE, "%s added a temporary user ban for %s@%s %s",
	   parv[0], name, uhost,
	    parv[2] ? parv[2] : "");
	bconf = (ConfigItem_ban *)MyMallocEx(sizeof(ConfigItem_ban));
	bconf->flag.type = CONF_BAN_USER;
	bconf->mask = strdup(make_user_host(name, uhost));
	bconf->reason = parv[2] ? strdup(parv[2]) : NULL;
	bconf->flag.type2 = CONF_BAN_TYPE_TEMPORARY;
	add_ConfigItem((ConfigItem *) bconf, (ConfigItem **) &conf_ban);
	check_pings(TStime(), 1);
}
