/*

    EnergyMech, IRC bot software
    Parts Copyright (c) 1997-2009 proton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
#define PROTECTION_C
#include "config.h"

#include "defines.h"
#include "structs.h"
#include "global.h"
#include "h.h"
#include "text.h"
#include "mcmd.h"

/*
 *
 *  kicking and screaming
 *
 */

void send_kick(Chan *chan, const char *nick, const char *format, ...)
{
	qKick	*new,**pp;
	va_list	vargs;

	/*
	 *  gsockdata safe to use since we're a `tail' function
	 */
	va_start(vargs,format);
	vsprintf(gsockdata,format,vargs);
	va_end(vargs);

	pp = &chan->kicklist;
	while(*pp)
		pp = &(*pp)->next;

	set_mallocdoer(send_kick);
	*pp = new = (qKick*)Calloc(sizeof(qKick) + Strlen2(nick,gsockdata));
	/* Calloc sets to zero new->next = NULL; */

	new->reason = Strcpy(new->nick,nick) + 1;
	Strcpy(new->reason,gsockdata);
}

void push_kicks(Chan *chan)
{
	qKick	*kick;
	int	n;

	n = (current->sendq_time - now);
	while(n < 6)
	{
		if ((kick = chan->kicklist) == NULL)
			return;

		chan->kicklist = kick->next;
		to_server("KICK %s %s :%s\n",chan->name,kick->nick,kick->reason);
		Free((char**)&kick);
		n += 2;
	}
}

void unmode_chanuser(Chan *chan, ChanUser *cu)
{
	qMode	*mode,**pp;

	pp = &chan->modelist;
	while(*pp)
	{
		mode = *pp;
		if ((mode->type == QM_CHANUSER) && (mode->data == (void*)cu))
		{
			*pp = mode->next;
			Free((char**)&mode);
			/*
			 *  there might be more modes associated with this chanuser
			 */
			continue;
		}
		pp = &mode->next;
	}
}

void send_mode(Chan *chan, int pri, int type, char plusminus, char modeflag, void *data)
{
	qMode	*mode,**pp;

#ifdef NO_DEBUG
	debug("(send_mode) chan = %s; pri = %i; type = %i; plusminus = %c; modeflag = %c; data = "mx_pfmt"\n",
		chan->name,pri,type,plusminus,modeflag,(mx_ptr)data);
#endif /* DEBUG */

	/*
	 *  make minusmodes always one priority lower than plusmodes
	 */
	if (plusminus == '-')
		pri |= 1;

	pp = &chan->modelist;
	while(*pp)
	{
		mode = *pp;
		if (mode->pri == pri)
		{
			/*
			 *  check for duplicate
			 */
			if ((mode->type == type) && (mode->plusminus == plusminus) &&
				(mode->modeflag == modeflag) && (mode->data == data))
				return;
		}
		if (mode->pri > pri)
			break;
		pp = &mode->next;
	}

	set_mallocdoer(send_mode);
	mode = (qMode*)Calloc(sizeof(qMode));
	mode->next = *pp;
	*pp = mode;
	mode->pri = pri;
	mode->type = type;
	mode->plusminus = plusminus;
	mode->modeflag = modeflag;

	switch(type)
	{
	case QM_CHANUSER:
		mode->data = data;
		break;
	default:
		if (data)
		{
			set_mallocdoer(send_mode);
			mode->data = (void*)Strdup((char*)data);
		}
		else
		{
			mode->data = NULL;
		}
	}
}

int mode_effect(Chan *chan, qMode *mode)
{
	ChanUser *cu;
	int	f;

	if (mode->type == QM_CHANUSER)
	{
		cu = (ChanUser*)mode->data;
		f = 0;
		switch(mode->modeflag)
		{
		case 'o':
			f = (cu->flags & CU_CHANOP) ? '+' : '-';
			break;
		case 'v':
			f = (cu->flags & CU_VOICE) ? '+' : '-';
			break;
		}
		if (f && f == mode->plusminus)
			return(FALSE);
	}
	return(TRUE);
}

void push_modes(Chan *chan, int lowpri)
{
	qMode	*mode;
	char	flaglist[32],parmlist[MSGLEN];
	char	*dstflag,*dstparm,*srcparm,lastmode;
	int	n,maxmodes;

	n = (current->sendq_time - now);

loop:
	maxmodes = current->setting[INT_MODES].int_var;
	lastmode = 0;
	dstflag = flaglist;
	dstparm = parmlist;
	while((mode = chan->modelist))
	{
		/*
		 *  if the line has already been partially filled,
		 *  then its ok to fill up "empty slots" with non-priority modes.
		 */
		if (lowpri && !lastmode && (mode->pri >= QM_PRI_LOW))
			return;
		chan->modelist = mode->next;
		if (mode_effect(chan,mode))
		{
			switch(mode->type)
			{
			case QM_CHANUSER:
				srcparm = ((ChanUser*)mode->data)->nick;
				break;
			default:
				srcparm = (char*)mode->data;
			}
			if (mode->plusminus != lastmode)
				*(dstflag++) = lastmode = mode->plusminus;
			*(dstflag++) = mode->modeflag;
			if (srcparm)
			{
				*(dstparm++) = ' ';
				dstparm = Strcpy(dstparm,srcparm);
			}
			maxmodes--;
		}
#ifdef NO_DEBUG
		else
		{
			debug("(push_modes) ineffectual mode: %c%c %s\n",mode->plusminus,mode->modeflag,
				(mode->type == QM_CHANUSER) ? ((ChanUser*)mode->data)->nick : (char*)mode->data);
		}
#endif /* DEBUG */
		if (mode->type != QM_CHANUSER)
			Free((char**)&mode->data);
		Free((char**)&mode);
		if (!maxmodes)
			break;
	}
	if (!lastmode)
		return;
	*dstflag = 0;
	*dstparm = 0;
	/*
	 *  the missing space is not a bug.
	 */
	to_server("MODE %s %s%s\n",chan->name,flaglist,parmlist);
	n += 2;

	if (lowpri && n < lowpri)
		goto loop;
}

void update_modes(Chan *chan)
{
	ChanUser *cu;

	for(cu=chan->users;cu;cu=cu->next)
	{
		if (
			(chan->setting[TOG_AOP].int_var && cu->user && cu->user->x.x.aop) ||
			((cu->flags & (CU_CHANOP|CU_NEEDOP)) == CU_NEEDOP)
			)
		{
#ifdef NO_DEBUG
			debug("(update_modes) pushing: MODE %s +o %s!%s\n",
				chan->name,cu->nick,cu->userhost);
#endif /* DEBUG */
			send_mode(chan,50,QM_CHANUSER,'+','o',(void*)cu);
		}
	}
}

/*
 *  check_mass() takes no action, it only touches the abuse counters
 *  and timers, TRUE is returned if the limit is reached
 */
int check_mass(Chan *chan, ChanUser *doer, int type)
{
	time_t	*when;
	int	*num,limit;

	/*
	 *  must handle servers ...
	 */
	if (!doer)
		return(FALSE);

	if (doer->user && doer->user->x.x.access >= ASSTLEVEL)
		return(FALSE);

	if ((type == INT_CKL || type == INT_FL) && (doer->flags & CU_CHANOP))
		return(FALSE);

	limit = chan->setting[type].int_var;
	switch(type)
	{
	/*
	 *  two things we dont want channel users to do
	 */
	//case CHK_CAPS:
	case INT_CKL:
		num = &doer->capsnum;
		break;
	//case CHK_PUB:
	case INT_FL:
		num = &doer->floodnum;
		break;
	/*
	 *  three things we dont want channel ops to do
	 */
	//case CHK_DEOP:
	case INT_MDL:
		num = &doer->deopnum;
		break;
	//case CHK_BAN:
	case INT_MBL:
		num = &doer->bannum;
		break;
	default:
/*	case CHK_KICK: */
/*	case INT_MKL: */
		num = &doer->kicknum;
		break;
	}

	when = (time_t*)&num[1];
	if ((now - *when) > 10)
	{
		*when = now;
		*num = 0;
	}
	++(*num);
	if (*num >= limit && limit)
		return(TRUE);
	return(FALSE);
}

void mass_action(Chan *chan, ChanUser *doer)
{
	int	mpl;

	if ((mpl = chan->setting[INT_MPL].int_var) == 0)
		return;

	if (mpl >= 2)
	{
		if (0 == (doer->flags & CU_DEOPPED) || 0 == (doer->flags & CU_BANNED))
		{
			deop_ban(chan,doer,NULL);
			doer->flags |= CU_DEOPPED|CU_BANNED;
		}
	}

	if (0 == (doer->flags & CU_KICKED))
	{
		send_kick(chan,CurrentNick,KICK_MASSMODES);
		doer->flags |= CU_KICKED;
	}
}

void prot_action(Chan *chan, char *from, ChanUser *doer, char *target, ChanUser *victim)
{
	int	maxprot,uprot;

	/*
	 *  cant do anything to a user that isnt on the channel
	 *  doer is normally supplied, but not always
	 */
	if (!doer)
	{
		if ((doer = find_chanuser(chan,from)) == NULL)
			return;
	}

	/*
	 *  No protective measures for doers with high access
	 */
	if (doer->user && doer->user->x.x.access >= ASSTLEVEL)
		return;

	maxprot = chan->setting[INT_PROT].int_var;

	if (victim)
		uprot = (victim->user) ? victim->user->x.x.prot : 0;
	else
	{
		uprot = get_protaction(chan,target);
	}

	if ((uprot >= 4) && (!(doer->flags & CU_BANNED)))
	{
		doer->flags |= CU_BANNED|CU_DEOPPED;
		deop_ban(chan,doer,NULL);
	}

	if ((uprot >= 3) && (!(doer->flags & CU_KICKED)))
	{
		doer->flags |= CU_KICKED;
		send_kick(chan,doer->nick,"\002%s is Protected\002",(target) ? target : get_nuh(victim));
	}
	/*
	 *  with (uprot == 2) our ONLY action is to deop the guilty party
	 */
	if ((uprot == 2) && (!(doer->flags & CU_DEOPPED)))
	{
		doer->flags |= CU_DEOPPED;
		send_mode(chan,50,QM_CHANUSER,'-','o',(void*)doer);
	}
}

/*
 *
 *  mode commands
 *
 */

void do_opdeopme(COMMAND_ARGS)
{
	/*
	 *  on_msg checks CAXS
	 */
	Chan	*chan;
	ChanUser *cu;

	if ((chan = CurrentChan) && chan->bot_is_op)
	{
		if ((cu = find_chanuser(chan,from)))
		{
			send_mode(chan,80,QM_CHANUSER,
				(CurrentCmd->name == C_DOWN) ? '-' : '+','o',cu);
		}
	}
}

void do_opvoice(COMMAND_ARGS)
{
	/*
	 *  on_msg checks CAXS + CARGS
	 */
	Chan	*chan;
	char	*m;

	if ((chan = CurrentChan) && chan->bot_is_op)
	{
		/* rest can only be NULL on OP/VOICE commands,
		   DEOP/UNVOICE commands require arguments (CARGS) */
		if (!*rest)
			rest = CurrentNick;
		m = CurrentCmd->cmdarg;
#ifdef DEBUG
		debug("(do_opvoice) Chan '%s', sign %c, mode %c, rest '%s'\n",chan->name,m[1],m[0],rest);
#endif /* DEBUG */
		channel_massmode(chan,rest,MODE_FORCE,m[1],m[0]);
	}
}

void do_kickban(COMMAND_ARGS)
{
	/*
	 *  on_msg checks CAXS + CARGS + ACCHAN
	 */
	Chan	*chan;
	ChanUser *victim;
	const char *m;
	char	*nick,*nuh;
	uchar	c;
	int	uaccess;

	nick = chop(&rest);
	chan = CurrentChan;

	/*
	 *  is the bot opped on that channel?
	 */
	if (!chan->bot_is_op)
		return;

	if ((victim = find_chanuser(chan,nick)) == NULL)
		return;

	if (*rest == 0)
	{
		if ((rest = randstring(RANDKICKSFILE)) == NULL)
			rest = KICK_DEFAULT;
	}

	m = CurrentCmd->cmdarg;
	c = *(m++);

	uaccess = get_authaccess(from,to);
	if (victim->user && victim->user->x.x.access > uaccess)
	{
		nuh = get_nuh(victim);
		send_kick(chan,CurrentNick,"%s attempt of %s",m,nuh);
		return;
	}

	if (c & 0x4)
		send_kick(chan,nick,FMT_PLAIN,rest);

	switch(c & 0x3)
	{
	case 0:
		deop_ban(chan,victim,NULL);
		break;
	case 1:
		deop_siteban(chan,victim);
		break;
	case 2:
		deop_screwban(chan,victim);
		break;
	}

	m = STREND(m)+1;
	to_user(from,"%s %sed on %s",nick,m,to);
}

void do_unban(COMMAND_ARGS)
{
	/*
	 *  on_msg checks: CAXS
	 */
	Ban	*ban;
	Chan	*chan;
	char	*nick,*nuh;

	nick = (rest) ? chop(&rest) : NULL;

	if (((chan = find_channel_ac(to)) == NULL) || !chan->bot_is_op)
		return;

	if (nick && STRCHR(nick,'*'))
	{
		channel_massunban(chan,nick,0);
		return;
	}

	if (!nick)
	{
		to_user(from,"You have been unbanned on %s",to);
		nuh = from;
	}
	else
	{
		if ((nuh = nick2uh(from,nick)) == NULL)
			return;
		to_user(from,"%s unbanned on %s",nuh,to);
	}

	for(ban=chan->banlist;ban;ban=ban->next)
	{
		if (!matches(ban->banstring,nuh))
		{
			send_mode(chan,90,QM_RAWMODE,'-','b',(void*)ban->banstring);
		}
	}
}

void do_banlist(COMMAND_ARGS)
{
	/*
	 *  on_msg checks CAXS
	 */
	Ban	*ban;
	Chan	*chan = CurrentChan;
#ifdef IRCD_EXTENSIONS
	char	*s;

	if (chan->banlist)
	{
		table_buffer(str_underline("channel") "\t" str_underline("type") "\t" str_underline("mask") "\t" str_underline("set by"));
		for(ban=chan->banlist;ban;ban=ban->next)
		{
#ifdef NEWBIE
			if (ban->imode)
				s = "invitelist";
			else
			if (ban->emode)
				s = "ban exception";
			else
				s = "ban";
#else /* NEWBIE */
			if (ban->imode)
				s = "I";
			else
			if (ban->emode)
				s = "e";
			else
				s = "b";
#endif /* NEWBIE */

			table_buffer("%s\t%s\t%s\t%s",to,s,ban->banstring,ban->bannedby);
		}
		table_send(from,2);
	}
	else
		to_user(from,"There are no active bans/exceptions on %s",to);
#else /* IRCD_EXTENSIONS */
	if (chan->banlist)
	{
		table_buffer(str_underline("channel") "\t\037ban mask\037\t\037set by\037");
		for(ban=chan->banlist;ban;ban=ban->next)
			table_buffer("%s\t%s\t%s",to,ban->banstring,ban->bannedby);
		table_send(from,2);
	}
	else
		to_user(from,"There are no active bans on %s",to);
#endif /* IRCD_EXTENSIONS */
}
