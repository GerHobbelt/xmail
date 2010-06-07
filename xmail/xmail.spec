Summary: Advanced, fast and reliable ESMTP/POP3 mail server
Name: xmail
Version: 1.23
Release: 1
Copyright: GPL
Group: System Environment/Daemons
Source: http://www.xmailserver.org/xmail-%{PACKAGE_VERSION}.tar.gz
URL: http://www.xmailserver.org
Packager: Davide Libenzi <davidel@xmailserver.org>
BuildRoot: /var/tmp/xmail-%{PACKAGE_VERSION}
Requires: glibc


%description
XMail is an Internet and intranet mail server featuring an SMTP server, POP3 server,
finger server, multiple domains, no need for users to have a real system account,
SMTP relay checking, RBL/RSS/ORBS/DUL and custom ( IP and address based ) spam protection,
SMTP authentication ( PLAIN LOGIN CRAM-MD5 POP3-before-SMTP and custom ),
POP3 mail fecthing of external POP3 accounts, account aliases, domain
aliases, custom mail processing, direct mail files delivery, custom mail filters,
mailing lists, remote administration, custom mail exchangers, logging, and multi-platform code.
XMail sources compile under GNU/Linux, FreeBSD, Solaris and NT/2000/XP.


%prep

%setup
make -f Makefile.lnx clean

%build
make -f Makefile.lnx


%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/var/MailRoot/bin
mkdir -p $RPM_BUILD_ROOT/var/MailRoot/docs
mkdir -p $RPM_BUILD_ROOT/usr/sbin
cp -R MailRoot $RPM_BUILD_ROOT/var/MailRoot.sample

install -m 755 bin/XMail $RPM_BUILD_ROOT/var/MailRoot/bin/XMail
install -m 755 bin/XMCrypt $RPM_BUILD_ROOT/var/MailRoot/bin/XMCrypt
install -m 755 bin/CtrlClnt $RPM_BUILD_ROOT/var/MailRoot/bin/CtrlClnt
install -m 755 bin/MkUsers $RPM_BUILD_ROOT/var/MailRoot/bin/MkUsers
install -m 4755 bin/sendmail $RPM_BUILD_ROOT/usr/sbin/sendmail.xmail
install -m 755 sendmail.sh $RPM_BUILD_ROOT/usr/sbin/sendmail.xmail.sh

install -m 644 docs/Readme.txt $RPM_BUILD_ROOT/var/MailRoot/docs/Readme.txt
install -m 644 docs/Readme.html $RPM_BUILD_ROOT/var/MailRoot/docs/Readme.html
install -m 644 docs/ChangeLog.txt $RPM_BUILD_ROOT/var/MailRoot/docs/ChangeLog.txt
install -m 644 docs/ChangeLog.html $RPM_BUILD_ROOT/var/MailRoot/docs/ChangeLog.html

mkdir -p $RPM_BUILD_ROOT/etc/rc.d/init.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc0.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc1.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc2.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc3.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc4.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc5.d
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/rc6.d

install -m 755 xmail $RPM_BUILD_ROOT/etc/rc.d/init.d/xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc0.d/K10xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc1.d/K10xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc2.d/K10xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc6.d/K10xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc3.d/S90xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc4.d/S90xmail
ln -s ../init.d/xmail $RPM_BUILD_ROOT/etc/rc.d/rc5.d/S90xmail


%clean
rm -rf $RPM_BUILD_ROOT


%pre
if [ -f /etc/rc.d/init.d/xmail ]
then
    /etc/rc.d/init.d/xmail stop
fi


%post
if [ ! -f /var/MailRoot/server.tab ]
then
    cp -R /var/MailRoot.sample/* /var/MailRoot
fi

if [ ! -f /usr/sbin/sendmail.orig ]
then
    mv /usr/sbin/sendmail /usr/sbin/sendmail.orig
    ln -s /usr/sbin/sendmail.xmail.sh /usr/sbin/sendmail
fi

/etc/rc.d/init.d/xmail start


%preun
if [ -f /etc/rc.d/init.d/xmail ]
then
    /etc/rc.d/init.d/xmail stop
fi

%postun
if [ -f /usr/sbin/sendmail.orig ]
then
    mv /usr/sbin/sendmail.orig /usr/sbin/sendmail
fi

%files
/var/MailRoot/bin/XMail
/var/MailRoot/bin/XMCrypt
/var/MailRoot/bin/CtrlClnt
/var/MailRoot/bin/MkUsers
/usr/sbin/sendmail.xmail
/usr/sbin/sendmail.xmail.sh

/var/MailRoot/docs/Readme.txt
/var/MailRoot/docs/Readme.html
/var/MailRoot/docs/ChangeLog.txt
/var/MailRoot/docs/ChangeLog.html

/var/MailRoot.sample

/etc/rc.d/init.d/xmail
/etc/rc.d/rc0.d/K10xmail
/etc/rc.d/rc1.d/K10xmail
/etc/rc.d/rc2.d/K10xmail
/etc/rc.d/rc6.d/K10xmail
/etc/rc.d/rc3.d/S90xmail
/etc/rc.d/rc4.d/S90xmail
/etc/rc.d/rc5.d/S90xmail


%changelog

* Wed Oct 12 2005 Davide Libenzi <davidel@xmailserver.org>
    The POP3 before SMTP authentication is now correctly interpreted as real SMTP
    authentication, by the mean of @@USERAUTH.
    ATTENTION: Fixed a possible cause of buffer overflow in the XMail's sendmail binary.
    Changed the DNS MX resolution to allow better handling of partially broken DNS
    servers configuations.

* Sun Jan 9 2005 Davide Libenzi <davidel@xmailserver.org>
    Added a fix for 64 bits porting compatibility.
    Added the ability to exclude filters from execution in case of authenticated user.
    By pre-pending the filter command token with a token containing "!aex", the filters
    won't be run if the user authenticated himself.
    Added @@USERAUTH macro even to standard in/out filters (before it was only defined
    for SMTP ones).
    Added a new "NoSenderBounce" variable inside the SERVER.TAB file, to enable
    XMail generated bounce messages to have the empty SMTP sender ('MAIL FROM:<>').
    Added a new "SMTP-MaxErrors" variable inside the SERVER.TAB file to set the maximum
    errors allowed in a single SMTP session (default zero, unlimited).
    Added a "LastLoginTimeDate" variable to the "userstat" CTRL command.
    Added external aliases support in the CTRL protocol.
    The MESSAGE.ID file is now automatically created, if missing.
    Changed the logic used to treat domain and user MAILPROC.TAB files. Before, a user's
    MAILPROC.TAB was overriding the domain one, while now the rules are merged together,
    with domain's ones first, followed by user's ones.
    The maximum mailbox size of zero is now interpreted as unlimited.
    Fixed XMail's sendmail to detect non-RFC822 data and handle it correctly.
    The IP:PORT addresses emission in spool files (and Received: lines) has been changed
    to the form [IP]:PORT.
    Added filter logging, that is enabled with the new -Qg command line option.
    Fixed an error message in the SMTP server, that was triggered by the remote client
    not using the proper syntax for the "MAIL FROM:" and "RCPT TO:" commands.
    Fixed explicit routing through SMTPGW.TAB file.
    Fixed a possible problem with file locking that might be triggered from CTRL commands
    cfgfileget/cfgfileset.
    Added a check to avoid the CTRL server to give an error when a domain created with
    older versions of XMail does not have the domain directory inside cmdaliases.
    The SMTP server FQDN variable should be set to the value of "SmtpServerDomain", when
    this is used inside the SERVER.TAB file.

* Sun May 30 2004 Davide Libenzi <davidel@xmailserver.org>
    Fixed a possible memory leak and a possible source of crashes.

* Sat May 29 2004 Davide Libenzi <davidel@xmailserver.org>
    Implemented the "filter" command for custom mail processing (MAILPROC.TAB, cmdaliases
    and custom domains).
    If "RemoveSpoolErrors" is set inside the SERVER.TAB file, messages are never frozen.
    Before there was a special case (delivery failure and delivery notification failure)
    that could have lead to frozen messages.
    Made "aliasdomainadd" to check for the existence of the alias domain (and reject
    the command if existing).
    Introduced a new environment variable recognized by XMail (XMAIL_PID_DIR), to let
    the user to specify a custom PID file directory (this is for Unix ports only).
    Implemented ability to stop custom mail processing upon certain exit codes from
    external commands execution.
    The SPAMMERS.TAB check is now bypassable (see doc for details).
    ATTENTION: Changed the "aliasdomainlist" syntax and output format (see doc for details).
    Made (on Unix setups) the PID file name to be dependent on the daemon file name.
    Implemeted a domain-wise MAILPROC.TAB and extended its "redirect" and "lredirect"
    commands to support account specific (USER@DOMAIN) and domain targets (DOMAIN).
    Implemented SMTP filters to allow users to reject the SMTP session before and
    after the remote client data has been received.

* Sat Mar 27 2004 Davide Libenzi <davidel@xmailserver.org>
    Restructured the external program execution environment on Unix ports. Simplified,
    as a consequence of this, the system dependent portion of XMail (SysDep*).
    Fixed a bug in the address range parsing (x.y.w.z/s).
    Fixed the alias lookup to perform a better "best match" wildcard selection.
    Fixed a bug in the DNS resolved that made XMail to not correctly handle domain CNAMEs.

* Sun Sep 14 2003 Davide Libenzi <davidel@xmailserver.org>
    Added Bcc: removal from message headers in XMail's sendmail.
    Added PSYNC logging (-Yl).
    Added domain completion to XMail's sendmail when the specified sender address (-f or -F)
    does not contain one. The environment variable (or registry in Windows) DEFAULT_DOMAIN is
    looked up to try to complete the address.
    Fixed a bug in the return code of SysAccept() in all Unix versions.
    Fixed a bug that was triggered by external command and filter exiting soon. XMail was not
    able to correctly sync with the child process by losing it. This apply only to Unix
    versions of XMail.
    A notification message is now sent to the sender if the message is handled with
    "smtp" or "smtprelay" commands and a permanent error happen when sending to the
    remote SMTP server.

* Tue Jul 08 2003 Davide Libenzi <davidel@xmailserver.org>
    Added a new configuration file "smtp.ipprop.tab" to be able to specify peer IP based
    configuration option, like for example IP white listing against IP checks.
    ATTENTION: The filter return code has been changed and new return codes are
    expected to be returned by filters. Please che the documentation and update your
    filters before starting to use the new version.
    Added the ability to specify a custom error message for filters.
    Fixed a bug in the string quoting function that showed up when the string was empty ("").
    Changed the order used by XMail to check the mailer domain. Now MX check is performed
    first, then A record check. This caused a slow down for domains having MX records but
    not A records.
    Added two new Received: types to give the ability to hide client information if
    the SMTP client does authenticate with the server.
    Added the rejection map name inside the SMTP log file in case of SNDRIP=EIPMAP error.
    Modified XMail's sendmail to add the RFC822 Date: header if missing.
    XMail now uses the name of the executable ( without .exe ) to both register the service
    name and fetch registry variables.
    The POP3 server now picks up messages even from the Maildir's "cur" subdirectory.

* Sat May 03 2003 Davide Libenzi <davidel@xmailserver.org>
    Implemented a new filters feature that enable the user to stop the
    selected filters list processing upon receival of certain exit codes.
    Fixed the wrong log file name generation when the daylight time is active.
    Fixed a bug inside the DNS MX resolver.
    Fixed a bug ( Windows OS bug ) that made XMail unable to create
    domains starting with reserved device names ( COM#, LPT, PRN, CON, ... ).
    So, for example, a domain named "com4.domain.org" couldn't be created because
    of this naming conflict. Fixed a bug that made XMail to not apply filters for
    local mailing list.
    Fixed a bug that made XMail to crash under certain conditions.

* Wed Apr 02 2003 Davide Libenzi <davidel@xmailserver.org>
    Added a "Server:" field to the notification message. It'll report the remote SMTP server
    host name and IP that issued the error. It will not be present if the error does not
    originate from a remote SMTP server.
    Added a new command line parameter -MD to set the number of subdirectories allocated
    for the DNS cache files storage.
    Messages with non RFC822 conforming headers are now handled by the PSYNC code.
    ATTENTION: The filter architecture has been completely changed. To correctly
    update to this version you have to create two empty files "filters.in.tab" and "filters.out.tab"
    inside the $MAIL_ROOT directory. Please refer to the documentation for more information
    about the new filter architecture. If you are not currently using filters, the simple
    creation of the two files listed above will be sufficent.
    ATTENTION: The internal spool file format is changed with the new line added
    ( the 1st one ) that contain various message information. Filters that rely on the
    internal spool file format must be changed to match the new structure.
    Fixed a bug that made XMail to not correctly report zero sized files inside the mailbox.
    Added file size to CTRL's "filelist" command.
    Fixed a connect-error reporting bug on Windows platform.

* Sat Jan 25 2003 Davide Libenzi <davidel@xmailserver.org>
    Better check for user/domain names.
    Changed search pattern for filters. Now a domain name is scanned for all sub-domains.
    Fixed a boundary check inside the Base64 decoder.
    Added the client FQDN inside the SMTP log file in case the RDNS check is enabled.
    Added a new SERVER.TAB variable "SmtpMsgIPBanSpammers" to set the message that is sent
    to the SMTP client when the client IP is listed inside the file SPAMMER.TAB.
    Added a new SERVER.TAB variable "SmtpMsgIPBanMaps" to set the message that is sent
    to the SMTP client when the client IP is listed inside one of the "CustMapsList".
    Added a new SERVER.TAB variable "SmtpMsgIPBanSpamAddress" to set the message that is sent
    to the SMTP client when the client IP is listed inside the file SPAM-ADDRESS.TAB.
    Fixed a bug inside the custom account handling that made XMail to pass the old password
    instead of the new one.
    Added OpenBSD support.

* Sat Nov 9 2002 Davide Libenzi <davidel@xmailserver.org>
    Added a new command line parameter -QT to enable a configurable timeout for filter commands.
    Fixed a bug that made XMail to ignore cmdalias accounts when a wildcard alias was matching
    the account itself.
    Added the 'smtprelay' command to the MAILPROC.TAB processing.
    Removed the 'wait' command from all custom processing.
    Added a new macro @@RRCPT to filters commands to extract the real local recipient.
    Changed the way the EXTALIASES.TAB mapping modify the return path. It now change the "Reply-To:"
    instead of the "From:" to avoid problems with signature verification software.
    Implemented logging on SMTP transactions rejected because of mapped IP or failing RDNS check.
    Added a new SERVER.TAB variable "SmtpServerDomain" to force the SMTP domain used by XMail
    in its banner string ( for CRAM-MD5 ESMTP authentication ).
    Improved DNS resolution for not existing domains.

* Sat Jul 27 2002 Davide Libenzi <davidel@xmailserver.org>
    Added a variable "CustomSMTPMessage" inside the server's configuration file SERVER.TAB to enable
    the postmaster to set a custom message that will be appended to the standard XMail error
    response.
    Added log entries in case of relay lists mapped IPs.
    Fixed a build error on FreeBSD.
    Added a new SERVER.TAB variable "DisableEmitAuthUser" to block the emission the the header
    "X-Auth-User:" for authenticated user.
    Added a new USER.TAB variable "DisableEmitAuthUser" to block the emission the the header
    "X-Auth-User:" for authenticated users ( this variable overrides the SERVER.TAB one ).
    Added command line driven mailbox delivery mode ( -MM = Maildir , -Mm = mailbox ).
    Added  sysv_inst.sh  shell script to help creating SysV boot scripts for XMail.

* Sat Jun 15 2002 Davide Libenzi <davidel@xmailserver.org>
    Fixed a bug in HOSTNAME:PORT handing code inside the PSYNC server.
    Fixed a bug introduced in 1.8 in the Windows version that made XMail to have bad behaviour when
    used with external programs.
    Fixed a bug that resulted in XMail generating frozen messages even if the SERVER.TAB variable was
    set to not create them.
    Fixed a bug that made it possible to send a "MAIL FROM:<@localdomain:remoteaddress>" and to have
    the message relayed if the IP of the current machine was inside the smtprelay.tab of the machine
    handling the MX of @localdomain.
    Implemented internal mail loop checking ( internal redirects ).
    Added a new MLUSERS.TAB permissions flags 'A', that is similar to 'W' by instead of checking
    the "MAIL FROM:<...>" address check the SMTP authentication address ( this will prevent malicious
    users to forge the address to gain write permissions on the list ).

* Sun May 19 2002 Davide Libenzi <davidel@xmailserver.org>
    Changed XMail's behaviour upon receival on long ( RFC compared ) data lines on SMTP and POP3 fetch
    inbound doors. Before the operation was aborted while now data is accepted without truncation,
    that might make XMail to behave non conforming the RFC. Added @@RRCPT macro to the "external"
    MAILPROC.TAB command to emit the real recipient of the message ( @@RCPT could be an alias ).	
    Added HOSTNAME:PORT capability to POP3LINKS.TAB entries. Added Linux/PowerPC port.
    Added "filelist" CTRL protocol command. Added SMTP HELP command.
    Changed bounce message format to add the last SMTP error and to make it works with Ecartis
    mail bounce processing. Changed the XMail's sendmail implementation to accept "-f FROM" and "-F FROM"
    non standard sendmail paramenter specification.
    Fixed a bug inside the PSYNC server code that made XMail to fail to resolve POP3 server addresses.
    Various code cleanups.
	
* Mon Apr 01 2002 Davide Libenzi <davidel@xmailserver.org>
    Fixed a bug inside the POP3 server that caused bad responses to UIDL and LIST commands
    in case of certain command patterns.
    Added support for HOSTNAME:PORT ( or IP:PORT ) for the DefaultSMTPGateways SERVER.TAB variable.
    Added domain aliases cleanup upon main domain removal.
    Added "MaxMessageSize" inside USER.TAB files to override the global ( SERVER.TAB ) one.

* Sun Mar 03 2002 Davide Libenzi <davidel@xmailserver.org>
    Added a new USER.TAB variable "UseReplyTo" ( default 1 ) to make it possible to disable the emission
    of the Reply-To: header for mailing lists.
    Fixed a bug that caused XMail to uncorrectly deliver POP3 fetched messages when used togheter with
    domain masquerading.
    Changed index file structure to use an hash table for faster lookups and index rebuilding.
    New files inside the  tabindex  directory now have the extension  .hdx  and old  .idx  files can be removed.
    Added X-Deliver-To: header to messages redirected with MAILPROC.TAB file.
    Added configurable Received: tag option in SERVER.TAB by using the variable "ReceivedHdrType".
    Added a configurable list of header tags to be used to extract addresses for POP3 fetched messages
    by using the SERVER.TAB variable "FetchHdrTags".
    History ( change log ) entries have been moved from the main documentation file and a new file ( ChangeLog.txt )
    has been created to store change-log entries.
    Removed RBL-MAPSCheck ( currently blackholes.mail-abuse.org. ), RSS-MAPSCheck ( currently relays.mail-abuse.org. )
    and DUL-MAPSCheck ( currently dialups.mail-abuse.org. ) specific variables and now everything must
    be handled with CustMapsList ( please look at the documentation ).
    Added  NotifyMsgLinesExtra  SERVER.TAB variable to specify the number of lines of the bounced message
    to include inside the notify reply ( default zero, that means only header ).
    The message log file is now listed inside the notification message sent to  ErrorsAdmin  ( or  PostMaster  ).
    Added  NotifySendLogToSender  SERVER.TAB variable to enable/disable the send of the message log file
    inside the notify message to the sender ( default is off ).
    Added  TempErrorsAdmin  SERVER.TAB variable to specify an account that will receive temporary delivery
    failures notifications ( default is empty ).
    Added a new SERVER.TAB variable  NotifyTryPattern  to specify at which delivery attempt failure
    the system has to send the notification message.
    Fixed a bug that caused alias domains to have higher priority lookup compared to standard domains.

* Tue Feb 05 2002 Davide Libenzi <davidel@xmailserver.org>
    Fixed a bug in wildcard aliases domain lookup.
    Fixed a bug in CTRL command "aliasdel" that failed to remove aliases with wildcard domains.
    Fixed a bug that caused XMail to timeout on very slow network connections.

* Fri Jan 18 2002 Davide Libenzi <davidel@xmailserver.org>
    Fixed a bug that made XMail to fail to parse custom maps lists in SERVER.TAB.
    Fixed a bug that prevented XMail to add wildcard-domain aliases.
    Added a filter feature to the CTRL commands "domainlist" and "aliasdomainlist".
    Added an extra message header field "X-AuthUser:" to log the username used by the account to send the message.
    Added Reply-To: RFC822 header for mailing lists sends.
    Fixed a Win32 subsystem API to let XMail to correctly handle network shared MAIL_ROOTs.

* Wed Dec 19 2001 Davide Libenzi <davidel@xmailserver.org>
    ORBS maps test removed due old ORBS dead, the SERVER.TAB variable "CustMapsList"
    can be used to setup new ORBS ( and other ) maps. Fixed a bug in XMail's  sendmail
    that was introduced in version 1.2 and made it to incorrectly interpret command line
    parameters. Fixed a bug that made XMail to not correctly recognize user type
    characters when lowercase. Fixed a bug that caused XMail to not start is the MAIL_ROOT
    environment variable had a final slash on Windows. Added a new filter return code ( 97 )
    to reject messages without notification and without frozen processing. Added two new
    command line options  -MR  and  -MS  to set the I/O socket buffers sizes in bytes
    ( do not use them if You don't know what You're doing ). Changed system library to have
    a better performace, expecially on the Windows platform. Users that are using XMail
    mainly inside their local LAN are strongly encouraged to switch to this version.
    Fixed a bug that enabled insertion of aliases that overlapped real accounts.

* Mon Nov 12 2001 Davide Libenzi <davidel@xmailserver.org>
    A problem with log file names generation has been fixed.
    Added a new CTRL command "userstat".
    Implemented Linux/SPARC port and relative makefile ( Makefile.slx ).
    Extended the XMail version of  sendmail  to support a filename as input ( both XMail format that
    raw email format ) and to accept a filename as recipient list.
    Added a new kind of aliases named "cmdaliases" that implements a sort of custom domains commands
    on a per-user basis ( look at the  CmdAliases  section ).
    ************************************************************************************************
    * You've to create the directory "cmdaliases" inside $MAIL_ROOT to have 1.2 working correctly
    ************************************************************************************************	
    Fixed a bug that had XMail to not check for the user variable SmtpPerms with CRAM-MD5 authetication.
    Fixed a bug in the XMail's  sendmail  implementation that made it unable to detect the "."
    end of message condition.
    Fixed a bug in the XMail's  sendmail  implementation that made it to skip cascaded command line
    parameters ( -Ooet ).
    Implemented a new XMail's  sendmail  switch -i to relax the <CR><LF>.<CR><LF> ond of message indicator.

* Tue Oct 10 2001 Davide Libenzi <davidel@xmailserver.org>
    Fixed a bug in the XMail version of  sendmail  that made messages to be double sent.
    The macro @@TMPFILE has been removed from filters coz it's useless.
    The command line parameter  -Lt NSEC  has been added to set the sleep timeout of LMAIL threads.
    Added domain aliasing ( see ALIASDOMAIN.TAB section ).
    ************************************************************************************************
    * You've to create the file ALIASDOMAIN.TAB inside $MAIL_ROOT ( even if empty )
    ************************************************************************************************	
    Added CTRL commands "aliasdomainadd", "aliasdomaindel" and "aliasdomainlist" to handle domain aliases
    through the CTRL protocol.

* Tue Sep 4 2001 Davide Libenzi <davidel@xmailserver.org>
    Added wildcard matching in the domain part of ALIASES.TAB ( see ALIASES.TAB section ).
    Changed the PSYNC scheduling behaviour to allow sync interval equal to zero ( disabled ) and
    let the file .psync-trigger to schedule syncs.
    Solaris on Intel support added.
    A new filter return code ( 98 ) has been added to give the ability to reject message without notify the sender.
    It's finally time, after about 70 releases, to go 1.0 !!

* Mon Jul 2 2001 Davide Libenzi <davidel@xmailserver.org>
    A stack shifting call method has been implemented to make virtually impossible for attackers
    to guess the stack frame pointer.
    With this new feature, even if buffer overflows are present, the worst thing that could happen
    is a server crash and not the attacker that execute root code on the server machine.
    Implemented the SIZE ESMTP extension and introduced a new SERVER.TAB variable "MaxMessageSize" that set
    the maximum message size that the server will accept ( in Kb ).
    If this variable is not set or if it's zero, any message will be accepted.
    A new SMTP authentication permission ( 'Z' ) has been added to allow authenticated users to bypass the check.
    The SMTP sender now check for the remote support of the SIZE ESMTP extension.
    A new SERVER.TAB variable has been added  "CustMapsList"  to enable the user to enter custom maps checking
    ( look at the section "SERVER.TAB variables" ).
    Fixed a bug in "frozdel" CTRL command.

* Sun Jun 10 2001 Davide Libenzi <davidel@xmailserver.org>

* Fri Jun 8 2001 Davide Libenzi <davidel@xmailserver.org>
    Fixed a possible buffer overflow bug inside the DNS resolver.

* Tue May 29 2001 Davide Libenzi <davidel@xmailserver.org>
    Fixed build errors in MkUsers.cpp and SendMail.cpp ( FreeBSD version ).
    Added the ability to specify a list of matching domains when using PSYNC with
    masquerading domains ( see POP3LINKS.TAB section ).
    The auxiliary program  sendmail  now read the MAIL_ROOT environment from
    registry ( Win32 version ) and if it fails it reads from the environment.
    Fixed a bug that made XMail to crash if the first line of ALIASES.TAB was empty.
    RPM packaging added.
    Added a new feature to the custom domain commands "redirect" and "lredirect"
    that will accept email addresses as redirection target.
    Fixed a bug in MkUsers.
    Added system resource checking before accepting SMTP connections
    (see "SmtpMinDiskSpace" and "SmtpMinVirtMemSpace" SERVER.TAB variables ).
    Added system resource checking before accepting POP3 connections
    ( see "Pop3MinVirtMemSpace" SERVER.TAB variable ).
    A new command line param -t has been implemented in sendmail.
    A new USER.TAB variable "SmtpPerms"  has been added	to enable account based SMTP permissions.
    If "SmtpPerms" is not found the SERVER.TAB variable "DefaultSmtpPerms" is checked.
    A new USER.TAB variable "ReceiveEnable" has been added to enable/disable the account from receiving emails.
    A new USER.TAB variable "PopEnable" has been added to enable/disable the account from fetching emails.

