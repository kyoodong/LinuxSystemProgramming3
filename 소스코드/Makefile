compile: clear crontab crond rsync

crontab:
	gcc crontab.c -o ssu_crontab

crond:
	gcc daemon.c -o ssu_crond

rsync:
	gcc rsync.c -o ssu_rsync

clear:
	rm -f ssu_crontab ssu_crond ssu_rsync
