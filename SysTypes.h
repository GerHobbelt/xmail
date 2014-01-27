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

#ifndef _SYSTYPES_H
#define _SYSTYPES_H

#ifdef WIN32

#include "SysTypesWin.h"

#else				// #ifdef WIN32
#ifdef __LINUX__

#include "SysTypesLinux.h"

#else				// #ifdef __LINUX__
#ifdef __SOLARIS__

#include "SysTypesSolaris.h"

#else				// #ifdef __SOLARIS__
#ifdef __BSD__

#include "SysTypesBSD.h"

#else				// #ifdef __BSD__

#error System type not defined

#endif				// #ifdef __BSD__
#endif				// #ifdef __SOLARIS__
#endif				// #ifdef __LINUX__
#endif				// #ifdef WIN32

#endif
