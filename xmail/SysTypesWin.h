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

#ifndef _SYSTYPESWIN_H
#define _SYSTYPESWIN_H

#ifdef MACH_BIG_ENDIAN_WORDS
#define BIG_ENDIAN_CPU
#endif
#ifdef MACH_BIG_ENDIAN_BITFIELD
#define BIG_ENDIAN_BITFIELD
#endif

#ifndef CHAR_BIT
#define CHAR_BIT                8
#endif

#define SYS_INFINITE_TIMEOUT    INFINITE
#define SYS_DEFAULT_MAXCOUNT    (INT_MAX - 1)

#define SYS_EOL                 "\r\n"
#define SYS_CRLF_EOL            1
#define SYS_SLASH_CHAR          '\\'
#define SYS_SLASH_STR           "\\"
#define SYS_BASE_FS_STR         "\\\\?\\"
#define SYS_MAX_PATH            _MAX_PATH

#define SYS_LLU_FMT             "%I64u"
#define SYS_LLX_FMT             "%I64X"
#define SYS_OFFT_FMT             "%I64"

#define SYS_INVALID_HANDLE      ((SYS_HANDLE) 0)
#define SYS_INVALID_SOCKET      ((SYS_SOCKET) INVALID_SOCKET)
#define SYS_INVALID_SEMAPHORE   ((SYS_SEMAPHORE) 0)
#define SYS_INVALID_MUTEX       ((SYS_MUTEX) 0)
#define SYS_INVALID_EVENT       ((SYS_EVENT) 0)
#define SYS_INVALID_PEVENT      ((SYS_PEVENT) 0)
#define SYS_INVALID_THREAD      ((SYS_THREAD) 0)
#define SYS_INVALID_MMAP        ((SYS_MMAP) 0)

#define SYS_THREAD_ONCE_INIT    { 0, 0 }

#define SysSNPrintf             _snprintf

#ifdef HAS_NO_OFFT_FSTREAM
#define Sys_fseek(f, o, w)      fseek(f, (long) (o), w)
#define Sys_ftell(f)            ftell(f)
#else
#define Sys_fseek(f, o, w)      _fseeki64(f, (__int64) (o), w)
#define Sys_ftell(f)            _ftelli64(f)
#endif

#define SYS_fd_set              fd_set
#define SYS_FD_ZERO             FD_ZERO
#define SYS_FD_CLR              FD_CLR
#define SYS_FD_SET              FD_SET
#define SYS_FD_ISSET            FD_ISSET

#define SYS_SHUT_RD             SD_RECEIVE
#define SYS_SHUT_WR             SD_SEND
#define SYS_SHUT_RDWR           SD_BOTH

typedef signed char SYS_INT8;
typedef unsigned char SYS_UINT8;
typedef signed short int SYS_INT16;
typedef unsigned short int SYS_UINT16;
typedef signed int SYS_INT32;
typedef unsigned int SYS_UINT32;
typedef signed __int64 SYS_INT64;
typedef unsigned __int64 SYS_UINT64;
typedef unsigned long SYS_HANDLE;
typedef int SYS_TLSKEY;
typedef SOCKET SYS_SOCKET;
typedef int socklen_t;
typedef HANDLE SYS_SEMAPHORE;
typedef HANDLE SYS_MUTEX;
typedef HANDLE SYS_EVENT;
typedef HANDLE SYS_PEVENT;
typedef unsigned long SYS_THREAD;
typedef void *SYS_MMAP;
typedef SYS_INT64 SYS_OFF_T;
typedef long SYS_SIZE_T;

struct SYS_THREAD_ONCE {
	LONG lOnce;
	LONG lDone;
};

#endif

