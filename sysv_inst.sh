#!/bin/sh

SYSVD=/etc/rc.d/init.d
RCD=/etc/rc.d

if [ ! -d "$SYSVD" ]; then
    echo "SysV init directory does not exist: $SYSVD"
    exit 1
fi
if [ ! -d "$RCD" ]; then
    echo "SysV RC init directory does not exist: $RCD"
    exit 2
fi

cp xmail $SYSVD

ln -s ../init.d/xmail $RCD/rc0.d/K10xmail
ln -s ../init.d/xmail $RCD/rc1.d/K10xmail
ln -s ../init.d/xmail $RCD/rc2.d/K10xmail
ln -s ../init.d/xmail $RCD/rc6.d/K10xmail
ln -s ../init.d/xmail $RCD/rc3.d/S90xmail
ln -s ../init.d/xmail $RCD/rc4.d/S90xmail
ln -s ../init.d/xmail $RCD/rc5.d/S90xmail

