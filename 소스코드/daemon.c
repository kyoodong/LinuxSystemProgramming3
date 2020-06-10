#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include "core.h"
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

void init_daemon();
void print_log(const char *str);
void *process_crontab();
void test(crontab *ct, int min, int hour, int day, int month, int dayofweek);

crontab head;
struct stat cronstat;
struct tm *tm;
time_t t;

int daemon_main() {
	pthread_t thread;
	int thread_id;

	// 파일 생성되기를 대기
	while (read_crontab_file(&head) < 0 || stat(CRONTAB_FILE, &cronstat) < 0) {
		print_log("Cannot open crontab file\n");
		sleep(5);
	}

	while (1) {
		// 스레드 생성
		thread_id = pthread_create(&thread, NULL, process_crontab, (void *) 0);
		if (thread_id < 0) {
			print_log("Creating thread error\n");
			exit(1);
		}

		// 스레드 종료시 자원을 반환하도록 설정
		pthread_detach(thread);
	
		sleep(60);
	}
}

int main(int argc, char *argv[]) {
	//init_daemon();
	daemon_main();
	exit(0);
}

/**
  crontab 데이터를 읽고 처리하는 함수 (스레드 실행)
  파일을 읽고, 모든 명령어를 처리하는 것이 오래걸릴 수 있으므로 스레드로 분리
  60초 주기로 실행되는 로직 상 이 주기에 대한 오차를 줄일 수 있음
  예를 들면 파일을 읽고 모든 명령어를 처리하는데 2초가 걸린다면
  실제로는 62초 주기로 실행되므로 2초가 누적되다보면 문제가 생김
  */
void *process_crontab() {
	struct stat statbuf;
	crontab *ct;
	char buf[BUFSIZ];

	if (stat(CRONTAB_FILE, &statbuf) < 0)
		pthread_exit(NULL);

	// crontab 파일이 변경되었음이 감지된 경우
	if (statbuf.st_mtime != cronstat.st_mtime) {
		// crontab 리스트를 비움
		while (!is_empty_crontab(&head))
			remove_crontab(head.next);

		// crontab 파일을 다시 읽어서 리스트 재구성
		if (read_crontab_file(&head)) {
			print_log("read_crontab_file error\n");
			pthread_exit(NULL);
		}

		cronstat = statbuf;
	}

	t = time(NULL);
	tm = localtime(&t);
	
	ct = head.next;
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
	return NULL;
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
