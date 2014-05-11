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

#include "SysInclude.h"
#include "SysDep.h"
#include "SvrDefines.h"
#include "MD5.h"


#define MD5_FTRANS(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_GTRANS(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_HTRANS(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_ITRANS(x, y, z) ((y) ^ ((x) | (~z)))

#define MD5_LROT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FSTEP(a, b, c, d, x, s, ac)                 \
    {                               \
        (a) += MD5_FTRANS((b), (c), (d)) + (x) + (md5_u32) (ac); \
        (a) = MD5_LROT((a), (s));               \
        (a) += (b);                     \
    }
#define MD5_GSTEP(a, b, c, d, x, s, ac)                 \
    {                               \
        (a) += MD5_GTRANS((b), (c), (d)) + (x) + (md5_u32) (ac); \
        (a) = MD5_LROT((a), (s));               \
        (a) += (b);                     \
    }
#define MD5_HSTEP(a, b, c, d, x, s, ac)                 \
    {                               \
        (a) += MD5_HTRANS((b), (c), (d)) + (x) + (md5_u32) (ac); \
        (a) = MD5_LROT((a), (s));               \
        (a) += (b);                     \
    }
#define MD5_ISTEP(a, b, c, d, x, s, ac)                 \
    {                               \
        (a) += MD5_ITRANS((b), (c), (d)) + (x) + (md5_u32) (ac); \
        (a) = MD5_LROT((a), (s));               \
        (a) += (b);                     \
    }

static void md5_transform(md5_u32 *buf, md5_u32 const *in)
{
    md5_u32 a = buf[0], b = buf[1], c = buf[2], d = buf[3];

    /* Round 1 */
#define S11 7
#define S12 12
#define S13 17
#define S14 22
    MD5_FSTEP(a, b, c, d, in[ 0], S11, 3614090360U); /* 1 */
    MD5_FSTEP(d, a, b, c, in[ 1], S12, 3905402710U); /* 2 */
    MD5_FSTEP(c, d, a, b, in[ 2], S13,  606105819U); /* 3 */
    MD5_FSTEP(b, c, d, a, in[ 3], S14, 3250441966U); /* 4 */
    MD5_FSTEP(a, b, c, d, in[ 4], S11, 4118548399U); /* 5 */
    MD5_FSTEP(d, a, b, c, in[ 5], S12, 1200080426U); /* 6 */
    MD5_FSTEP(c, d, a, b, in[ 6], S13, 2821735955U); /* 7 */
    MD5_FSTEP(b, c, d, a, in[ 7], S14, 4249261313U); /* 8 */
    MD5_FSTEP(a, b, c, d, in[ 8], S11, 1770035416U); /* 9 */
    MD5_FSTEP(d, a, b, c, in[ 9], S12, 2336552879U); /* 10 */
    MD5_FSTEP(c, d, a, b, in[10], S13, 4294925233U); /* 11 */
    MD5_FSTEP(b, c, d, a, in[11], S14, 2304563134U); /* 12 */
    MD5_FSTEP(a, b, c, d, in[12], S11, 1804603682U); /* 13 */
    MD5_FSTEP(d, a, b, c, in[13], S12, 4254626195U); /* 14 */
    MD5_FSTEP(c, d, a, b, in[14], S13, 2792965006U); /* 15 */
    MD5_FSTEP(b, c, d, a, in[15], S14, 1236535329U); /* 16 */

    /* Round 2 */
#define S21 5
#define S22 9
#define S23 14
#define S24 20
    MD5_GSTEP(a, b, c, d, in[ 1], S21, 4129170786U); /* 17 */
    MD5_GSTEP(d, a, b, c, in[ 6], S22, 3225465664U); /* 18 */
    MD5_GSTEP(c, d, a, b, in[11], S23,  643717713U); /* 19 */
    MD5_GSTEP(b, c, d, a, in[ 0], S24, 3921069994U); /* 20 */
    MD5_GSTEP(a, b, c, d, in[ 5], S21, 3593408605U); /* 21 */
    MD5_GSTEP(d, a, b, c, in[10], S22,   38016083U); /* 22 */
    MD5_GSTEP(c, d, a, b, in[15], S23, 3634488961U); /* 23 */
    MD5_GSTEP(b, c, d, a, in[ 4], S24, 3889429448U); /* 24 */
    MD5_GSTEP(a, b, c, d, in[ 9], S21,  568446438U); /* 25 */
    MD5_GSTEP(d, a, b, c, in[14], S22, 3275163606U); /* 26 */
    MD5_GSTEP(c, d, a, b, in[ 3], S23, 4107603335U); /* 27 */
    MD5_GSTEP(b, c, d, a, in[ 8], S24, 1163531501U); /* 28 */
    MD5_GSTEP(a, b, c, d, in[13], S21, 2850285829U); /* 29 */
    MD5_GSTEP(d, a, b, c, in[ 2], S22, 4243563512U); /* 30 */
    MD5_GSTEP(c, d, a, b, in[ 7], S23, 1735328473U); /* 31 */
    MD5_GSTEP(b, c, d, a, in[12], S24, 2368359562U); /* 32 */

    /* Round 3 */
#define S31 4
#define S32 11
#define S33 16
#define S34 23
    MD5_HSTEP(a, b, c, d, in[ 5], S31, 4294588738U); /* 33 */
    MD5_HSTEP(d, a, b, c, in[ 8], S32, 2272392833U); /* 34 */
    MD5_HSTEP(c, d, a, b, in[11], S33, 1839030562U); /* 35 */
    MD5_HSTEP(b, c, d, a, in[14], S34, 4259657740U); /* 36 */
    MD5_HSTEP(a, b, c, d, in[ 1], S31, 2763975236U); /* 37 */
    MD5_HSTEP(d, a, b, c, in[ 4], S32, 1272893353U); /* 38 */
    MD5_HSTEP(c, d, a, b, in[ 7], S33, 4139469664U); /* 39 */
    MD5_HSTEP(b, c, d, a, in[10], S34, 3200236656U); /* 40 */
    MD5_HSTEP(a, b, c, d, in[13], S31,  681279174U); /* 41 */
    MD5_HSTEP(d, a, b, c, in[ 0], S32, 3936430074U); /* 42 */
    MD5_HSTEP(c, d, a, b, in[ 3], S33, 3572445317U); /* 43 */
    MD5_HSTEP(b, c, d, a, in[ 6], S34,   76029189U); /* 44 */
    MD5_HSTEP(a, b, c, d, in[ 9], S31, 3654602809U); /* 45 */
    MD5_HSTEP(d, a, b, c, in[12], S32, 3873151461U); /* 46 */
    MD5_HSTEP(c, d, a, b, in[15], S33,  530742520U); /* 47 */
    MD5_HSTEP(b, c, d, a, in[ 2], S34, 3299628645U); /* 48 */

    /* Round 4 */
#define S41 6
#define S42 10
#define S43 15
#define S44 21
    MD5_ISTEP(a, b, c, d, in[ 0], S41, 4096336452U); /* 49 */
    MD5_ISTEP(d, a, b, c, in[ 7], S42, 1126891415U); /* 50 */
    MD5_ISTEP(c, d, a, b, in[14], S43, 2878612391U); /* 51 */
    MD5_ISTEP(b, c, d, a, in[ 5], S44, 4237533241U); /* 52 */
    MD5_ISTEP(a, b, c, d, in[12], S41, 1700485571U); /* 53 */
    MD5_ISTEP(d, a, b, c, in[ 3], S42, 2399980690U); /* 54 */
    MD5_ISTEP(c, d, a, b, in[10], S43, 4293915773U); /* 55 */
    MD5_ISTEP(b, c, d, a, in[ 1], S44, 2240044497U); /* 56 */
    MD5_ISTEP(a, b, c, d, in[ 8], S41, 1873313359U); /* 57 */
    MD5_ISTEP(d, a, b, c, in[15], S42, 4264355552U); /* 58 */
    MD5_ISTEP(c, d, a, b, in[ 6], S43, 2734768916U); /* 59 */
    MD5_ISTEP(b, c, d, a, in[13], S44, 1309151649U); /* 60 */
    MD5_ISTEP(a, b, c, d, in[ 4], S41, 4149444226U); /* 61 */
    MD5_ISTEP(d, a, b, c, in[11], S42, 3174756917U); /* 62 */
    MD5_ISTEP(c, d, a, b, in[ 2], S43,  718787259U); /* 63 */
    MD5_ISTEP(b, c, d, a, in[ 9], S44, 3951481745U); /* 64 */

    buf[0] += a;
    buf[1] += b;
    buf[2] += c;
    buf[3] += d;
}

void md5_init(md5_ctx_t *mdctx)
{
    mdctx->i[0] = mdctx->i[1] = 0;
    mdctx->buf[0] = (md5_u32) 0x67452301;
    mdctx->buf[1] = (md5_u32) 0xefcdab89;
    mdctx->buf[2] = (md5_u32) 0x98badcfe;
    mdctx->buf[3] = (md5_u32) 0x10325476;
}

void md5_update(md5_ctx_t *mdctx, unsigned char const *data, size_t size)
{
    size_t i, j, mdi;
    md5_u32 in[16];

    /* compute number of bytes mod 64 */
    mdi = (size_t) ((mdctx->i[0] >> 3) & 0x3F);

    /* update number of bits */
    if ((mdctx->i[0] + ((md5_u32) size << 3)) < mdctx->i[0])
        mdctx->i[1]++;
    mdctx->i[0] += ((md5_u32) size << 3);
    mdctx->i[1] += ((md5_u32) size >> 29);

    while (size--) {
        /* add new character to buffer, increment mdi */
        mdctx->in[mdi++] = *data++;

        /* transform if necessary */
        if (mdi == 0x40) {
            for (i = j = 0; i < 16; i++, j += 4)
                in[i] = (((md5_u32) mdctx->in[j + 3]) << 24) |
                    (((md5_u32) mdctx->in[j + 2]) << 16) |
                    (((md5_u32) mdctx->in[j + 1]) << 8) |
                    ((md5_u32) mdctx->in[j]);
            md5_transform(mdctx->buf, in);
            mdi = 0;
        }
    }
}

void md5_final(md5_ctx_t *mdctx)
{
    unsigned int i, j, mdi;
    md5_u32 in[16];
    static unsigned char const buf_pad[64] = {
        0x80, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    /* save number of bits */
    in[14] = mdctx->i[0];
    in[15] = mdctx->i[1];

    /* compute number of bytes mod 64 */
    mdi = (unsigned int) ((mdctx->i[0] >> 3) & 0x3F);

    /* pad out to 56 mod 64 */
    i = (mdi < 56) ? (56 - mdi): (120 - mdi);
    md5_update(mdctx, buf_pad, i);

    /* append length in bits and transform */
    for (i = 0, j = 0; i < 14; i++, j += 4)
        in[i] = (((md5_u32) mdctx->in[j + 3]) << 24) |
            (((md5_u32) mdctx->in[j + 2]) << 16) |
            (((md5_u32) mdctx->in[j + 1]) << 8) |
            ((md5_u32) mdctx->in[j]);
    md5_transform(mdctx->buf, in);

    for (i = j = 0; i < 4; i++, j += 4) {
        mdctx->digest[j] = (unsigned char) (mdctx->buf[i] & 0xFF);
        mdctx->digest[j + 1] =
            (unsigned char) ((mdctx->buf[i] >> 8) & 0xFF);
        mdctx->digest[j + 2] =
            (unsigned char) ((mdctx->buf[i] >> 16) & 0xFF);
        mdctx->digest[j + 3] =
            (unsigned char) ((mdctx->buf[i] >> 24) & 0xFF);
    }
}

void md5_hex(unsigned char *src, char *dst)
{
    unsigned i, c;
    static char const hex[] = "0123456789abcdef";

    for (i = 0; i < 16; i++) {
        c = src[i];
        dst[i * 2 + 0] = hex[c >> 4];
        dst[i * 2 + 1] = hex[c & 0x0F];
    }
    dst[32] = '\0';
}

void do_md5_file(FILE *file, long start, long bytes, char *hash)
{
    int n;
    md5_ctx_t ctx;
    unsigned char buff[1024];

    md5_init(&ctx);
    fseek(file, start, SEEK_SET);
    while (bytes > 0) {
        n = (int)fread(buff, 1, Min(bytes, sizeof(buff)), file);
        if (n <= 0)
            break;
        md5_update(&ctx, buff, n);
        bytes -= n;
    }
    md5_final(&ctx);
    md5_hex(ctx.digest, hash);
}

void do_md5_string(char const *pass, size_t passlen, char *hash)
{
    md5_ctx_t ctx;

    md5_init(&ctx);
    md5_update(&ctx, (unsigned char const *) pass, passlen);
    md5_final(&ctx);
    md5_hex(ctx.digest, hash);
}

