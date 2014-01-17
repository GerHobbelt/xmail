#!/bin/sh
#
# skeleton	example file to build /etc/init.d/ scripts.
#		This file should be used to construct scripts for /etc/init.d.
#
#		Written by Miquel van Smoorenburg <miquels@cistron.nl>.
#		Modified by Davide Libenzi <davidel@xmailserver.org>
#
# Version:	@(#)skeleton  1.8  03-Mar-1998  miquels@cistron.nl
#

XMAIL_ROOT=/var/MailRoot
XMAIL_CMD_LINE=""
PATH=$XMAIL_ROOT/bin:/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=$XMAIL_ROOT/bin/XMail
NAME=XMail
DESC="XMail Server"

test -f $DAEMON || exit 0

set -e
ulimit -c 20000
ulimit -s 128

start_xmail()
{
    MAIL_ROOT=$XMAIL_ROOT
    export MAIL_ROOT
    MAIL_CMD_LINE=$XMAIL_CMD_LINE
    export MAIL_CMD_LINE
    rm -f /var/run/$NAME.pid
    $DAEMON
    while [ ! -f /var/run/$NAME.pid ]
    do
        sleep 1
    done
}

stop_xmail()
{
    if [ -f /var/run/$NAME.pid ]
    then
        echo `date` > $XMAIL_ROOT/.shutdown
        kill -INT `cat /var/run/$NAME.pid`
        while [ -f $XMAIL_ROOT/.shutdown ]
        do
            sleep 1
        done
    fi
}


case "$1" in
  start)
      echo -n "Starting $DESC: "
      start_xmail
      echo "$NAME.[" `cat /var/run/$NAME.pid` "]"
	;;
  stop)
      echo -n "Stopping $DESC: "
      stop_xmail
      echo "$NAME."
	;;
  #reload)
	#
	#	If the daemon can reload its config files on the fly
	#	for example by sending it SIGHUP, do it here.
	#
	#	If the daemon responds to changes in its config file
	#	directly anyway, make this a do-nothing entry.
	#
	# echo "Reloading $DESC configuration files."
	# start-stop-daemon --stop --signal 1 --quiet --pidfile \
	#	/var/run/$NAME.pid --exec $DAEMON
  #;;
  restart|force-reload)
	#
	#	If the "reload" option is implemented, move the "force-reload"
	#	option to the "reload" entry above. If not, "force-reload" is
	#	just the same as "restart".
	#
	echo -n "Restarting $DESC: "
	stop_xmail
	sleep 1
	start_xmail
        echo "$NAME.[" `cat /var/run/$NAME.pid` "]"	
	;;
  *)
	N=/etc/init.d/$NAME
	# echo "Usage: $N {start|stop|restart|reload|force-reload}" >&2
	echo "Usage: $N {start|stop|restart|force-reload}" >&2
	exit 1
	;;
esac

exit 0
