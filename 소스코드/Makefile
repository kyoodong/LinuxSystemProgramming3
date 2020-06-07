compile: clear core crontab crond rsync
debug:
	rm -f a.out
#gcc crontab.c core.c -g
	gcc rsync.c core.c -g
	gdb ./a.out

crontab:
	gcc crontab.c -o ssu_crontab core.o

crond:
	gcc daemon.c -o ssu_crond core.o

rsync:
	gcc rsync.c -o ssu_rsync core.o

clear:
	rm -f ssu_crontab ssu_crond ssu_rsync core.o
	rm -rf testdir/*

core:
	gcc -c core.c -o core.o
