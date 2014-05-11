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

#ifndef _SYSLISTS_H
#define _SYSLISTS_H

#define SYS_LIST_HEAD_INIT(name)        { &(name), &(name) }

#define SYS_LIST_HEAD(name)             struct SysListHead name = SYS_LIST_HEAD_INIT(name)

#define SYS_INIT_LIST_HEAD(ptr)                 \
    do {                            \
        (ptr)->pNext = (ptr); (ptr)->pPrev = (ptr); \
    } while (0)

#define SYS_INIT_LIST_LINK(ptr)                 \
    do {                            \
        (ptr)->pNext = NULL; (ptr)->pPrev = NULL;   \
    } while (0)

#define SYS_LIST_ADD(new, prev, next)           \
    do {                        \
        struct SysListHead *pPrev = prev;   \
        struct SysListHead *pNext = next;   \
        pNext->pPrev = new;         \
        (new)->pNext = pNext;           \
        (new)->pPrev = pPrev;           \
        pPrev->pNext = new;         \
    } while (0)

#define SYS_LIST_ADDH(new, head)        SYS_LIST_ADD(new, head, (head)->pNext)

#define SYS_LIST_ADDT(new, head)        SYS_LIST_ADD(new, (head)->pPrev, head)

#define SYS_LIST_UNLINK(prev, next)     \
    do {                    \
        (next)->pPrev = prev;       \
        (prev)->pNext = next;       \
    } while (0)

#define SYS_LIST_DEL(entry)                     \
    do {                                \
        SYS_LIST_UNLINK((entry)->pPrev, (entry)->pNext);    \
        (entry)->pPrev = entry;                 \
        (entry)->pNext = entry;                 \
    } while (0)

#define SYS_LIST_EMTPY(head)            ((head)->pNext == head)

#define SYS_LIST_SPLICE(list, head)                 \
    do {                                \
        struct SysListHead *first = (list)->pNext;      \
        if (first != list) {                    \
            struct SysListHead *last = (list)->pPrev;   \
            struct SysListHead *at = (head)->pNext;     \
            (first)->pPrev = head;              \
            (head)->pNext = first;              \
            (last)->pNext = at;             \
            (at)->pPrev = last;             \
            SYS_INIT_LIST_HEAD(list);           \
        }                           \
    } while (0)

#define SYS_LIST_ENTRY(ptr, type, member)   ((type *)((char *)(ptr)-(size_t)(&((type *)0)->member))) /* [i_a] */

#define SYS_LIST_FOR_EACH(pos, head)        for (pos = (head)->pNext; pos != (head); pos = (pos)->pNext)

#define SYS_LIST_FOR_EACH_SAFE(pos, n, head)                \
    for (pos = (head)->pNext, n = pos->pNext; pos != (head);    \
         pos = n, n = pos->pNext)

#define SYS_LIST_FIRST(head)                (((head)->pNext != (head)) ? (head)->pNext: NULL)

#define SYS_LIST_LAST(head)                 (((head)->pPrev != (head)) ? (head)->pPrev: NULL)

#define SYS_LIST_NEXT(pos, head)            (((pos)->pNext != (head)) ? (pos)->pNext: NULL)

#define SYS_LIST_PREV(pos, head)            (((pos)->pPrev != (head)) ? (pos)->pPrev: NULL)

#define SYS_LIST_LINKED(ptr)                (((ptr)->pPrev != NULL) && ((ptr)->pNext != NULL))

#define SYS_COPY_HEAD(dsth, srch)                   \
    do {                                \
        (dsth)->pPrev = (srch)->pPrev;              \
        (dsth)->pNext = (srch)->pNext;              \
        if ((srch)->pNext != NULL) (srch)->pNext->pPrev = dsth; \
        if ((srch)->pPrev != NULL) (srch)->pPrev->pNext = dsth; \
    } while (0)

struct SysListHead {
    struct SysListHead *pNext;
    struct SysListHead *pPrev;
};

#endif
