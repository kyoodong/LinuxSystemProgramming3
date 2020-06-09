#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include "core.h"
#include <sys/stat.h>
#include <sys/types.h>

void init_daemon();
void print_log(const char *str);
int process_crontab();
void test(crontab *ct, int min, int hour, int day, int month, int dayofweek);

crontab head;
struct stat cronstat;
struct tm *tm;
time_t t;

int daemon_main() {
	struct stat statbuf;

	if (read_crontab_file(&head) < 0 || stat(CRONTAB_FILE, &cronstat) < 0) {
		print_log("read_crontab_file error\n");
		exit(1);
	}

	while (1) {
		if (stat(CRONTAB_FILE, &statbuf) < 0)
			continue;

		// crontab 파일이 변경되었음이 감지된 경우
		if (statbuf.st_mtime != cronstat.st_mtime) {
			// crontab 리스트를 비움
			while (!is_empty_crontab(&head))
				remove_crontab(head.next);

			// crontab 파일을 다시 읽어서 리스트 재구성
			if (read_crontab_file(&head)) {
				print_log("read_crontab_file error\n");
				exit(1);
			}

			cronstat = statbuf;
		}

		t = time(NULL);
		tm = localtime(&t);

		//printf("min = %d  hour = %d  day = %d  dayofweek = %d  month = %d\n", tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_wday, tm->tm_mon + 1);

		process_crontab();
		sleep(60);
	}
}

int main(int argc, char *argv[]) {
	init_daemon();
	daemon_main();
	exit(0);
}

int process_crontab() {
	crontab *ct;
	char buf[BUFSIZ];
	
	ct = head.next;
	//printf("NOW min = %d  hour = %d  day = %d  dayofweek = %d  month = %d\n", tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_wday, tm->tm_mon);
	while (ct != NULL) {
		if (parse_execute_term(ct->min, tm->tm_min) &&
			parse_execute_term(ct->hour, tm->tm_hour) &&
			parse_execute_term(ct->day, tm->tm_mday) &&
			parse_execute_term(ct->month, tm->tm_mon + 1) &&
			parse_execute_term(ct->dayofweek, tm->tm_wday)) {
			
			// 실행!
			sprintf(buf, "%s &", ct->op);
			system(ct->op);

			sprintf(buf, "run %s %s %s %s %s %s\n", ct->min, ct->hour, ct->day, ct->dayofweek, ct->month, ct->op);
			log_crontab(buf);
		}
		ct = ct->next;
	}

	/*
	int dayofweek = 0;
	for (int month = 1; month <= 12; month++) {
		for (int day = 1; day <= 31; day++) {
			printf("\n%d월 %d일 %d시 %d분 %d요일\n", month, day, 0, 0, dayofweek);

			ct = head.next;
			while (ct != NULL) {
				test(ct, 0, 0, day, month, dayofweek);
				ct = ct->next;
			}
			dayofweek++;
			if (dayofweek == 7)
				dayofweek = 0;
		}
	}

	for (int month = 2; month <= 12; month++) {
			for (int day = 24; day <= 31; day++) {
				for (int hour = 0; hour < 24; hour++) {
					for (int min = 0; min < 60; min++) {
						ct = head.next;
						printf("%d월 %d일 %d시 %d분\n", month, day, hour, min);
	
						while (ct != NULL) {
							test(ct, min, hour, day, month, dayofweek);
							ct = ct->next;
						}
					printf("\n");
					}
			}
			sleep(1);
		}
	}
	*/
}

void test(crontab *ct, int min, int hour, int day, int month, int dayofweek) {
	char buf[BUFSIZ];
	if (parse_execute_term(ct->min, min) &&
		parse_execute_term(ct->hour, hour) &&
		parse_execute_term(ct->day, day) &&
		parse_execute_term(ct->month, month) &&
		parse_execute_term(ct->dayofweek, dayofweek)) {
			sprintf(buf, "%s &", ct->op);
			system(ct->op);
	
			sprintf(buf, "run %s %s %s %s %s %s\n", ct->min, ct->hour, ct->day, ct->dayofweek, ct->month, ct->op);
			log_crontab(buf);
	}
}

void print_log(const char *str) {
	//fputs(str, stderr);
	syslog(LOG_INFO, "%s", str);
}

/**
	프로세스를 daemon 프로세스로 만드는 함수
	*/
void init_daemon() {
	pid_t pid;
	int fd, maxfd;

	if (fork() != 0)
		exit(2);

	pid = getpid();
	setsid();
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	maxfd = getdtablesize();

	for (fd = 0; fd < maxfd; fd++)
		close(fd);

	umask(0);
	fd = open("/dev/null", O_RDWR);
	dup(0);
	dup(0);
}
