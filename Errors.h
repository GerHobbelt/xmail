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

#ifndef _ERRORS_H
#define _ERRORS_H


/* Remeber to update error strings i Errors.cpp */
enum XMailErrors {
    __ERR_SUCCESS = 0,
#define ERR_SUCCESS (-__ERR_SUCCESS)

    __ERR_SERVER_SHUTDOWN,
#define ERR_SERVER_SHUTDOWN (-__ERR_SERVER_SHUTDOWN)

    __ERR_MEMORY,
#define ERR_MEMORY (-__ERR_MEMORY)

    __ERR_NETWORK,
#define ERR_NETWORK (-__ERR_NETWORK)

    __ERR_SOCKET_CREATE,
#define ERR_SOCKET_CREATE (-__ERR_SOCKET_CREATE)

    __ERR_TIMEOUT,
#define ERR_TIMEOUT (-__ERR_TIMEOUT)

    __ERR_LOCKED,
#define ERR_LOCKED (-__ERR_LOCKED)

    __ERR_SOCKET_BIND,
#define ERR_SOCKET_BIND (-__ERR_SOCKET_BIND)

    __ERR_CONF_PATH,
#define ERR_CONF_PATH (-__ERR_CONF_PATH)

    __ERR_USERS_FILE_NOT_FOUND,
#define ERR_USERS_FILE_NOT_FOUND (-__ERR_USERS_FILE_NOT_FOUND)

    __ERR_FILE_CREATE,
#define ERR_FILE_CREATE (-__ERR_FILE_CREATE)

    __ERR__DUMMY,

    __ERR_USER_NOT_FOUND,
#define ERR_USER_NOT_FOUND (-__ERR_USER_NOT_FOUND)

    __ERR_USER_EXIST,
#define ERR_USER_EXIST (-__ERR_USER_EXIST)

    __ERR_WRITE_USERS_FILE,
#define ERR_WRITE_USERS_FILE (-__ERR_WRITE_USERS_FILE)

    __ERR_NO_USER_PRFILE,
#define ERR_NO_USER_PRFILE (-__ERR_NO_USER_PRFILE)

    __ERR_FILE_DELETE,
#define ERR_FILE_DELETE (-__ERR_FILE_DELETE)

    __ERR_DIR_CREATE,
#define ERR_DIR_CREATE (-__ERR_DIR_CREATE)

    __ERR_DIR_DELETE,
#define ERR_DIR_DELETE (-__ERR_DIR_DELETE)

    __ERR_FILE_OPEN,
#define ERR_FILE_OPEN (-__ERR_FILE_OPEN)

    __ERR_INVALID_FILE,
#define ERR_INVALID_FILE (-__ERR_INVALID_FILE)

    __ERR_FILE_WRITE,
#define ERR_FILE_WRITE (-__ERR_FILE_WRITE)

    __ERR_MSG_NOT_IN_RANGE,
#define ERR_MSG_NOT_IN_RANGE (-__ERR_MSG_NOT_IN_RANGE)

    __ERR_MSG_DELETED,
#define ERR_MSG_DELETED (-__ERR_MSG_DELETED)

    __ERR_INVALID_PASSWORD,
#define ERR_INVALID_PASSWORD (-__ERR_INVALID_PASSWORD)

    __ERR_ALIAS_FILE_NOT_FOUND,
#define ERR_ALIAS_FILE_NOT_FOUND (-__ERR_ALIAS_FILE_NOT_FOUND)

    __ERR_ALIAS_EXIST,
#define ERR_ALIAS_EXIST (-__ERR_ALIAS_EXIST)

    __ERR_WRITE_ALIAS_FILE,
#define ERR_WRITE_ALIAS_FILE (-__ERR_WRITE_ALIAS_FILE)

    __ERR_ALIAS_NOT_FOUND,
#define ERR_ALIAS_NOT_FOUND (-__ERR_ALIAS_NOT_FOUND)

    __ERR_SVR_PRFILE_NOT_LOCKED,
#define ERR_SVR_PRFILE_NOT_LOCKED (-__ERR_SVR_PRFILE_NOT_LOCKED)

    __ERR_GET_PEER_INFO,
#define ERR_GET_PEER_INFO (-__ERR_GET_PEER_INFO)

    __ERR_SMTP_PATH_PARSE_ERROR,
#define ERR_SMTP_PATH_PARSE_ERROR (-__ERR_SMTP_PATH_PARSE_ERROR)

    __ERR_BAD_RETURN_PATH,
#define ERR_BAD_RETURN_PATH (-__ERR_BAD_RETURN_PATH)

    __ERR_BAD_EMAIL_ADDR,
#define ERR_BAD_EMAIL_ADDR (-__ERR_BAD_EMAIL_ADDR)

    __ERR_RELAY_NOT_ALLOWED,
#define ERR_RELAY_NOT_ALLOWED (-__ERR_RELAY_NOT_ALLOWED)

    __ERR_BAD_FORWARD_PATH,
#define ERR_BAD_FORWARD_PATH (-__ERR_BAD_FORWARD_PATH)

    __ERR_GET_SOCK_INFO,
#define ERR_GET_SOCK_INFO (-__ERR_GET_SOCK_INFO)

    __ERR_GET_SOCK_HOST,
#define ERR_GET_SOCK_HOST (-__ERR_GET_SOCK_HOST)

    __ERR_NO_DOMAIN,
#define ERR_NO_DOMAIN (-__ERR_NO_DOMAIN)

    __ERR_USER_NOT_LOCAL,
#define ERR_USER_NOT_LOCAL (-__ERR_USER_NOT_LOCAL)

    __ERR_BAD_SERVER_ADDR,
#define ERR_BAD_SERVER_ADDR (-__ERR_BAD_SERVER_ADDR)

    __ERR_BAD_SERVER_RESPONSE,
#define ERR_BAD_SERVER_RESPONSE (-__ERR_BAD_SERVER_RESPONSE)

    __ERR_INVALID_POP3_RESPONSE,
#define ERR_INVALID_POP3_RESPONSE (-__ERR_INVALID_POP3_RESPONSE)

    __ERR_LINKS_FILE_NOT_FOUND,
#define ERR_LINKS_FILE_NOT_FOUND (-__ERR_LINKS_FILE_NOT_FOUND)

    __ERR_LINK_EXIST,
#define ERR_LINK_EXIST (-__ERR_LINK_EXIST)

    __ERR_WRITE_LINKS_FILE,
#define ERR_WRITE_LINKS_FILE (-__ERR_WRITE_LINKS_FILE)

    __ERR_LINK_NOT_FOUND,
#define ERR_LINK_NOT_FOUND (-__ERR_LINK_NOT_FOUND)

    __ERR_SMTPGW_FILE_NOT_FOUND,
#define ERR_SMTPGW_FILE_NOT_FOUND (-__ERR_SMTPGW_FILE_NOT_FOUND)

    __ERR_GATEWAY_ALREADY_EXIST,
#define ERR_GATEWAY_ALREADY_EXIST (-__ERR_GATEWAY_ALREADY_EXIST)

    __ERR_GATEWAY_NOT_FOUND,
#define ERR_GATEWAY_NOT_FOUND (-__ERR_GATEWAY_NOT_FOUND)

    __ERR_USER_NOT_MAILINGLIST,
#define ERR_USER_NOT_MAILINGLIST (-__ERR_USER_NOT_MAILINGLIST)

    __ERR_NO_USER_MLTABLE_FILE,
#define ERR_NO_USER_MLTABLE_FILE (-__ERR_NO_USER_MLTABLE_FILE)

    __ERR_MLUSER_ALREADY_EXIST,
#define ERR_MLUSER_ALREADY_EXIST (-__ERR_MLUSER_ALREADY_EXIST)

    __ERR_MLUSER_NOT_FOUND,
#define ERR_MLUSER_NOT_FOUND (-__ERR_MLUSER_NOT_FOUND)

    __ERR_SPOOL_FILE_NOT_FOUND,
#define ERR_SPOOL_FILE_NOT_FOUND (-__ERR_SPOOL_FILE_NOT_FOUND)

    __ERR_INVALID_SPOOL_FILE,
#define ERR_INVALID_SPOOL_FILE (-__ERR_INVALID_SPOOL_FILE)

    __ERR_SPOOL_FILE_EXPIRED,
#define ERR_SPOOL_FILE_EXPIRED (-__ERR_SPOOL_FILE_EXPIRED)

    __ERR_SMTPRELAY_FILE_NOT_FOUND,
#define ERR_SMTPRELAY_FILE_NOT_FOUND (-__ERR_SMTPRELAY_FILE_NOT_FOUND)

    __ERR_DOMAINS_FILE_NOT_FOUND,
#define ERR_DOMAINS_FILE_NOT_FOUND (-__ERR_DOMAINS_FILE_NOT_FOUND)

    __ERR_DOMAIN_NOT_HANDLED,
#define ERR_DOMAIN_NOT_HANDLED (-__ERR_DOMAIN_NOT_HANDLED)

    __ERR_BAD_SMTP_RESPONSE,
#define ERR_BAD_SMTP_RESPONSE (-__ERR_BAD_SMTP_RESPONSE)

    __ERR_CFG_VAR_NOT_FOUND,
#define ERR_CFG_VAR_NOT_FOUND (-__ERR_CFG_VAR_NOT_FOUND)

    __ERR_BAD_DNS_RESPONSE,
#define ERR_BAD_DNS_RESPONSE (-__ERR_BAD_DNS_RESPONSE)

    __ERR_SMTPGW_NOT_FOUND,
#define ERR_SMTPGW_NOT_FOUND (-__ERR_SMTPGW_NOT_FOUND)

    __ERR_INCOMPLETE_CONFIG,
#define ERR_INCOMPLETE_CONFIG (-__ERR_INCOMPLETE_CONFIG)

    __ERR_MAIL_ERROR_LOOP,
#define ERR_MAIL_ERROR_LOOP (-__ERR_MAIL_ERROR_LOOP)

    __ERR_EXTALIAS_FILE_NOT_FOUND,
#define ERR_EXTALIAS_FILE_NOT_FOUND (-__ERR_EXTALIAS_FILE_NOT_FOUND)

    __ERR_EXTALIAS_EXIST,
#define ERR_EXTALIAS_EXIST (-__ERR_EXTALIAS_EXIST)

    __ERR_WRITE_EXTALIAS_FILE,
#define ERR_WRITE_EXTALIAS_FILE (-__ERR_WRITE_EXTALIAS_FILE)

    __ERR_EXTALIAS_NOT_FOUND,
#define ERR_EXTALIAS_NOT_FOUND (-__ERR_EXTALIAS_NOT_FOUND)

    __ERR_NO_USER_DEFAULT_PRFILE,
#define ERR_NO_USER_DEFAULT_PRFILE (-__ERR_NO_USER_DEFAULT_PRFILE)

    __ERR_FINGER_QUERY_FORMAT,
#define ERR_FINGER_QUERY_FORMAT (-__ERR_FINGER_QUERY_FORMAT)

    __ERR_LOCKED_RESOURCE,
#define ERR_LOCKED_RESOURCE (-__ERR_LOCKED_RESOURCE)

    __ERR_NO_PREDEFINED_MX,
#define ERR_NO_PREDEFINED_MX (-__ERR_NO_PREDEFINED_MX)

    __ERR_NO_MORE_MXRECORDS,
#define ERR_NO_MORE_MXRECORDS (-__ERR_NO_MORE_MXRECORDS)

    __ERR_INVALID_MESSAGE_FORMAT,
#define ERR_INVALID_MESSAGE_FORMAT (-__ERR_INVALID_MESSAGE_FORMAT)

    __ERR_SMTP_BAD_MAIL_FROM,
#define ERR_SMTP_BAD_MAIL_FROM (-__ERR_SMTP_BAD_MAIL_FROM)

    __ERR_SMTP_BAD_RCPT_TO,
#define ERR_SMTP_BAD_RCPT_TO (-__ERR_SMTP_BAD_RCPT_TO)

    __ERR_SMTP_BAD_DATA,
#define ERR_SMTP_BAD_DATA (-__ERR_SMTP_BAD_DATA)

    __ERR_INVALID_MXRECS_STRING,
#define ERR_INVALID_MXRECS_STRING (-__ERR_INVALID_MXRECS_STRING)

    __ERR_SETSOCKOPT,
#define ERR_SETSOCKOPT (-__ERR_SETSOCKOPT)

    __ERR_CREATEEVENT,
#define ERR_CREATEEVENT (-__ERR_CREATEEVENT)

    __ERR_CREATESEMAPHORE,
#define ERR_CREATESEMAPHORE (-__ERR_CREATESEMAPHORE)

    __ERR_CLOSEHANDLE,
#define ERR_CLOSEHANDLE (-__ERR_CLOSEHANDLE)

    __ERR_RELEASESEMAPHORE,
#define ERR_RELEASESEMAPHORE (-__ERR_RELEASESEMAPHORE)

    __ERR_BEGINTHREADEX,
#define ERR_BEGINTHREADEX (-__ERR_BEGINTHREADEX)

    __ERR_CREATEFILEMAPPING,
#define ERR_CREATEFILEMAPPING (-__ERR_CREATEFILEMAPPING)

    __ERR_MAPVIEWOFFILE,
#define ERR_MAPVIEWOFFILE (-__ERR_MAPVIEWOFFILE)

    __ERR_UNMAPVIEWOFFILE,
#define ERR_UNMAPVIEWOFFILE (-__ERR_UNMAPVIEWOFFILE)

    __ERR_SEMGET,
#define ERR_SEMGET (-__ERR_SEMGET)

    __ERR_SEMCTL,
#define ERR_SEMCTL (-__ERR_SEMCTL)

    __ERR_SEMOP,
#define ERR_SEMOP (-__ERR_SEMOP)

    __ERR_FORK,
#define ERR_FORK (-__ERR_FORK)

    __ERR_SHMGET,
#define ERR_SHMGET (-__ERR_SHMGET)

    __ERR_SHMCTL,
#define ERR_SHMCTL (-__ERR_SHMCTL)

    __ERR_SHMAT,
#define ERR_SHMAT (-__ERR_SHMAT)

    __ERR_SHMDT,
#define ERR_SHMDT (-__ERR_SHMDT)

    __ERR_OPENDIR,
#define ERR_OPENDIR (-__ERR_OPENDIR)

    __ERR_STAT,
#define ERR_STAT (-__ERR_STAT)

    __ERR_SMTP_BAD_CMD_SEQUENCE,
#define ERR_SMTP_BAD_CMD_SEQUENCE (-__ERR_SMTP_BAD_CMD_SEQUENCE)

    __ERR_NO_ROOT_DOMAIN_VAR,
#define ERR_NO_ROOT_DOMAIN_VAR (-__ERR_NO_ROOT_DOMAIN_VAR)

    __ERR_NS_NOT_FOUND,
#define ERR_NS_NOT_FOUND (-__ERR_NS_NOT_FOUND)

    __ERR_NO_DEFINED_MXS_FOR_DOMAIN,
#define ERR_NO_DEFINED_MXS_FOR_DOMAIN (-__ERR_NO_DEFINED_MXS_FOR_DOMAIN)

    __ERR_BAD_CTRL_COMMAND,
#define ERR_BAD_CTRL_COMMAND (-__ERR_BAD_CTRL_COMMAND)

    __ERR_DOMAIN_ALREADY_HANDLED,
#define ERR_DOMAIN_ALREADY_HANDLED (-__ERR_DOMAIN_ALREADY_HANDLED)

    __ERR_BAD_CTRL_LOGIN,
#define ERR_BAD_CTRL_LOGIN (-__ERR_BAD_CTRL_LOGIN)

    __ERR_CTRL_ACCOUNTS_FILE_NOT_FOUND,
#define ERR_CTRL_ACCOUNTS_FILE_NOT_FOUND (-__ERR_CTRL_ACCOUNTS_FILE_NOT_FOUND)

    __ERR_SPAMMER_IP,
#define ERR_SPAMMER_IP (-__ERR_SPAMMER_IP)

    __ERR_TRUNCATED_DGRAM_DNS_RESPONSE,
#define ERR_TRUNCATED_DGRAM_DNS_RESPONSE (-__ERR_TRUNCATED_DGRAM_DNS_RESPONSE)

    __ERR_NO_DGRAM_DNS_RESPONSE,
#define ERR_NO_DGRAM_DNS_RESPONSE (-__ERR_NO_DGRAM_DNS_RESPONSE)

    __ERR_DNS_NOTFOUND,
#define ERR_DNS_NOTFOUND (-__ERR_DNS_NOTFOUND)

    __ERR_BAD_SMARTDNSHOST_SYNTAX,
#define ERR_BAD_SMARTDNSHOST_SYNTAX (-__ERR_BAD_SMARTDNSHOST_SYNTAX)

    __ERR_MAILBOX_SIZE,
#define ERR_MAILBOX_SIZE (-__ERR_MAILBOX_SIZE)

    __ERR_DYNDNS_CONFIG,
#define ERR_DYNDNS_CONFIG (-__ERR_DYNDNS_CONFIG)

    __ERR_PROCESS_EXECUTE,
#define ERR_PROCESS_EXECUTE (-__ERR_PROCESS_EXECUTE)

    __ERR_BAD_MAILPROC_CMD_SYNTAX,
#define ERR_BAD_MAILPROC_CMD_SYNTAX (-__ERR_BAD_MAILPROC_CMD_SYNTAX)

    __ERR_NO_MAILPROC_FILE,
#define ERR_NO_MAILPROC_FILE (-__ERR_NO_MAILPROC_FILE)

    __ERR_DNS_RECURSION_NOT_AVAILABLE,
#define ERR_DNS_RECURSION_NOT_AVAILABLE (-__ERR_DNS_RECURSION_NOT_AVAILABLE)

    __ERR_POP3_EXTERNAL_LINK_DISABLED,
#define ERR_POP3_EXTERNAL_LINK_DISABLED (-__ERR_POP3_EXTERNAL_LINK_DISABLED)

    __ERR_BAD_DOMAIN_PROC_CMD_SYNTAX,
#define ERR_BAD_DOMAIN_PROC_CMD_SYNTAX (-__ERR_BAD_DOMAIN_PROC_CMD_SYNTAX)

    __ERR_NOT_A_CUSTOM_DOMAIN,
#define ERR_NOT_A_CUSTOM_DOMAIN (-__ERR_NOT_A_CUSTOM_DOMAIN)

    __ERR_NO_MORE_TOKENS,
#define ERR_NO_MORE_TOKENS (-__ERR_NO_MORE_TOKENS)

    __ERR_SELECT,
#define ERR_SELECT (-__ERR_SELECT)

    __ERR_REGISTER_EVENT_SOURCE,
#define ERR_REGISTER_EVENT_SOURCE (-__ERR_REGISTER_EVENT_SOURCE)

    __ERR_NOMORESEMS,
#define ERR_NOMORESEMS (-__ERR_NOMORESEMS)

    __ERR_INVALID_SEMAPHORE,
#define ERR_INVALID_SEMAPHORE (-__ERR_INVALID_SEMAPHORE)

    __ERR_SHMEM_ALREADY_EXIST,
#define ERR_SHMEM_ALREADY_EXIST (-__ERR_SHMEM_ALREADY_EXIST)

    __ERR_SHMEM_NOT_EXIST,
#define ERR_SHMEM_NOT_EXIST (-__ERR_SHMEM_NOT_EXIST)

    __ERR_SEM_NOT_EXIST,
#define ERR_SEM_NOT_EXIST (-__ERR_SEM_NOT_EXIST)

    __ERR_SERVER_BUSY,
#define ERR_SERVER_BUSY (-__ERR_SERVER_BUSY)

    __ERR_IP_NOT_ALLOWED,
#define ERR_IP_NOT_ALLOWED (-__ERR_IP_NOT_ALLOWED)

    __ERR_FILE_EOF,
#define ERR_FILE_EOF (-__ERR_FILE_EOF)

    __ERR_BAD_TAG_ADDRESS,
#define ERR_BAD_TAG_ADDRESS (-__ERR_BAD_TAG_ADDRESS)

    __ERR_MAILFROM_UNKNOWN,
#define ERR_MAILFROM_UNKNOWN (-__ERR_MAILFROM_UNKNOWN)

    __ERR_FILTERED_MESSAGE,
#define ERR_FILTERED_MESSAGE (-__ERR_FILTERED_MESSAGE)

    __ERR_NO_DOMAIN_FILTER,
#define ERR_NO_DOMAIN_FILTER (-__ERR_NO_DOMAIN_FILTER)

    __ERR_POP3_RETR_BROKEN,
#define ERR_POP3_RETR_BROKEN (-__ERR_POP3_RETR_BROKEN)

    __ERR_CCLN_INVALID_RESPONSE,
#define ERR_CCLN_INVALID_RESPONSE (-__ERR_CCLN_INVALID_RESPONSE)

    __ERR_CCLN_ERROR_RESPONSE,
#define ERR_CCLN_ERROR_RESPONSE (-__ERR_CCLN_ERROR_RESPONSE)

    __ERR_INCOMPLETE_PROCESSING,
#define ERR_INCOMPLETE_PROCESSING (-__ERR_INCOMPLETE_PROCESSING)

    __ERR_NO_EXTERNAL_AUTH_DEFINED,
#define ERR_NO_EXTERNAL_AUTH_DEFINED (-__ERR_NO_EXTERNAL_AUTH_DEFINED)

    __ERR_EXTERNAL_AUTH_FAILURE,
#define ERR_EXTERNAL_AUTH_FAILURE (-__ERR_EXTERNAL_AUTH_FAILURE)

    __ERR_MD5_AUTH_FAILED,
#define ERR_MD5_AUTH_FAILED (-__ERR_MD5_AUTH_FAILED)

    __ERR_NO_SMTP_AUTH_CONFIG,
#define ERR_NO_SMTP_AUTH_CONFIG (-__ERR_NO_SMTP_AUTH_CONFIG)

    __ERR_UNKNOWN_SMTP_AUTH,
#define ERR_UNKNOWN_SMTP_AUTH (-__ERR_UNKNOWN_SMTP_AUTH)

    __ERR_BAD_SMTP_AUTH_CONFIG,
#define ERR_BAD_SMTP_AUTH_CONFIG (-__ERR_BAD_SMTP_AUTH_CONFIG)

    __ERR_BAD_EXTRNPRG_EXITCODE,
#define ERR_BAD_EXTRNPRG_EXITCODE (-__ERR_BAD_EXTRNPRG_EXITCODE)

    __ERR_BAD_SMTP_CMD_SYNTAX,
#define ERR_BAD_SMTP_CMD_SYNTAX (-__ERR_BAD_SMTP_CMD_SYNTAX)

    __ERR_SMTP_AUTH_FAILED,
#define ERR_SMTP_AUTH_FAILED (-__ERR_SMTP_AUTH_FAILED)

    __ERR_BAD_SMTP_EXTAUTH_RESPONSE_FILE,
#define ERR_BAD_SMTP_EXTAUTH_RESPONSE_FILE (-__ERR_BAD_SMTP_EXTAUTH_RESPONSE_FILE)

    __ERR_SMTP_USE_FORBIDDEN,
#define ERR_SMTP_USE_FORBIDDEN (-__ERR_SMTP_USE_FORBIDDEN)

    __ERR_SPAM_ADDRESS,
#define ERR_SPAM_ADDRESS (-__ERR_SPAM_ADDRESS)

    __ERR_SOCK_NOMORE_DATA,
#define ERR_SOCK_NOMORE_DATA (-__ERR_SOCK_NOMORE_DATA)

    __ERR_BAD_TAB_INDEX_FIELD,
#define ERR_BAD_TAB_INDEX_FIELD (-__ERR_BAD_TAB_INDEX_FIELD)

    __ERR_FILE_READ,
#define ERR_FILE_READ (-__ERR_FILE_READ)

    __ERR_BAD_INDEX_FILE,
#define ERR_BAD_INDEX_FILE (-__ERR_BAD_INDEX_FILE)

    __ERR_INDEX_HASH_NOT_FOUND,
#define ERR_INDEX_HASH_NOT_FOUND (-__ERR_INDEX_HASH_NOT_FOUND)

    __ERR_RECORD_NOT_FOUND,
#define ERR_RECORD_NOT_FOUND (-__ERR_RECORD_NOT_FOUND)

    __ERR_HEAP_ALLOC,
#define ERR_HEAP_ALLOC (-__ERR_HEAP_ALLOC)

    __ERR_HEAP_FREE,
#define ERR_HEAP_FREE (-__ERR_HEAP_FREE)

    __ERR_RESOURCE_NOT_LOCKED,
#define ERR_RESOURCE_NOT_LOCKED (-__ERR_RESOURCE_NOT_LOCKED)

    __ERR_LOCK_ENTRY_NOT_FOUND,
#define ERR_LOCK_ENTRY_NOT_FOUND (-__ERR_LOCK_ENTRY_NOT_FOUND)

    __ERR_LINE_TOO_LONG,
#define ERR_LINE_TOO_LONG (-__ERR_LINE_TOO_LONG)

    __ERR_MAIL_LOOP_DETECTED,
#define ERR_MAIL_LOOP_DETECTED (-__ERR_MAIL_LOOP_DETECTED)

    __ERR_FILE_MOVE,
#define ERR_FILE_MOVE (-__ERR_FILE_MOVE)

    __ERR_INVALID_MAILDIR_SUBPATH,
#define ERR_INVALID_MAILDIR_SUBPATH (-__ERR_INVALID_MAILDIR_SUBPATH)

    __ERR_SMTP_TOO_MANY_RECIPIENTS,
#define ERR_SMTP_TOO_MANY_RECIPIENTS (-__ERR_SMTP_TOO_MANY_RECIPIENTS)

    __ERR_DNS_CACHE_FILE_FMT,
#define ERR_DNS_CACHE_FILE_FMT (-__ERR_DNS_CACHE_FILE_FMT)

    __ERR_DNS_CACHE_FILE_EXPIRED,
#define ERR_DNS_CACHE_FILE_EXPIRED (-__ERR_DNS_CACHE_FILE_EXPIRED)

    __ERR_MMAP,
#define ERR_MMAP (-__ERR_MMAP)

    __ERR_NOT_LOCKED,
#define ERR_NOT_LOCKED (-__ERR_NOT_LOCKED)

    __ERR_SMTPFWD_FILE_NOT_FOUND,
#define ERR_SMTPFWD_FILE_NOT_FOUND (-__ERR_SMTPFWD_FILE_NOT_FOUND)

    __ERR_SMTPFWD_NOT_FOUND,
#define ERR_SMTPFWD_NOT_FOUND (-__ERR_SMTPFWD_NOT_FOUND)

    __ERR_USER_BREAK,
#define ERR_USER_BREAK (-__ERR_USER_BREAK)

    __ERR_SET_THREAD_PRIORITY,
#define ERR_SET_THREAD_PRIORITY (-__ERR_SET_THREAD_PRIORITY)

    __ERR_NULL_SENDER,
#define ERR_NULL_SENDER (-__ERR_NULL_SENDER)

    __ERR_RCPTTO_UNKNOWN,
#define ERR_RCPTTO_UNKNOWN (-__ERR_RCPTTO_UNKNOWN)

    __ERR_LOADMODULE,
#define ERR_LOADMODULE (-__ERR_LOADMODULE)

    __ERR_LOADMODULESYMBOL,
#define ERR_LOADMODULESYMBOL (-__ERR_LOADMODULESYMBOL)

    __ERR_NOMORE_TLSKEYS,
#define ERR_NOMORE_TLSKEYS (-__ERR_NOMORE_TLSKEYS)

    __ERR_INVALID_TLSKEY,
#define ERR_INVALID_TLSKEY (-__ERR_INVALID_TLSKEY)

    __ERR_ERRORINIT_FAILED,
#define ERR_ERRORINIT_FAILED (-__ERR_ERRORINIT_FAILED)

    __ERR_SENDFILE,
#define ERR_SENDFILE (-__ERR_SENDFILE)

    __ERR_MUTEXINIT,
#define ERR_MUTEXINIT (-__ERR_MUTEXINIT)

    __ERR_CONDINIT,
#define ERR_CONDINIT (-__ERR_CONDINIT)

    __ERR_THREADCREATE,
#define ERR_THREADCREATE (-__ERR_THREADCREATE)

    __ERR_CREATEMUTEX,
#define ERR_CREATEMUTEX (-__ERR_CREATEMUTEX)

    __ERR_NO_LOCAL_SPOOL_FILES,
#define ERR_NO_LOCAL_SPOOL_FILES (-__ERR_NO_LOCAL_SPOOL_FILES)

    __ERR_NO_HANDLED_DOMAIN,
#define ERR_NO_HANDLED_DOMAIN (-__ERR_NO_HANDLED_DOMAIN)

    __ERR_INVALID_MAIL_DOMAIN,
#define ERR_INVALID_MAIL_DOMAIN (-__ERR_INVALID_MAIL_DOMAIN)

    __ERR_BAD_CMDSTR_CHARS,
#define ERR_BAD_CMDSTR_CHARS (-__ERR_BAD_CMDSTR_CHARS)

    __ERR_FETCHMSG_UNDELIVERED,
#define ERR_FETCHMSG_UNDELIVERED (-__ERR_FETCHMSG_UNDELIVERED)

    __ERR_USER_VAR_NOT_FOUND,
#define ERR_USER_VAR_NOT_FOUND (-__ERR_USER_VAR_NOT_FOUND)

    __ERR_NO_POP3_IP,
#define ERR_NO_POP3_IP (-__ERR_NO_POP3_IP)

    __ERR_NO_MESSAGE_FILE,
#define ERR_NO_MESSAGE_FILE (-__ERR_NO_MESSAGE_FILE)

    __ERR_GET_DISK_SPACE_INFO,
#define ERR_GET_DISK_SPACE_INFO (-__ERR_GET_DISK_SPACE_INFO)

    __ERR_GET_MEMORY_INFO,
#define ERR_GET_MEMORY_INFO (-__ERR_GET_MEMORY_INFO)

    __ERR_LOW_DISK_SPACE,
#define ERR_LOW_DISK_SPACE (-__ERR_LOW_DISK_SPACE)

    __ERR_LOW_VM_SPACE,
#define ERR_LOW_VM_SPACE (-__ERR_LOW_VM_SPACE)

    __ERR_USER_DISABLED,
#define ERR_USER_DISABLED (-__ERR_USER_DISABLED)

    __ERR_BAD_DNS_NAME_RECORD,
#define ERR_BAD_DNS_NAME_RECORD (-__ERR_BAD_DNS_NAME_RECORD)

    __ERR_MESSAGE_SIZE,
#define ERR_MESSAGE_SIZE (-__ERR_MESSAGE_SIZE)

    __ERR_SMTPSRV_MSG_SIZE,
#define ERR_SMTPSRV_MSG_SIZE (-__ERR_SMTPSRV_MSG_SIZE)

    __ERR_MAPS_CONTAINED,
#define ERR_MAPS_CONTAINED (-__ERR_MAPS_CONTAINED)

    __ERR_ADOMAIN_FILE_NOT_FOUND,
#define ERR_ADOMAIN_FILE_NOT_FOUND (-__ERR_ADOMAIN_FILE_NOT_FOUND)

    __ERR_ADOMAIN_EXIST,
#define ERR_ADOMAIN_EXIST (-__ERR_ADOMAIN_EXIST)

    __ERR_ADOMAIN_NOT_FOUND,
#define ERR_ADOMAIN_NOT_FOUND (-__ERR_ADOMAIN_NOT_FOUND)

    __ERR_NOT_A_CMD_ALIAS,
#define ERR_NOT_A_CMD_ALIAS (-__ERR_NOT_A_CMD_ALIAS)

    __ERR_GETSOCKOPT,
#define ERR_GETSOCKOPT (-__ERR_GETSOCKOPT)

    __ERR_NO_HDR_FETCH_TAGS,
#define ERR_NO_HDR_FETCH_TAGS (-__ERR_NO_HDR_FETCH_TAGS)

    __ERR_SET_FILE_TIME,
#define ERR_SET_FILE_TIME (-__ERR_SET_FILE_TIME)

    __ERR_LISTDIR_NOT_FOUND,
#define ERR_LISTDIR_NOT_FOUND (-__ERR_LISTDIR_NOT_FOUND)

    __ERR_DUPLICATE_HANDLE,
#define ERR_DUPLICATE_HANDLE (-__ERR_DUPLICATE_HANDLE)

    __ERR_EMPTY_LOG,
#define ERR_EMPTY_LOG (-__ERR_EMPTY_LOG)

    __ERR_BAD_RELATIVE_PATH,
#define ERR_BAD_RELATIVE_PATH (-__ERR_BAD_RELATIVE_PATH)

    __ERR_DNS_NXDOMAIN,
#define ERR_DNS_NXDOMAIN (-__ERR_DNS_NXDOMAIN)

    __ERR_BAD_RFCNAME,
#define ERR_BAD_RFCNAME (-__ERR_BAD_RFCNAME)

    __ERR_CONNECT,
#define ERR_CONNECT (-__ERR_CONNECT)

    __ERR_MESSAGE_DELETED,
#define ERR_MESSAGE_DELETED (-__ERR_MESSAGE_DELETED)

    __ERR_PIPE,
#define ERR_PIPE (-__ERR_PIPE)

    __ERR_WAITPID,
#define ERR_WAITPID (-__ERR_WAITPID)

    __ERR_MUNMAP,
#define ERR_MUNMAP (-__ERR_MUNMAP)

    __ERR_INVALID_MMAP_OFFSET,
#define ERR_INVALID_MMAP_OFFSET (-__ERR_INVALID_MMAP_OFFSET)

    __ERR_UNMAPFILEVIEW,
#define ERR_UNMAPFILEVIEW (-__ERR_UNMAPFILEVIEW)

    __ERR_INVALID_IMAP_LINE,
#define ERR_INVALID_IMAP_LINE (-__ERR_INVALID_IMAP_LINE)

    __ERR_IMAP_RESP_NO,
#define ERR_IMAP_RESP_NO (-__ERR_IMAP_RESP_NO)

    __ERR_IMAP_RESP_BAD,
#define ERR_IMAP_RESP_BAD (-__ERR_IMAP_RESP_BAD)

    __ERR_IMAP_RESP_BYE,
#define ERR_IMAP_RESP_BYE (-__ERR_IMAP_RESP_BYE)

    __ERR_IMAP_UNKNOWN_AUTH,
#define ERR_IMAP_UNKNOWN_AUTH (-__ERR_IMAP_UNKNOWN_AUTH)

    __ERR_NO_MESSAGE_AUTH,
#define ERR_NO_MESSAGE_AUTH (-__ERR_NO_MESSAGE_AUTH)

    __ERR_INVALID_PARAMETER,
#define ERR_INVALID_PARAMETER (-__ERR_INVALID_PARAMETER)

    __ERR_ALREADY_EXIST,
#define ERR_ALREADY_EXIST (-__ERR_ALREADY_EXIST)

    __ERR_SSLCTX_CREATE,
#define ERR_SSLCTX_CREATE (-__ERR_SSLCTX_CREATE)

    __ERR_SSL_CREATE,
#define ERR_SSL_CREATE (-__ERR_SSL_CREATE)

    __ERR_SSL_CONNECT,
#define ERR_SSL_CONNECT (-__ERR_SSL_CONNECT)

    __ERR_SSL_SETCERT,
#define ERR_SSL_SETCERT (-__ERR_SSL_SETCERT)

    __ERR_SSL_SETKEY,
#define ERR_SSL_SETKEY (-__ERR_SSL_SETKEY)

    __ERR_SSL_READ,
#define ERR_SSL_READ (-__ERR_SSL_READ)

    __ERR_SSL_WRITE,
#define ERR_SSL_WRITE (-__ERR_SSL_WRITE)

    __ERR_SSL_CERT_VALIDATE,
#define ERR_SSL_CERT_VALIDATE (-__ERR_SSL_CERT_VALIDATE)

    __ERR_SSL_NOCERT,
#define ERR_SSL_NOCERT (-__ERR_SSL_NOCERT)

    __ERR_NO_REMOTE_SSL,
#define ERR_NO_REMOTE_SSL (-__ERR_NO_REMOTE_SSL)

    __ERR_SSL_ALREADY_ACTIVE,
#define ERR_SSL_ALREADY_ACTIVE (-__ERR_SSL_ALREADY_ACTIVE)

    __ERR_SSL_CHECKKEY,
#define ERR_SSL_CHECKKEY (-__ERR_SSL_CHECKKEY)

    __ERR_SSL_VERPATHS,
#define ERR_SSL_VERPATHS (-__ERR_SSL_VERPATHS)

    __ERR_TLS_MODE_REQUIRED,
#define ERR_TLS_MODE_REQUIRED (-__ERR_TLS_MODE_REQUIRED)

    __ERR_NOT_FOUND,
#define ERR_NOT_FOUND (-__ERR_NOT_FOUND)

    __ERR_NOREMOTE_POP3_UIDL,
#define ERR_NOREMOTE_POP3_UIDL (-__ERR_NOREMOTE_POP3_UIDL)

    __ERR_CORRUPTED,
#define ERR_CORRUPTED (-__ERR_CORRUPTED)

    __ERR_SSL_DISABLED,
#define ERR_SSL_DISABLED (-__ERR_SSL_DISABLED)

    __ERR_SSL_ACCEPT,
#define ERR_SSL_ACCEPT (-__ERR_SSL_ACCEPT)

    __ERR_BAD_SEQUENCE,
#define ERR_BAD_SEQUENCE (-__ERR_BAD_SEQUENCE)

    __ERR_EMPTY_ADDRESS,
#define ERR_EMPTY_ADDRESS (-__ERR_EMPTY_ADDRESS)

    __ERR_DNS_MAXDEPTH,
#define ERR_DNS_MAXDEPTH (-__ERR_DNS_MAXDEPTH)

    __ERR_THREAD_SETSTACK,
#define ERR_THREAD_SETSTACK (-__ERR_THREAD_SETSTACK)

    __ERR_DNS_FORMAT,
#define ERR_DNS_FORMAT (-__ERR_DNS_FORMAT)

    __ERR_DNS_SVRFAIL,
#define ERR_DNS_SVRFAIL (-__ERR_DNS_SVRFAIL)

    __ERR_DNS_NOTSUPPORTED,
#define ERR_DNS_NOTSUPPORTED (-__ERR_DNS_NOTSUPPORTED)

    __ERR_DNS_REFUSED,
#define ERR_DNS_REFUSED (-__ERR_DNS_REFUSED)

    __ERR_INVALID_RELAY_ADDRESS,
#define ERR_INVALID_RELAY_ADDRESS (-__ERR_INVALID_RELAY_ADDRESS)

    __ERR_INVALID_HOSTNAME,
#define ERR_INVALID_HOSTNAME (-__ERR_INVALID_HOSTNAME)

    __ERR_INVALID_INET_ADDR,
#define ERR_INVALID_INET_ADDR (-__ERR_INVALID_INET_ADDR)

    __ERR_SOCKET_SHUTDOWN,
#define ERR_SOCKET_SHUTDOWN (-__ERR_SOCKET_SHUTDOWN)

    __ERR_SSL_SHUTDOWN,
#define ERR_SSL_SHUTDOWN (-__ERR_SSL_SHUTDOWN)

        __ERR_I_ARG_TOO_MANY_IP_PORT_COMBOS,
#define ERR_I_ARG_TOO_MANY_IP_PORT_COMBOS (-__ERR_I_ARG_TOO_MANY_IP_PORT_COMBOS)

    __ERR_TOO_MANY_ELEMENTS,
#define ERR_TOO_MANY_ELEMENTS (-__ERR_TOO_MANY_ELEMENTS)

    __ERR_GET_RAND_BYTES,
#define ERR_GET_RAND_BYTES (-__ERR_GET_RAND_BYTES)

    __ERR_WAIT,
#define ERR_WAIT (-__ERR_WAIT)

    __ERR_EVENTFD,
#define ERR_EVENTFD (-__ERR_EVENTFD)

    __ERR_TOO_BIG,
#define ERR_TOO_BIG (-__ERR_TOO_BIG)

    ERROR_COUNT
};

int ErrGetErrorCode(void);
int ErrSetErrorCode(int iError, char const *pszInfo = NULL, int iSize = -1);
const char *ErrGetErrorString(int iError);
const char *ErrGetErrorString(void);
char *ErrGetErrorStringInfo(int iError);
int ErrLogMessage(int iLogLevel, char const *pszFormat, ...);
int ErrFileLogString(char const *pszFileName, char const *pszMessage);

#endif

