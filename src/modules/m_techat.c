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

DLLFUNC int m_techat(aClient *cptr, aClient *sptr, int parc, char *parv[]);

/* Place includes here */
#define MSG_TECHAT      "TECHAT"        /* techadmin chat */
#define TOK_TECHAT      "AB"    /* questionmark? */

#ifndef DYNAMIC_LINKING
ModuleInfo m_techat_info
#else
#define m_techat_info mod_header
ModuleInfo mod_header
#endif
  = {
  	2,
	"techat",	/* Name of module */
	"$Id$", /* Version */
	"command /techat", /* Short description of module */
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
int    m_techat_init(int module_load)
#endif
{
	/*
	 * We call our add_Command crap here
	*/
	add_Command(MSG_TECHAT, TOK_TECHAT, m_techat, MAXPARA);
}

/* Is first run when server is 100% ready */
#ifdef DYNAMIC_LINKING
DLLFUNC int	mod_load(int module_load)
#else
int    m_techat_load(int module_load)
#endif
{
}


/* Called when module is unloaded */
#ifdef DYNAMIC_LINKING
DLLFUNC void	mod_unload(void)
#else
void	m_techat_unload(void)
#endif
{
	if (del_Command(MSG_TECHAT, TOK_TECHAT, m_techat) < 0)
	{
		sendto_realops("Failed to delete commands when unloading %s",
				m_techat_info.name);
	}
}

/*
** m_techat (Techadmin chat only) -Potvin (cloned by --sts)
**      parv[0] = sender prefix
**      parv[1] = message text
*/
DLLFUNC int m_techat(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
	char *message;


	message = parc > 1 ? parv[1] : NULL;

	if (BadPtr(message))
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "TECHAT");
		return 0;
	}
#ifdef ADMINCHAT
	if (MyClient(sptr))
		if (!(IsTechAdmin(sptr) || IsNetAdmin(sptr)))
#else
	if (MyClient(sptr))
#endif
	{
		sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
		return 0;
	}
	sendto_serv_butone_token(IsServer(cptr) ? cptr : NULL, parv[0],
	   MSG_TECHAT, TOK_TECHAT, ":%s", message);
#ifdef ADMINCHAT
	sendto_umode(UMODE_TECHADMIN, "*** Te-chat -- from %s: %s",
	    parv[0], message);
#endif
	return 0;
}
