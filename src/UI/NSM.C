
/*******************************************************************************/
/* Copyright (C) 2012 Jonathan Moore Liles                                     */
/*                                                                             */
/* This program is free software; you can redistribute it and/or modify it     */
/* under the terms of the GNU General Public License as published by the       */
/* Free Software Foundation; either version 2 of the License, or (at your      */
/* option) any later version.                                                  */
/*                                                                             */
/* This program is distributed in the hope that it will be useful, but WITHOUT */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for   */
/* more details.                                                               */
/*                                                                             */
/* You should have received a copy of the GNU General Public License along     */
/* with This program; see the file COPYING.  If not,write to the Free Software */
/* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */
/*******************************************************************************/


#include "NSM.H"

#include "../Nio/Nio.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>

extern int Pexitprogram;

extern NSM_Client *nsm;
extern char       *instance_name;

NSM_Client::NSM_Client(zyn::MiddleWare *m)
    :project_filename(0),
     display_name(0),
     middleware(m)
{
}

int command_open(const char *name,
                 const char *display_name,
                 const char *client_id,
                 char **out_msg);
int command_save(char **out_msg);

int
NSM_Client::command_save(char **out_msg)
{
    (void) out_msg;
    int r = ERR_OK;
    if(!project_filename)
        return ERR_NO_SESSION_OPEN;

    middleware->transmitMsgGui(0, "/save_xmz", "s", project_filename);

    return r;
}

int
NSM_Client::command_open(const char *name,
                         const char *display_name,
                         const char *client_id,
                         char **out_msg)
{
    (void) out_msg;
    zyn::Nio::stop();

    if(instance_name)
        free(instance_name);

    instance_name = strdup(client_id);

    zyn::Nio::start();

    char *new_filename = (char *)malloc(strlen(name) + 5);

    if (new_filename) {
        strcpy(new_filename, name);
        strcat(new_filename, ".xmz");
    } else {
        // TODO, handle error condition...
    }

    struct stat st;

    int r = ERR_OK;

    if(0 == stat(new_filename, &st))
        middleware->transmitMsgGui(0, "/load_xmz", "s", new_filename);
    else
        middleware->transmitMsgGui(0, "/reset_master", "");

    if(project_filename)
        free(project_filename);

    if(this->display_name)
        free(this->display_name);

    project_filename = new_filename;

    this->display_name = strdup(display_name);

    return r;
}

void
NSM_Client::command_active(bool active)
{
    (void)active;
}
