#!/bin/sh

cp xmail /etc/rc.d/init.d/xmail

ln -s ../init.d/xmail /etc/rc.d/rc0.d/K10xmail
ln -s ../init.d/xmail /etc/rc.d/rc1.d/K10xmail
ln -s ../init.d/xmail /etc/rc.d/rc2.d/K10xmail
ln -s ../init.d/xmail /etc/rc.d/rc6.d/K10xmail
ln -s ../init.d/xmail /etc/rc.d/rc3.d/S90xmail
ln -s ../init.d/xmail /etc/rc.d/rc4.d/S90xmail
ln -s ../init.d/xmail /etc/rc.d/rc5.d/S90xmail

