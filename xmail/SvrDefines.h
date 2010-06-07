/*
 *  XMail by Davide Libenzi (Intranet and Internet mail server)
 *  Copyright (C) 1999,..,2010  Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _SVRDEFINES_H
#define _SVRDEFINES_H

/* Include configuration options */
#include "SvrConfig.h"

#define TAB_COMMENT_CHAR            '#'
#define MAX_SMTP_ADDRESS            1024
#define MAX_ADDR_NAME               256
#define MAX_HOST_NAME               256
#define MAX_MESSAGE_ID              SYS_MAX_PATH
#define MAX_ACCEPT_ADDRESSES        32
#define LOG_ROTATE_HOURS            24
#define STD_SERVER_TIMEOUT          90000
#define LOCAL_ADDRESS               "127.0.0.1"
#define LOCAL_ADDRESS_SQB           "[" LOCAL_ADDRESS "]"

#endif
