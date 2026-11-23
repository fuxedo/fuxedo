include ../mk/domain.mk

export FLDTBLDIR32:=$(TUXDIR)/udataobj
export FIELDTBLS32:=tpadm

check: server  tuxconfig
	-rm -f ULOG.*
	-tmipcrm -y
	-tmboot -y
	sleep 3 # 22.1 returns before the bulletin board is ready
	echo "SRVCNM\t.TMIB\nTA_CLASS\tT_DOMAIN\nTA_OPERATION\tGET\n\n" | ud32
	echo "SRVCNM\t.TMIB\nTA_CLASS\tT_MACHINE\nTA_OPERATION\tGET\n\n" | ud32
	echo "SRVCNM\t.TMIB\nTA_CLASS\tT_SERVICE\nTA_OPERATION\tGET\n\n" | ud32
	echo "SRVCNM\t.TMIB\nTA_CLASS\tT_SERVER\nTA_OPERATION\tGET\n\n" | ud32
	echo "SRVCNM\t.TMIB\nTA_CLASS\tT_QUEUE\nTA_OPERATION\tGET\n\n" | ud32
	echo "SRVCNM\t.TMIB\nTA_CLASS\tT_SVCGRP\nTA_OPERATION\tGET\n\n" | ud32
	echo "pq" | tmadmin
	tmshutdown -y
	-tmshutdown -y -k KILL

server: server.c
	buildserver -o $@ -f $< -s SERVICE -v -f "-Wl,--no-as-needed"

clean:
	-rm -f *.o ubbconfig tuxconfig server ULOG.* stdout stderr access.*
