#define _XOPEN_SOURCE 500
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include "core.h"
#include <sys/stat.h>
#include <sys/types.h>

int is_same_file(const char *src, const char *dest);
void onexit();
void on_sigint(int sig);
int sync_file(int argc, char *argv[], const char *src, const char *dest, int toption);
void sync_dir(int argc, char *argv[], const char *src, const char *dest, int roption, int toption, int moption, int depth);
void lock_file(int fd, int length);
void unlock_file(int fd);
void log_rsync(int argc, char *argv[], const char *str);
void copy_file(const char *src, const char *dest);
void remove_dir(const char *dirpath);

typedef struct node {
	char fname[BUF_SIZE];
	struct stat stat;
	struct node *prev, *next;
} node;

char backup_filepath[BUF_SIZE];
int already_exist;

int main(int argc, char *argv[]) {
	char buf[BUF_SIZE];
	struct stat statbuf;
	struct sigaction sigint;
	char *src, *dst, *fname;
	char op;
	int roption = 0;
	int toption = 0;
	int moption = 0;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <src> <dest>\n", argv[0]);
		exit(1);
	}

	// 종료 액션 등록
	atexit(onexit);

	// SIGINT 액션 등록
	sigint.sa_flags = 0;
	sigint.sa_handler = on_sigint;
	sigemptyset(&sigint.sa_mask);
	if (sigaction(SIGINT, &sigint, NULL) < 0) {
		fprintf(stderr, "sigaction error\n");
		exit(1);
	}

	while ((op = getopt(argc, argv, "rtm")) != -1) {
		switch (op) {
			case 'r':
				roption = 1;
				break;

			case 't':
				toption = 1;
				break;

			case 'm':
				moption = 1;
				break;

			case '?':
				break;
		}
	}

	src = argv[optind];
	dst = argv[optind + 1];

	// src, dest 가 없는 파일인 경우
	if (access(src, F_OK) != 0 || access(dst, F_OK) != 0) {
		fprintf(stderr, "usage: %s <src> <dest>\n", argv[0]);
		exit(1);
	}

	if (stat(dst, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", dst);
		exit(1);
	}

	// dest 가 디렉토리가 아닌 경우
	if (!S_ISDIR(statbuf.st_mode)) {
		fprintf(stderr, "usage: %s <src> <dest>\n", argv[0]);
		exit(1);
	}

	if (stat(src, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", src);
		exit(1);
	}

	// src 가 디렉토리인 경우
	if (S_ISDIR(statbuf.st_mode)) {
		fname = strrchr(src, '/');
		if (fname == NULL)
			fname = src;
		else
			fname ++;

		sprintf(buf, "%s/%s", dst, fname);
		sync_dir(argc, argv, src, buf, roption, toption, moption, 0);
	}
	else {
		sync_file(argc, argv, src, dst, toption);
	}

	exit(0);
}

/**
  같은 파일인지 노드로서 검사해주는 함수
  @param lhs 검사할 노드1
  @param rhs 검사할 노드 2
  @return 같으면 1 아니면 0
  */
int is_same_node(node *lhs, node *rhs) {
	if (strcmp(lhs->fname, rhs->fname))
		return 0;

	if (lhs->stat.st_size != rhs->stat.st_size || lhs->stat.st_mtime != rhs->stat.st_mtime)
		return 0;

	return 1;
}

/**
  head에 노드를 추가해주는 함수
  @param head 노드 원본 리스트
  @param elem 추가할 노드
  */
void insert_node(node *head, node *elem) {
	elem->next = head->next;
	elem->prev = head;
	head->next = elem;
}

/**
  노드를 삭제해주는 함수
  @param elem 삭제할 노드
  */
void remove_node(node *elem) {
	if (elem->prev != NULL) {
		elem->prev->next = elem->next;
	}

	if (elem->next != NULL) {
		elem->next->prev = elem->prev;
	}

	free(elem);
}

int is_empty_list(node *head) {
	return head->next == NULL;
}

/**
  두 파일이 같은 파일인지 검사해주는 함수
  @param src 입력 파일 경로
  @param dest 대상 파일 경로
  @return 입력파일과 대상 파일의 이름, 크기, 수정 시간이 같으면 1 아니면 0
  */
int is_same_file(const char *src, const char *dest) {
	struct stat srcstat, deststat;

	// 아예 없는 파일인 경우
	if (access(dest, F_OK) != 0)
		return 0;

	if (stat(src, &srcstat) < 0 || stat(dest, &deststat) < 0) {
		fprintf(stderr, "stat error for %s %s\n", src, dest);
		exit(1);
	}

	// 수정 시간이 다른 경우
	if (srcstat.st_mtime != deststat.st_mtime)
		return 0;

	// 파일 크기가 다른 경우
	if (srcstat.st_size != deststat.st_size)
		return 0;

	return 1;
}

/**
  파일을 동기화 시켜주는 함수
  @param argc 프로그램 인자 수
  @param argv 프로그램 인자 벡터
  @param src 입력 파일 경로
  @param dest 대상 파일 경로
  @param toption t 옵션 여부
  @return 동기화 성공 시 0, 에러 시 -1 리턴
  */
int sync_file(int argc, char *argv[], const char *src, const char *dest, int toption) {
	int fd;
	int srcfd;
	char buf[BUF_SIZE];
	char cwd[BUF_SIZE];
	ssize_t length;
	struct stat statbuf;
	struct utimbuf utimbuf;
	const char *fname, *path;

	fname = strrchr(src, '/');
	if (fname == NULL) {
		fname = src;
		path = NULL;
	}
	else {
		fname = '\0';
		fname++;
		path = src;
	}

	sprintf(buf, "%s/%s", dest, fname);
	if (is_same_file(src, buf))
		return -1;
	
	// src 파일 열기
	if ((srcfd = open(src, O_RDONLY)) < 0) {
		fprintf(stderr, "open error for %s\n", src);
		exit(1);
	}

	length = lseek(srcfd, 0, SEEK_END);
	if (length < 0 || lseek(srcfd, 0, SEEK_SET) < 0) {
		fprintf(stderr, "lseek error for %s\n", src);
		exit(1);
	}

	// 파일 잠금
	lock_file(srcfd, length);


	// toption
	if (toption) {
		if (path != NULL) {
			getcwd(cwd, sizeof(cwd));
			chdir(path);
		}

		sprintf(backup_filepath, "%s.tar", fname);

		// tar 생성
		sprintf(buf, "tar -cvf %s.tar %s", backup_filepath, fname);
		system(buf);

		// tar 파일 옮기기
		sprintf(buf, "%s.tar", fname);
		stat(buf, &statbuf);

		sprintf(buf, "tar -xvf %s.tar -C %s", fname, dest);
		system(buf);

		if (path != NULL)
			chdir(cwd);

		// 로깅
		sprintf(buf, "\ttotalSize %ld\n\t%s", statbuf.st_size, fname);
		log_rsync(argc, argv, buf);

		// tar 파일 삭제
		unlink(backup_filepath);
	}
	else {
		sprintf(backup_filepath, "%sXXXXXX", src);

		// 임시 파일, SIGINT 발생 시 되돌리기 백업용
		if ((fd = mkstemp(backup_filepath)) < 0) {
			fprintf(stderr, "mkstemp error\n");
			exit(1);
		}
		copy_file(src, backup_filepath);
		close(fd);

		// @TODO 디버깅용
		//sleep(15);
	
		sprintf(buf, "%s/%s", dest, fname);

		// 임시 파일을 dest 로 바꿔치기
		if (rename(backup_filepath, buf) < 0) {
			fprintf(stderr, "rename error for %s to %s\n", backup_filepath, dest);
			unlock_file(srcfd);
			exit(1);
		}

		sprintf(buf, "\t%s %ldbytes", src, statbuf.st_size);
		log_rsync(argc, argv, buf);
	}


	// 성공적 종료
	unlock_file(srcfd);
	close(srcfd);
	backup_filepath[0] = '\0';
	return 0;
}

/**
  프로세스 종료 직전 정리하는 함수
  동기화 하기위해 만들어진 백업 임시 파일을 삭제함
  */
void onexit() {
	struct stat statbuf;
	char buf[BUF_SIZE];
	char *c;

	if (backup_filepath[0] != 0) {
		stat(backup_filepath, &statbuf);
		
		// 디렉토리 삭제 중 종료
		if (S_ISDIR(statbuf.st_mode)) {
			// 기존 디렉토리가 있었다면
			if (already_exist) {
				// 동기화중인 디렉토리를 삭제하고 백업 디렉토리를 복원
				strcpy(buf, backup_filepath);
				c = strstr(buf, ".bak");
				*c = 0;

				remove_dir(buf);
				rename(backup_filepath, buf);
			} else {
				// 기존디렉토리가 없다면 원래 없었으니 백업 디렉토리도 삭제
				remove_dir(backup_filepath);
			}
		}
		else
			unlink(backup_filepath);
	}
}

void on_sigint(int sig) {
	onexit();
	exit(0);
}

/**
  파일을 복사하는 함수
  @param src 복사할 파일 경로
  @param dest 새 파일 경로
  */
void copy_file(const char *src, const char *dest) {
	int fd;
	int srcfd;
	char buf[BUF_SIZE];
	struct utimbuf utimbuf;
	struct stat statbuf;
	size_t length;

	// src 파일이 없으면 취소
	if (access(src, F_OK) != 0)
		return;

	if (stat(src, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", src);
		unlock_file(srcfd);
		exit(1);
	}

	if ((srcfd = open(src, O_RDONLY)) < 0) {
		fprintf(stderr, "open error for %s\n", src);
		exit(1);
	}

	if ((fd = open(dest, O_RDWR | O_CREAT | O_TRUNC, statbuf.st_mode)) < 0) {
		fprintf(stderr, "open error for %s\n", dest);
		exit(1);
	}

	// src 파일을 임시파일에 복사
	while ((length = read(srcfd, buf, sizeof(buf))) > 0)
		write(fd, buf, length);

	// 원본 파일과 수정 시간을 맞춤
	utimbuf.actime = statbuf.st_atime;
	utimbuf.modtime = statbuf.st_mtime;
	if (utime(dest, &utimbuf) < 0) {
		fprintf(stderr, "utime error\n");
		unlock_file(srcfd);
		exit(1);
	}

	close(fd);
}

void copy_dir(const char *src, const char *dest) {
	struct dirent **dirp;
	int count;
	struct stat statbuf;
	char buf[BUF_SIZE];
	char buf2[BUF_SIZE];
	int fd1, fd2;
	size_t length;

	if (stat(src, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", src);
		exit(1);
	}

	mkdir(dest, statbuf.st_mode);
	
	if ((count = scandir(src, &dirp, NULL, NULL)) < 0) {
		fprintf(stderr, "scandir error for %s\n", src);
		exit(1);
	}

	for (int i = 0; i < count; i++) {
		if (!strcmp(dirp[i]->d_name, ".") || !strcmp(dirp[i]->d_name, ".."))
			continue;

		sprintf(buf, "%s/%s", src, dirp[i]->d_name);
		sprintf(buf2, "%s/%s", dest, dirp[i]->d_name);
		if (stat(buf, &statbuf) < 0) {
			fprintf(stderr, "stat error for %s\n", buf);
			exit(1);
		}

		// 디렉토리
		if (S_ISDIR(statbuf.st_mode)) {
			copy_dir(buf, buf2);
		}
		// 일반 파일
		else {
			copy_file(buf, buf2);
		}
	}

	for (int i = 0; i < count; i++)
		free(dirp[i]);
	free(dirp);
}

void remove_dir(const char *dirpath) {
	struct dirent **dirp;
	int count;
	char buf[BUF_SIZE];
	struct stat statbuf;

	if (access(dirpath, F_OK) != 0)
		return;

	if ((count = scandir(dirpath, &dirp, NULL, NULL)) < 0) {
		fprintf(stderr, "scandir error for %s\n", dirpath);
		exit(1);
	}

	for (int i = 0; i < count; i++) {
		if (!strcmp(dirp[i]->d_name, ".") || !strcmp(dirp[i]->d_name, ".."))
			continue;

		sprintf(buf, "%s/%s", dirpath, dirp[i]->d_name);
		if (stat(buf, &statbuf) < 0) {
			fprintf(stderr, "stat error for %s\n", buf);
			exit(1);
		}

		// 디렉토리
		if (S_ISDIR(statbuf.st_mode)) {
			remove_dir(buf);
		}

		// 일반 파일
		remove(buf);
	}
	remove(dirpath);

	for (int i = 0; i < count; i++)
		free(dirp[i]);
	free(dirp);
}

void sync_dir(int argc, char *argv[], const char *src, const char *dest, int roption, int toption, int moption, int depth) {
	const char *fname;
	char buf[BUF_SIZE];
	char buf2[BUF_SIZE];
	node src_list, dst_list, *tmp;
	struct dirent **dirp;
	int count;
	struct stat statbuf;
	node *prev;

	fname = strrchr(src, '/');
	if (fname == NULL) {
		fname = src;
	} else {
		fname++;
	}

	// 같은 파일이 이미 존재
	if (is_same_file(src, dest))
		return;

	// 최초 호출
	if (depth == 0) {
		// dest 파일이 존재하긴 하다면
		if (access(dest, F_OK) == 0) {
			already_exist = 1;
			// dest 디렉토리 백업
			sprintf(backup_filepath, "%s.bak", dest);
			copy_dir(dest, backup_filepath);
		} else {
			already_exist = 0;
			strcpy(backup_filepath, dest);
			
			if (stat(src, &statbuf) < 0) {
				fprintf(stderr, "stat error for %s\n", src);
				exit(1);
			}

			mkdir(dest, statbuf.st_mode);
		}
	}
	// 재귀 호출
	else {
		// 없는 디렉토리라면 디렉토리 생성
		if (access(dest, F_OK) != 0) {
			if (stat(src, &statbuf) < 0) {
				fprintf(stderr, "stat error for %s\n", src);
				exit(1);
			}

			mkdir(dest, statbuf.st_mode);
		}
		else {
			if (stat(dest, &statbuf) < 0) {
				fprintf(stderr, "stat error for %s\n", dest);
				exit(1);
			}
	
			// dest 가 일반 파일이면
			if (!S_ISDIR(statbuf.st_mode))
				// 삭제
				remove(dest);
		}
	}

	// src 디렉토리 읽기
	if ((count = scandir(src, &dirp, NULL, NULL)) < 0) {
		fprintf(stderr, "scandir error for %s\n", src);
		exit(1);
	}

	for (int i = 0; i < count; i++) {
		if (!strcmp(dirp[i]->d_name, ".") || !strcmp(dirp[i]->d_name, ".."))
			continue;

		sprintf(buf, "%s/%s", src, dirp[i]->d_name);
		if (stat(buf, &statbuf) < 0) {
			fprintf(stderr, "stat error for %s\n", buf);
			exit(1);
		}

		if (S_ISDIR(statbuf.st_mode)) {
			// roption 꺼져있으면 디렉토리는 제낌
			if (!roption)
				continue;
		}

		// 노드 세팅
		tmp = malloc(sizeof(node));
		strcpy(tmp->fname, dirp[i]->d_name);
		tmp->stat = statbuf;

		// 노드 리스트에 추가
		insert_node(&src_list, tmp);
	}

	for (int i = 0; i < count; i++)
		free(dirp[i]);
	free(dirp);

	// dest 디렉토리 읽기
	if ((count = scandir(dest, &dirp, NULL, NULL)) < 0) {
		fprintf(stderr, "scandir error for %s\n", dest);
		exit(1);
	}

	for (int i = 0; i < count; i++) {
		if (!strcmp(dirp[i]->d_name, ".") || !strcmp(dirp[i]->d_name, ".."))
			continue;

		sprintf(buf, "%s/%s", dest, dirp[i]->d_name);
		if (stat(buf, &statbuf) < 0) {
			fprintf(stderr, "stat error for %s\n", buf);
			exit(1);
		}

		// roption 꺼져있으면 디렉토리는 제낌
		if (!roption) {
			if (S_ISDIR(statbuf.st_mode))
				continue;
		}

		// 노드 세팅
		tmp = malloc(sizeof(node));
		strcpy(tmp->fname, dirp[i]->d_name);
		tmp->stat = statbuf;

		// 노드 리스트에 추가
		insert_node(&dst_list, tmp);
	}

	for (int i = 0; i < count; i++)
		free(dirp[i]);
	free(dirp);

	// 동기화
	tmp = src_list.next;
	while (!is_empty_list(&src_list)) {
		strcpy(buf, src);
		strcat(buf, "/");
		strcat(buf, tmp->fname);

		strcpy(buf2, dest);
		strcat(buf2, "/");
		strcat(buf2, tmp->fname);
		
		// 동일한 파일 존재 시
		if (is_same_file(buf, buf2)) {
			// dst_list 에서 src 노드를 삭제
			// 추후 moption에서 남은 dst_list 파일들을 삭제함
			for (node *n = dst_list.next; n != NULL; n = n->next) {
				if (is_same_node(tmp, n)) {
					remove_node(n);
					break;
				}
			}

			// 노드 삭제 및 이동
			prev = tmp;
			tmp = tmp->next;
			remove_node(prev);
			continue;
		}

		// 디렉토리
		if (S_ISDIR(tmp->stat.st_mode)) {
			// 이미 있는 디렉토리라면 삭제
			sync_dir(argc, argv, buf, buf2, roption, toption, moption, depth + 1);
		}

		// 일반 파일
		else {
			copy_file(buf, buf2);
		}

		// dst_list 에서 src 노드를 삭제
		// 추후 moption에서 남은 dst_list 파일들을 삭제함
		for (node *n = dst_list.next; n != NULL; n = n->next) {
			if (is_same_node(tmp, n)) {
				remove_node(n);
				break;
			}
		}

		// 노드 삭제 및 이동
		prev = tmp;
		tmp = tmp->next;
		remove_node(prev);
	}

	tmp = dst_list.next;
	while (!is_empty_list(&dst_list)) {
		if (moption) {
			strcpy(buf, dest);
			strcat(buf, "/");
			strcat(buf, tmp->fname);
			// 디렉토리
			if (S_ISDIR(tmp->stat.st_mode))
				remove_dir(buf);
			else
				remove(buf);
		}
		prev = tmp;
		tmp = tmp->next;
		remove_node(prev);
	}

	sleep(15);

	if (already_exist)
		remove_dir(backup_filepath);
	backup_filepath[0] = 0;
}

/**
  파일을 잠그는 함수
  @param fd 파일 디스크립터
  @param length 파일 크기
  */
void lock_file(int fd, int length) {
	struct flock lock;

	// 파일 잠금
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = length;
	lock.l_pid = getpid();

	fcntl(fd, F_SETLK, &lock);
}

/**
  파일을 잠그는 함수
  @param fd 파일 디스크립터
  */
void unlock_file(int fd) {
	struct flock lock;

	// 파일 잠금
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	lock.l_pid = getpid();

	fcntl(fd, F_SETLK, &lock);
}

/**
  @param argc 프로그램 인자 갯수
  @param argv 프로그램 인자 벡터
  @param str 로그 내용
  */
void log_rsync(int argc, char *argv[], const char *str) {
	FILE *fp;
	time_t t;
	char buf[BUF_SIZE];

	t = time(NULL);

	if ((fp = fopen("ssu_rsync_log", "a")) == NULL) {
		fprintf(stderr, "open error for ssu_rsync_log\n");
		exit(1);
	}

	strcpy(buf, argv[0]);
	for (int i = 1; i < argc; i++) {
		strcat(buf, " ");
		strcat(buf, argv[i]);
	}

	fprintf(fp, "[%s] %s\n%s\n",
			strtok(ctime(&t), "\n"),
			buf,
			str
	);
	fclose(fp);
}
