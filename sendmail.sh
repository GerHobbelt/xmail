#!/bin/sh


if [ -z $MAIL_ROOT ]; then
	export MAIL_ROOT=/var/MailRoot
fi


/usr/sbin/sendmail.xmail $*

