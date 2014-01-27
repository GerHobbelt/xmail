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

#ifndef _SVRCONFIG_H
#define _SVRCONFIG_H

/* [i_a] */
#define SVR_TABLE_FILE              "mailusers.tab"
#define SVR_ALIAS_FILE              "aliases.tab"

#define USER_PROFILE_FILE           "user.tab"
#define DEFAULT_USER_PROFILE_FILE   "userdef.tab"
#define MAILBOX_DIRECTORY           "mailbox"

#define POP3_LOCKS_DIR              "pop3locks"
#define MLUSERS_TABLE_FILE          "mlusers.tab"
#define MAILPROCESS_FILE            "mailproc.tab"
#define USR_DOMAIN_TMPDIR           ".tmp"
#define USR_TMPDIR                  "tmp"


/* [i_a] */
#define MAIL_DOMAINS_DIR            "domains"
#define MAIL_DOMAINS_FILE           "domains.tab"

/* [i_a] */
#define MAILDIR_DIRECTORY           "Maildir"

/* [i_a] */
#define ENV_MAIL_ROOT               "MAIL_ROOT"
#define ENV_CMD_LINE                "MAIL_CMD_LINE"
#define SVR_SHUTDOWN_FILE           ".shutdown"

#define SVR_PROFILE_FILE            "server.tab"
#define MESSAGEID_FILE              "message.id"
#define SMTP_SPOOL_DIR              "spool"

#define LOCAL_TEMP_SUBPATH      SMTP_SPOOL_DIR SYS_SLASH_STR "temp" SYS_SLASH_STR
#define LOCAL_SUBPATH           SMTP_SPOOL_DIR SYS_SLASH_STR "local" SYS_SLASH_STR


#endif
