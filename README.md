xmail
=====

xmail by Davide Libenzi + patches.

The original can be downloaded here: http://xmailserver.org/index.html

This github repository contains a slightly patched version plus MSVC project files for compiling the server using Microsoft Developer Studio C++ compilers.



## What is `xmail`?

XMail is an Internet and intranet mail server featuring an ESMTP server, POP3 server, finger server, TLS support for SMTP and POP3 (both server and client side), multiple domains, no need for users to have a real system account, SMTP relay checking, DNS based maps check, custom (IP based and address based) spam protection, SMTP authentication (PLAIN LOGIN CRAM-MD5 POP3-before-SMTP and custom), a POP3 account syncronizer with external POP3 accounts, account aliases, domain aliases, custom mail processing, direct mail files delivery, custom mail filters, mailing lists, remote administration, custom mail exchangers, logging, and multi-platform code. XMail sources compile under GNU/Linux, FreeBSD, OpenBSD, NetBSD, OSX, Solaris and NT/2K/XP/Win7.



## Why would I use xmail?

- one-stop shop email server; one compiled C/C++ binary so no perl or other additives increasing your installed weight
- flexible and simple configuration using simple text files (one important series of minimal patches ensures that all config files accept `#`-prefixed comment lines anywhere in each and all of the config file, allowing for inline documentation of your configurations)
- small footprint while designed to handle both small and large mail traffic numbers
- user-definable filters to do anything you fancy, from fighting spam to custom email munching/rewriting/routing/forwarding
- robust and easily readable code, without any cryptic magick, allowing for fast and thorough security reviews at source code level, resulting in a trustworthy companion for handling your email in an otherwise hostile internet environment

