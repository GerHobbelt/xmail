#!/usr/bin/perl
#-----------------------------------------
# cvpod - simple script to involk pod2html
#         and pod2text
#
# a single argument gives the input file
# name; the outputs are <rootname>.html
# and <rootname>.txt.
#
# Beau E, Cox, BeauCox.com
# <beau@beaucox.com>
# <http://beaucox.com>
#
# October 13, 2002
#-----------------------------------------

use strict;
use warnings;

	my $infn = shift @ARGV;
	$infn || die "no input file specified\n";

	(-T $infn) || die "input file $infn not found or invalid\n";

	my @in;

	open (IN, $infn) or die "unable to open $infn:\n$!\n";
	while (<IN>) {
		super_chomp ();
		$_ || next;
		last if /^=begin\s+cvpod/i;
		}
	while (<IN>) {
		super_chomp ();
		$_ || next;
		last if /^=end\s+cvpod/i;
		push @in, $_;
		}

	unless (@in) {
		print "'=begin cvpod' directive found in $infn, no action taken\n";
		exit 1;
		}
	close IN;

	while (1) {
		$_ = shift @in;
		$_ || last;
		my ($cssfn) = /^:begin\s+css\s+(.+)/i;
		if ($cssfn) {
			open (CSS, ">$cssfn")
				or die "unable to open $infn:\n$!\n";
			while (1) {
				$_ = shift @in;
				$_ || last;
				last if (/^:/);
				print CSS "$_\n";
				}
			close CSS;
			}
		$_ || last;
		my ($cmd) = /^:run\s+(.+)/i;
		$cmd || next;
		print "$cmd\n";
		system $cmd;
		}

	my $cmd = ($^O =~ m/win32/i) ? "del" : "rm";
	$cmd .= " pod2h*.x~~ *.tmp";
	print "$cmd\n";
	system $cmd;
	
	exit 0;

sub super_chomp
{
	$_ = shift if (@_);
	s/\x0D//g;
	s/\x0A//g;
	$_;
}

