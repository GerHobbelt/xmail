#!/usr/bin/perl -w
#
# split-cabundle.pl by Davide Libenzi (Splits a CA Bundle created with mkcabundle.pl)
# Copyright (C) 2008  Davide Libenzi
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Davide Libenzi <davidel@xmailserver.org>
#

use strict;

my $openf = 0;
my $startc = 0;

while (<STDIN>) {
    my $ln = $_;

    chomp($ln);
    if ($ln =~ /^SHA1 Fingerprint=(.*)$/) {
	my $fname;

	($fname = $1) =~ s/://g;
	print "Creating ${fname}.pem\n";
	if (!open(CFILE, ">${fname}.pem")) {
	    print STDERR "Unable to crete file $fname\n";
	    exit(2);
	}
	$openf = 1;
    } elsif ($ln =~ /^-----BEGIN CERTIFICATE-----/) {
	if (!$openf) {
	    print STDERR "Wrong bundle file format: missing SHA1 Fingerprint\n";
	    exit(3);
	}
	$startc = 1;
	print CFILE "$ln\n";
    } elsif ($ln =~ /^-----END CERTIFICATE-----/) {
	if (!$startc) {
	    print STDERR "Wrong bundle file format: ENDCERT without BEGCERT\n";
	    exit(3);
	}
	print CFILE "$ln\n";
	close(CFILE);
	$startc = 0;
	$openf = 0;
    } elsif ($startc) {
	print CFILE "$ln\n";
    }
}

