/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Andrew Uselton (uselton2@llnl.gov>
 *  UCRL-CODE-2002-008.
 *  
 *  This file is part of PowerMan, a remote power management program.
 *  For details, see <http://www.llnl.gov/linux/powerman/>.
 *  
 *  PowerMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  PowerMan is distributed in the hope that it will be useful, but WITHOUT 
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
 *  for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with PowerMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <syslog.h>

#include "powerman.h"

#include "string.h"
#include "wrappers.h"
#include "error.h"
#include "buffer.h"
#include "config.h"
#include "device.h"
#include "powermand.h"
#include "client.h"
#include "action.h"

static List act_actions = NULL;

/* Each of these represents a script that must be completed.
 * An error message and two of the debug "dump" routines rely on
 * this array for the names of the client protocol operations.
 * The header file has corresponding #define values.
 */
char *pm_coms[] = {
    "PM_ERROR",
    "PM_LOG_IN",
    "PM_CHECK_LOGIN",
    "PM_LOG_OUT",
    "PM_UPDATE_PLUGS",
    "PM_UPDATE_NODES",
    "PM_POWER_ON",
    "PM_POWER_OFF",
    "PM_POWER_CYCLE",
    "PM_RESET",
    "PM_NAMES"
};

/*
 *   The main select() loop will generate this function call 
 * periodically.  The frequency can be set in the config file.
 */
void act_update(void)
{
    Action *act;

    syslog(LOG_INFO, "updating plugs and nodes");

    act = act_create(PM_UPDATE_PLUGS);
    list_append(act_actions, act);

    act = act_create(PM_UPDATE_NODES);
    list_append(act_actions, act);
}

/*
 *   This function retrieves the Action at the head of the queue.
 * It must verify that the retrieved action is for a client who 
 * still exists.  Internally generated Actions (from act_update())
 * do not have this concern, so they are not checked.  If the
 * client has disconnected the Action is discarded.
 */
Action *act_find(void)
{
    Action *act = NULL;

    while ((!list_is_empty(act_actions)) && (act == NULL)) {
	act = list_peek(act_actions);
	/* act->client == NULL is a special internally generated action */
	if (act->client == NULL)
	    continue;
	if (cli_exists(act->client))
	    continue;
	/* I could log an event: "client abort prior to action completion" */
	act_del_queuehead(act_actions);
	act = NULL;
    }
    return act;
}


/*
 *   The devices have gone idle and the cluster quiescent.  It's
 * time to start a new action.  The first five ACTION types 
 * may be handled immediately, and yet another action pulled 
 * from the queue.  act_initiate() is called recursively until 
 * some Action comes to the head of the list that requires device
 * communication (or the list runs out and the function returns).
 *   When an Action is encountered thatrequires device 
 * communication the cluster is marked "Occupied" and corresponding
 * Actions are dispatched to each appropriate device.
 */
void act_initiate(Action * act)
{
    Device *dev;
    ListIterator itr;

    assert(act != NULL);
    CHECK_MAGIC(act);

    switch (act->com) {
    case PM_ERROR:
    case PM_LOG_IN:
    case PM_CHECK_LOGIN:
    case PM_LOG_OUT:
    case PM_NAMES:
	act_finish(act);
	if ((act = act_find()) != NULL)
	    act_initiate(act);
	return;
    case PM_UPDATE_PLUGS:
    case PM_UPDATE_NODES:
    case PM_POWER_ON:
    case PM_POWER_OFF:
    case PM_POWER_CYCLE:
    case PM_RESET:
	break;
    default:
	assert(FALSE);
    }
    itr = list_iterator_create(dev_devices);
    while ((dev = list_next(itr)))
	dev_acttodev(dev, act);
    list_iterator_destroy(itr);
}

/*
 *    From either act_initiate() or the main select() loop this function
 * replies to a client if appropriate, marks the timestamp (suppressing
 * an act_update() for update_interval), and destroys the Action
 * strucutre.
 *
 * Destroys:  Action
 */
void act_finish(Action * act)
{
    /* act->client == NULL means that there is no client expecting a reply. */
    if (act->client != NULL)
	cli_reply(act);
    act_del_queuehead(act_actions);
}

Action *act_create(int com)
{
    Action *act;

    act = (Action *) Malloc(sizeof(Action));
    INIT_MAGIC(act);
    act->com = com;
    act->client = NULL;
    act->seq = 0;
    act->itr = NULL;
    act->cur = NULL;
    act->target = NULL;
    return act;
}

void act_destroy(Action * act)
{
    CHECK_MAGIC(act);

    if (act->target != NULL)
	str_destroy(act->target);
    act->target = NULL;
    if (act->itr != NULL)
	list_iterator_destroy(act->itr);
    act->itr = NULL;
    act->cur = NULL;
    CLEAR_MAGIC(act);
    Free(act);
}


/*
 *  Get rid of the Action at the head of the queue.
 *
 * Destroys:  Action
 */
void act_del_queuehead(List acts)
{
    Action *act;

    act = list_pop(acts);
    act_destroy(act);
}

void act_add(Action *act)
{
    list_append(act_actions, act);
}


void act_init(void)
{
    act_actions = list_create((ListDelF) act_destroy);
}

void act_fini(void)
{
    list_destroy(act_actions);
}

/*
 * vi:softtabstop=4
 */
