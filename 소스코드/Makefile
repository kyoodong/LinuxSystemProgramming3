compile: clear core crontab crond rsync
debug: clear
	rm -f a.out
	gcc crontab.c core.c -g
#gcc rsync.c core.c -g
	gdb ./a.out

crontab:
	gcc crontab.c -o ssu_crontab core.o

crond:
	gcc daemon.c -o ssu_crond core.o -lpthread

rsync: clear
	gcc rsync.c -o ssu_rsync core.o

test_rsync:
	./ssu_rsync srcdir testdir -t
	rm testdir/srcdir/a.out
	./ssu_rsync srcdir testdir -r
	touch testdir/srcdir/newfile
	./ssu_rsync srcdir testdir -m
	./ssu_rsync /home/dongkyoo/Develop/AI /home/dongkyoo/Develop/LinuxSystemProgramming3/소스코드/testdir
	./ssu_rsync /home/dongkyoo/Develop/AI /home/dongkyoo/Develop/LinuxSystemProgramming3/소스코드/testdir/srcdir -t
	./ssu_rsync srcdir testdir -m
	cat ssu_rsync_log
	tree testdir

clear:
	rm -f ssu_crontab ssu_crond ssu_rsync core.o ssu_rsync_log
	rm -rf testdir/*

core:
	gcc -c core.c -o core.o
