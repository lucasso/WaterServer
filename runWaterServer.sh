#!/bin/sh

app="/usr/bin/waterServer"
appconf=/etc/waterServer/config.ini
logconf=/etc/waterServer/log.ini
pidfile=/var/run/waterServer.pid

start() {
    echo -n $"Starting $app: "
    $app $appconf $logconf
    RETVAL=$?
    echo
    return $RETVAL
}

stop() {
    echo -n $"Stopping $app: "
    if [ -e $pidfile ]; then 
		cat $pidfile | xargs kill -INT
		RETVAL=$?
		echo
	else
		echo "not running"
		RETVAL=1
	fi
    return $RETVAL
}


restart() {
    stop
    start
}

case "$1" in
    start)
        $1
        ;;
    stop)
        $1
        ;;
    restart)
        $1
        ;;
    *)
        echo $"Usage: $0 {start|stop|restart}"
        exit 2
esac

exit $?

#$(dirname $0)/waterServer $(dirname $0)/config.ini $(dirname $0)/log.ini
