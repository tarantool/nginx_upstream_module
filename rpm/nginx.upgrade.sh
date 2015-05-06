#!/bin/sh
#
# Legacy action script for "service nginx upgrade"

# Source function library.
[ -f /etc/rc.d/init.d/functions ] && . /etc/rc.d/init.d/functions

prog=nginx
nginx=/usr/sbin/nginx
conffile=/etc/nginx/nginx.conf
pidfile=`/usr/bin/systemctl show -p PIDFile nginx.service | sed 's/^PIDFile=//' | tr ' ' '\n'`
SLEEPMSEC=200000
UPGRADEWAITLOOPS=5

oldbinpidfile=${pidfile}.oldbin
${nginx} -t -c ${conffile} -q || return 6
echo -n $"Starting new master $prog: "
killproc -p ${pidfile} ${prog} -USR2
echo

for i in `/usr/bin/seq $UPGRADEWAITLOOPS`; do
    /bin/usleep $SLEEPMSEC
    if [ -f ${oldbinpidfile} -a -f ${pidfile} ]; then
        echo -n $"Graceful shutdown of old $prog: "
        killproc -p ${oldbinpidfile} ${prog} -QUIT
        echo
        exit 0
    fi
done

echo $"Upgrade failed!"
exit 1
