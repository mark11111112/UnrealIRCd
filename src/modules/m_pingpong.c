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

DLLFUNC int m_ping(aClient *cptr, aClient *sptr, int parc, char *parv[]);
DLLFUNC int m_pong(aClient *cptr, aClient *sptr, int parc, char *parv[]);
DLLFUNC int m_nospoof(aClient *cptr, aClient *sptr, int parc, char *parv[]);


/* Place includes here */
#define MSG_PING        "PING"  /* PING */
#define TOK_PING        "8"     /* 56 */  
#define MSG_PONG        "PONG"  /* PONG */
#define TOK_PONG        "9"     /* 57 */  


#ifndef DYNAMIC_LINKING
ModuleInfo m_pingpong_info
#else
#define m_pingpong_info mod_header
ModuleInfo mod_header
#endif
  = {
  	2,
	"pingpong",	/* Name of module */
	"$Id$", /* Version */
	"ping, pong and nospoof", /* Short description of module */
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
int    m_pingpong_init(int module_load)
#endif
{
	/*
	 * We call our add_Command crap here
	*/
	Debug((DEBUG_NOTICE, "INIT"));

	add_Command(MSG_PING, TOK_PING, m_ping, MAXPARA);
	add_CommandX(MSG_PONG, TOK_PONG, m_pong, MAXPARA, M_UNREGISTERED|M_USER|M_SERVER);
}

/* Is first run when server is 100% ready */
#ifdef DYNAMIC_LINKING
DLLFUNC int	mod_load(int module_load)
#else
int    m_pingpong_load(int module_load)
#endif
{
}


/* Called when module is unloaded */
#ifdef DYNAMIC_LINKING
DLLFUNC void	mod_unload(void)
#else
void	m_pingpong_unload(void)
#endif
{
	if (del_Command(MSG_PING, TOK_PING, m_ping) < 0)
	{
		sendto_realops("Failed to delete command ping when unloading %s",
				m_pingpong_info.name);
	}
	if (del_Command(MSG_PONG, TOK_PONG, m_pong) < 0)
	{
		sendto_realops("Failed to delete command pong when unloading %s",
				m_pingpong_info.name);
	}
}


/*
** m_ping
**	parv[0] = sender prefix
**	parv[1] = origin
**	parv[2] = destination
*/
DLLFUNC int  m_ping(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	char *origin, *destination;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOORIGIN), me.name, parv[0]);
		return 0;
	}
	origin = parv[1];
	destination = parv[2];	/* Will get NULL or pointer (parc >= 2!!) */

	acptr = find_client(origin, NULL);
	if (!acptr)
		acptr = find_server_quick(origin);
	if (acptr && acptr != sptr)
		origin = cptr->name;
	if (!BadPtr(destination) && mycmp(destination, me.name) != 0)
	{
		if ((acptr = find_server_quick(destination)))
			sendto_one(acptr, ":%s PING %s :%s", parv[0],
			    origin, destination);
		else
		{
			sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
			    me.name, parv[0], destination);
			return 0;
		}
	}
	else
		sendto_one(sptr, ":%s %s %s :%s", me.name,
		    IsToken(sptr) ? TOK_PONG : MSG_PONG,
		    (destination) ? destination : me.name, origin);
	return 0;
}

/*
** m_nospoof - allows clients to respond to no spoofing patch
**	parv[0] = prefix
**	parv[1] = code
*/
DLLFUNC int  m_nospoof(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	unsigned long result;
Debug((DEBUG_NOTICE, "NOSPOOF"));

#ifdef NOSPOOF
	if (IsNotSpoof(cptr))
		return 0;
	if (IsRegistered(cptr))
		return 0;
	if (!*sptr->name)
		return 0;
	if (BadPtr(parv[1]))
		goto temp;
	result = strtoul(parv[1], NULL, 16);
	/* Accept code in second parameter (ircserv) */
	if (result != sptr->nospoof)
	{
		if (BadPtr(parv[2]))
			goto temp;
		result = strtoul(parv[2], NULL, 16);
		if (result != sptr->nospoof)
			goto temp;
	}
	sptr->nospoof = 0;
	if (sptr->user && sptr->name[0])
		return register_user(cptr, sptr, sptr->name,
		    sptr->user->username, NULL, NULL);
	return 0;
      temp:
	/* Homer compatibility */
	sendto_one(cptr, ":%X!nospoof@%s PRIVMSG %s :\1VERSION\1",
	    cptr->nospoof, me.name, cptr->name);
#endif
	return 0;
}

/*
** m_pong
**	parv[0] = sender prefix
**	parv[1] = origin
**	parv[2] = destination
*/
DLLFUNC int m_pong(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	char *origin, *destination;


#ifdef NOSPOOF
	if (!IsRegistered(cptr))
		return m_nospoof(cptr, sptr, parc, parv);
#endif

	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOORIGIN), me.name, parv[0]);
		return 0;
	}

	origin = parv[1];
	destination = parv[2];
	cptr->flags &= ~FLAGS_PINGSENT;
	sptr->flags &= ~FLAGS_PINGSENT;

	if (!BadPtr(destination) && mycmp(destination, me.name) != 0)
	{
		if ((acptr = find_client(destination, NULL)) ||
		    (acptr = find_server_quick(destination)))
		{
			if (!IsServer(cptr) && !IsServer(acptr))
			{
				sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
				    me.name, parv[0], destination);
				return 0;
			}
			else
				sendto_one(acptr, ":%s PONG %s %s",
				    parv[0], origin, destination);
		}
		else
		{
			sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
			    me.name, parv[0], destination);
			return 0;
		}
	}
#ifdef	DEBUGMODE
	else
		Debug((DEBUG_NOTICE, "PONG: %s %s", origin,
		    destination ? destination : "*"));
#endif
	return 0;
}
