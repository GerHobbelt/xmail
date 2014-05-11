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

#ifndef _MD5_H
#define _MD5_H

#define MD5_DIGEST_LEN 16

typedef SYS_UINT32 md5_u32;

typedef struct s_md5_ctx {
    md5_u32 i[2];
    md5_u32 buf[4];
    unsigned char in[64];
    unsigned char digest[MD5_DIGEST_LEN];
} md5_ctx_t;

void md5_init(md5_ctx_t *mdctx);
void md5_update(md5_ctx_t *mdctx, unsigned char const *data, size_t size);
void md5_final(md5_ctx_t *mdctx);

void md5_hex(unsigned char *src, char *dst);
void do_md5_file(FILE *file, long start, long bytes, char *hash);
void do_md5_string(char const *pass, size_t passlen, char *hash);

#endif
