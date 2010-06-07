#! /bin/sh

#
# report diff with original directory. Ignore irrelevant files.
#

diff -u -EbwB -r --strip-trailing-cr  ../../1original/xmail/ .  -x resource.h -x '*.rc' -x '*.aps' -x VERSION.txt -x '*.ds*' -x '*.vcproj' -x '*.s*' -x '*.user' -x '*.ncb' -x '*.o' -x '*I_A*' -x '*.sh' -x '*.bak'
