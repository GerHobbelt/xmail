/*
 *  XMail by Davide Libenzi ( Intranet and Internet mail server )
 *  Copyright (C) 1999,..,2004  Davide Libenzi
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

#include <stdio.h>

static int MkMachIsLE(void)
{
	union MachWordBytes {
		unsigned int w;
		unsigned char b[sizeof(unsigned int)];
	} MWB;

	MWB.w = 1;
	return MWB.b[0] != 0;
}

static int MkMachIsLEBF(void)
{
	union MachBitField {
		struct {
			unsigned int b:1;
		} BF;
		unsigned int w;
	} MWB;

	MWB.w = 1;
	return MWB.BF.b != 0;
}

static int MkMachGenType(int iBits, char const *pszBase)
{
	fprintf(stdout,
		"typedef signed %s MachInt%d;\n"
		"typedef unsigned %s MachUInt%d;\n"
		"#define MACH_TYPE_%dBIT %s\n\n", pszBase, iBits, pszBase, iBits, iBits, pszBase);

	return 0;
}

int main(int argc, char *argv[])
{
	fprintf(stdout, "#ifndef _MACHDEFS_H\n" "#define _MACHDEFS_H\n\n\n");

	if (!MkMachIsLE())
		fprintf(stdout, "#define MACH_BIG_ENDIAN_WORDS\n\n");
	else
		fprintf(stdout, "#undef MACH_BIG_ENDIAN_WORDS\n\n");
	if (!MkMachIsLEBF())
		fprintf(stdout, "#define MACH_BIG_ENDIAN_BITFIELD\n\n");
	else
		fprintf(stdout, "#undef MACH_BIG_ENDIAN_BITFIELD\n\n");

	if (sizeof(char) == 1)
		MkMachGenType(8, "char");
	else if (sizeof(short) == 1)
		MkMachGenType(8, "short");
	else if (sizeof(int) == 1)
		MkMachGenType(8, "int");
	else if (sizeof(long) == 1)
		MkMachGenType(8, "long");

	if (sizeof(char) == 2)
		MkMachGenType(16, "char");
	else if (sizeof(short) == 2)
		MkMachGenType(16, "short");
	else if (sizeof(int) == 2)
		MkMachGenType(16, "int");
	else if (sizeof(long) == 2)
		MkMachGenType(16, "long");

	if (sizeof(char) == 4)
		MkMachGenType(32, "char");
	else if (sizeof(short) == 4)
		MkMachGenType(32, "short");
	else if (sizeof(int) == 4)
		MkMachGenType(32, "int");
	else if (sizeof(long) == 4)
		MkMachGenType(32, "long");

	fprintf(stdout, "\n\n" "#endif\n\n");

	return 0;
}
