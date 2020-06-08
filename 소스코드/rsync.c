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

#define USAGE "usage: ssu_rsync [option] <src> <dest>\n\t-r : recursive sync\n\t-t : sync using tar\n\t-m : Fully sync\n"

typedef struct node {
	char fname[BUF_SIZE];
	struct stat stat;
	struct node *prev, *next;
} node;

node glob_sync_list;
node glob_delete_list;


int is_same_file(const char *src, const char *dest);
void onexit();
void on_sigint(int sig);
int sync_file(int argc, char *argv[], char *src, const char *dest, int toption);
void sync_dir(int argc, char *argv[], const char *src, const char *dest, int roption, int toption, int moption, int depth);
void lock_file(int fd, int length);
void unlock_file(int fd);
void log_rsync(int argc, char *argv[], const char *str);
void copy_file(const char *src, const char *dest);
void remove_dir(const char *dirpath);


char backup_filepath[BUF_SIZE];
int already_exist;

int main(int argc, char *argv[]) {
	char buf[BUF_SIZE];
	struct stat statbuf;
	struct sigaction sigint;
	char src[BUF_SIZE], dst[BUF_SIZE], *fname;
	char op;
	int roption = 0;
	int toption = 0;
	int moption = 0;

	if (argc < 3) {
		fprintf(stderr, USAGE);
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

	// src, dest 가 없는 파일인 경우
	if (access(argv[optind], F_OK) != 0 || access(argv[optind + 1], F_OK) != 0) {
		fprintf(stderr, USAGE);
		exit(1);
	}

	// src 내에 '/' 가 한 개도 없다면
	if ((fname = strrchr(argv[optind], '/')) == NULL) {
		sprintf(buf, "./%s", argv[optind]);
		
		if (realpath(buf, src) == NULL) {
			fprintf(stderr, "realpath error for %s\n", buf);
			exit(1);
		}
	}
	// src 내에 '/' 가 있다면
	else {
		if (realpath(argv[optind], src) == NULL) {
			fprintf(stderr, "realpath error for %s\n", argv[optind]);
			exit(1);
		}
	}

	// '/' 로 끝나는 경로
	if (src[strlen(src) - 1] == '/')
		src[strlen(src) - 1] = 0;

	// dst 내에 '/' 가 한 개도 없다면
	if ((fname = strrchr(argv[optind + 1], '/')) == NULL) {
		sprintf(buf, "./%s", argv[optind + 1]);
		
		if (realpath(buf, dst) == NULL) {
			fprintf(stderr, "realpath error for %s\n", buf);
			exit(1);
		}
	}
	// dst 내에 '/' 가 있다면
	else {
		if (realpath(argv[optind + 1], dst) == NULL) {
			fprintf(stderr, "realpath error for %s\n", argv[optind + 1]);
			exit(1);
		}
	}

	// '/' 로 끝나는 경로
	if (dst[strlen(dst) - 1] == '/')
		dst[strlen(dst) - 1] = 0;


	if (stat(dst, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", dst);
		exit(1);
	}

	// dest 가 디렉토리가 아닌 경우
	if (!S_ISDIR(statbuf.st_mode)) {
		fprintf(stderr, USAGE);
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

	if (head->next != NULL) {
		head->next->prev = elem;
	}
	head->next = elem;
}

/**
  노드를 삭제해주는 함수
  @param elem 삭제할 노드
  */
void remove_node(node *elem) {
	if (elem->prev != NULL)
		elem->prev->next = elem->next;

	if (elem->next != NULL)
		elem->next->prev = elem->prev;

	elem->next = elem->prev = NULL;

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
int sync_file(int argc, char *argv[], char *src, const char *dest, int toption) {
	int fd;
	int srcfd;
	char buf[BUFSIZ];
	char cwd[BUF_SIZE];
	char path[BUF_SIZE];
	ssize_t length;
	struct stat statbuf;
	struct utimbuf utimbuf;
	char *fname;

	// src 읽기 권한 없는 경우
	if (access(src, R_OK) != 0) {
		fprintf(stderr, USAGE);
		exit(1);
	}

	// dest 디렉토리 접근권한 없는 경우  
	if (access(dest, F_OK) == 0) {
		if (access(dest, R_OK) != 0 || access(dest, W_OK) != 0 || access(dest, X_OK) != 0) {
			fprintf(stderr, USAGE);
			exit(1);
		}
	}

	// 같은 파일이 이미 존재
	if (is_same_file(src, dest))
	fname = strrchr(src, '/');
	*fname = 0;
	strcpy(path, src);
	*fname++ = '/';

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
		// 해당 디렉토리로 chdir. tar 할 때 디렉토리경로까지 묶이는 문제를 해결하기 위함
		getcwd(cwd, sizeof(cwd));
		chdir(path);

		sprintf(backup_filepath, "%s.tar", src);

		// tar 생성
		sprintf(buf, "tar -cf %s %s", backup_filepath, fname);
		system(buf);

		sprintf(buf, "tar -xf %s -C %s", backup_filepath, dest);
		system(buf);

		// tar 파일 삭제
		unlink(backup_filepath);

		// 원래 작업디렉토리로 복구
		chdir(cwd);

		// 로깅
		sprintf(buf, "\ttotalSize %ld\n\t%s", statbuf.st_size, fname);
		log_rsync(argc, argv, buf);
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

		sprintf(buf, "\t%s %ldbytes", fname, statbuf.st_size);
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

/**
  SIGINT 캐치함수
  */
void on_sigint(int sig) {
	//onexit();
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
	struct stat statbuf, sb;
	size_t length;

	// src 파일이 없으면 취소
	if (access(src, F_OK) != 0)
		return;

	if (stat(src, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", src);
		unlock_file(srcfd);
		exit(1);
	}

	if (S_ISDIR(statbuf.st_mode))
		return;

	// dest 파일이 있으면
	if (access(dest, F_OK) == 0) {
		// 쓰기권한 체크
		if (access(dest, W_OK) != 0) {
			fprintf(stderr, USAGE);
			exit(1);
		}

		if (stat(dest, &sb) < 0) {
			fprintf(stderr, "stat error for %s\n", dest);
			exit(1);
		}

		// 삭제
		if (S_ISDIR(sb.st_mode)) {
			// 디렉토리는 access 권한까지 체크
			if (access(dest, X_OK) != 0) {
				fprintf(stderr, USAGE);
				exit(1);
			}
			remove_dir(dest);
		}
		else
			remove(dest);
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

	// 파일 소유자 수정
	chown(dest, statbuf.st_uid, statbuf.st_gid);
	/*
	if (chown(dest, statbuf.st_uid, statbuf.st_gid) < 0) {
		fprintf(stderr, "chown error for %s %d %d\n", dest, statbuf.st_uid, statbuf.st_gid);
		unlock_file(srcfd);
		exit(1);
	}
	*/

	close(fd);
}

void copy_dir(const char *src, const char *dest, int roption) {
	struct dirent **dirp;
	int count;
	struct stat statbuf;
	char buf[BUF_SIZE];
	char buf2[BUF_SIZE];
	int fd1, fd2;
	size_t length;
	struct utimbuf utimbuf;

	if (stat(src, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", src);
		exit(1);
	}

	mkdir(dest, statbuf.st_mode);

	// 원본 파일과 수정 시간을 맞춤
	utimbuf.actime = statbuf.st_atime;
	utimbuf.modtime = statbuf.st_mtime;
	if (utime(dest, &utimbuf) < 0) {
		fprintf(stderr, "utime error\n");
		exit(1);
	}

	if (!roption)
		return;
	
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
			copy_dir(buf, buf2, roption);
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

const char *get_path(const char *path, int depth) {
	const char *cp = path + strlen(path) - 1;

	// tar 로 묶을때는 절대경로로 필요하기 때문에 수정
	for (int i = 0; i <= depth + 1;) {
		if (*cp == '/')
			i++;
		cp--;
	}

	return cp + 2;
}

void sync_dir(int argc, char *argv[], const char *src, const char *dest, int roption, int toption, int moption, int depth) {
	const char *fname;
	char buf[BUFSIZ];
	char buf2[BUF_SIZE];
	char cwd[BUF_SIZE];
	node src_list, dst_list, *tmp;
	struct dirent **dirp;
	int count;
	struct stat statbuf;
	node *prev;
	int allow_insert = 0;

	src_list.next = NULL;
	src_list.prev = NULL;
	dst_list.next = NULL;
	dst_list.prev = NULL;

	fname = strrchr(src, '/');
	if (fname == NULL) {
		fname = src;
	} else {
		fname++;
	}

	// src 디렉토리 접근권한 없는 경우
	if (access(src, R_OK) != 0 || access(src, W_OK) != 0 || access(src, X_OK) != 0) {
		fprintf(stderr, USAGE);
		exit(1);
	}

	// dest 디렉토리 접근권한 없는 경우  
	if (access(dest, F_OK) == 0) {
		if (access(dest, R_OK) != 0 || access(dest, W_OK) != 0 || access(dest, X_OK) != 0) {
			fprintf(stderr, USAGE);
			exit(1);
		}
	}

	// 같은 파일이 이미 존재
	if (is_same_file(src, dest))
		return;

	// src 디렉토리 읽기
	if ((count = scandir(src, &dirp, NULL, NULL)) < 0) {
		fprintf(stderr, "scandir error for %s\n", src);
		exit(1);
	}

	// 최초 호출
	if (depth == 0) {
		// dest 파일이 존재하긴 하다면
		if (access(dest, F_OK) == 0) {
			already_exist = 1;
			// dest 디렉토리 백업
			sprintf(backup_filepath, "%s.bak", dest);
			copy_dir(dest, backup_filepath, 1);
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
		node *n;

		// 없는 디렉토리라면 디렉토리 생성
		if (access(dest, F_OK) != 0) {
			if (stat(src, &statbuf) < 0) {
				fprintf(stderr, "stat error for %s\n", src);
				exit(1);
			}

			copy_dir(src, dest, 0);
		}
		else {
			if (stat(dest, &statbuf) < 0) {
				fprintf(stderr, "stat error for %s\n", dest);
				exit(1);
			}
	
			// dest 가 일반 파일이면
			if (!S_ISDIR(statbuf.st_mode)) {
				// 삭제
				remove(dest);
			}
		}

		// src가 빈 디렉토리인 경우
		if (count == 2) {
			n = malloc(sizeof(node));
			strcpy(n->fname, get_path(src, depth - 1));
			n->stat.st_size = 0;
			insert_node(&glob_sync_list, n);
		}
	}

	// src 디렉토리에 어떤 파일이 있는지 확인
	for (int i = 0; i < count; i++) {
		if (!strcmp(dirp[i]->d_name, ".") || !strcmp(dirp[i]->d_name, ".."))
			continue;

		sprintf(buf, "%s/%s", src, dirp[i]->d_name);

		// 권한 없음
		if (access(buf, R_OK) != 0) {
			fprintf(stderr, USAGE);
			exit(1);
		}

		if (stat(buf, &statbuf) < 0) {
			fprintf(stderr, "stat error for %s\n", buf);
			exit(1);
		}

		if (S_ISDIR(statbuf.st_mode)) {
			// 디렉토리는 write, execute 권한 추가 검사
			if (access(buf, W_OK) != 0 || access(buf, X_OK) != 0) {
				fprintf(stderr, USAGE);
				exit(1);
			}
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

		if (S_ISDIR(statbuf.st_mode)) {
			// 디렉토리는 write, execute 권한 추가 검사
			if (access(buf, W_OK) != 0 || access(buf, X_OK) != 0) {
				fprintf(stderr, USAGE);
				exit(1);
			}
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
	while (tmp != NULL) {
		strcpy(buf, src);
		strcat(buf, "/");
		strcat(buf, tmp->fname);

		strcpy(buf2, dest);
		strcat(buf2, "/");
		strcat(buf2, tmp->fname);
		
		// 동일한 파일 존재 시
		if (is_same_file(buf, buf2)) {
			// 디렉토리는 같은 파일이라 판명되었지만 하위 파일들이 다를 수 있음
			if (roption && S_ISDIR(tmp->stat.st_mode)) {
				sync_dir(argc, argv, buf, buf2, roption, toption, moption, depth + 1);
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
			continue;
		}

		// 디렉토리 동기화
		if (S_ISDIR(tmp->stat.st_mode)) {
			if (roption)
				sync_dir(argc, argv, buf, buf2, roption, toption, moption, depth + 1);

			else {
				struct dirent **dirp;
				int c = scandir(buf, &dirp, NULL, NULL);

				if (c == 2) {
					if (access(buf2, F_OK) == 0)
						remove(buf2);

					copy_dir(buf, buf2, roption);
					tmp->stat.st_size = 0;
					allow_insert = 1;
				}

				for (int i = 0; i < c; i++)
					free(dirp[i]);
				free(dirp);
			}
		}

		// 일반 파일
		else {
			// toption 아닐때는 파일을 직접 복사
			// toption 일때는 한 번에 tar 로 묶어서 복사
			if (!toption)
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

		char *cp = buf + strlen(buf) - 1;

		// tar 로 묶을때는 절대경로로 필요하기 때문에 수정
		for (int i = 0; i <= depth + 1;) {
			if (*cp == '/')
				i++;
			cp--;
		}
		strcpy(prev->fname, cp + 2);

		// 일반 파일만 sync 리스트에 넣음
		if (!S_ISDIR(prev->stat.st_mode) || allow_insert) {
			insert_node(&glob_sync_list, prev);
			
			if (tmp != NULL) {
				tmp->prev = NULL;
			}
		}
	}

	// 대상 디렉토리의 파일 중에서 소스 디렉토리의 파일에 없었던 파일들을 순회
	tmp = dst_list.next;
	while (tmp != NULL) {
		// 소스 디렉토리에 없는 파일은 지우는 옵션 : m
		if (moption) {
			strcpy(buf, dest);
			strcat(buf, "/");
			strcat(buf, tmp->fname);

			// 삭제
			if (S_ISDIR(tmp->stat.st_mode))
				remove_dir(buf);
			else
				remove(buf);

			prev = tmp;
			tmp = tmp->next;
			
			char *cp = buf + strlen(buf) - 1;
	
			// tar 로 묶을때는 절대경로로 필요하기 때문에 수정
			for (int i = 0; i <= depth + 1;) {
				if (*cp == '/')
					i++;
				cp--;
			}
			strcpy(prev->fname, cp + 2);

			// 삭제 리스트 추가 (로깅용)
			insert_node(&glob_delete_list, prev);
			if (tmp != NULL) {
				tmp->prev = NULL;
			}
			continue;
		}
		prev = tmp;
		tmp = tmp->next;
		remove_node(prev);
	}

	if (depth == 0) {
		memset(buf, 0, sizeof(buf));
		if (toption) {
			char *cp;
			int length = strlen(src) + 1;

			tmp = glob_sync_list.next;

			// 동기화 할 파일이 없는 경우
			if (tmp == NULL) {
				tmp = glob_delete_list.next;

				// 지워야할 항목 조차 없다면
				if (tmp == NULL) {
					exit(0);
				}

				sprintf(buf, "totalSize : 0bytes\n");
				cp = buf + strlen(buf);
			}
			
			// 동기화 할 파일 있음
			else {
				char *c;

				getcwd(cwd, sizeof(cwd));
				c = strrchr(src, '/');
				*c = 0;
				chdir(src);
				*c = '/';

				sprintf(buf, "tar -cf %s.tar", src);
				cp = buf + strlen(buf);
	
				// 동기화 해야하는 파일들 다 tar로 묶음
				while (tmp != NULL) {
					cp = stpcpy(cp, " ");
					cp = stpcpy(cp, tmp->fname);
	
					tmp = tmp->next;
				}
				system(buf);
				chdir(cwd);
	
				c = strrchr(dest, '/');
				*c = 0;
	
				// tar 해제
				sprintf(buf, "tar -xf %s.tar -C %s", src, dest);
				system(buf);

				*c = '/';
	
				// tar 크기 확인
				sprintf(buf2, "%s.tar", src);
				if (stat(buf2, &statbuf) < 0) {
					fprintf(stderr, "stat error for %s\n", buf2);
					remove(buf2);
					exit(1);
				}
	
				sprintf(buf, "totalSize : %ldbytes\n", statbuf.st_size);
	
				// 동기화
				tmp = glob_sync_list.next;
				cp = buf + strlen(buf);
	
				// 동기화 해야하는 파일들 다 tar로 묶음
				while (tmp != NULL) {
					cp += sprintf(cp, "\t%s %ldbytes\n", strchr(tmp->fname, '/') + 1, tmp->stat.st_size);
	
					prev = tmp;
					tmp = tmp->next;
					remove_node(prev);
				}

				// tar 삭제
				remove(buf2);
			}

			// 삭제 동기화
			tmp = glob_delete_list.next;

			// 삭제되는 파일들 목록
			while (tmp != NULL) {
				cp += sprintf(cp, "\t%s delete\n", strchr(tmp->fname, '/') + 1);

				prev = tmp;
				tmp = tmp->next;
				remove_node(prev);
			}

			buf[strlen(buf) - 1] = 0;
			log_rsync(argc, argv, buf);
		}
		
		// toption 없이 직접 동기화하는 작업
		else {
			// 동기화 파일 리스트
			tmp = glob_sync_list.next;
			char *cp = buf;

			// 동기화 해야하는 파일들 다 tar로 묶음
			while (tmp != NULL) {
				cp += sprintf(cp, "\t%s %ldbytes\n", strchr(tmp->fname, '/') + 1, tmp->stat.st_size);
				prev = tmp;
				tmp = tmp->next;
				//remove_node(prev);
			}

			// 삭제 파일 리스트
			tmp = glob_delete_list.next;

			// 동기화 해야하는 파일들 다 tar로 묶음
			while (tmp != NULL) {
				cp += sprintf(cp, "\t%s delete\n", strchr(tmp->fname, '/') + 1);

				prev = tmp;
				tmp = tmp->next;
				remove_node(prev);
			}

			buf[strlen(buf) - 1] = 0;
			log_rsync(argc, argv, buf);
		}

		if (already_exist)
			remove_dir(backup_filepath);

		backup_filepath[0] = 0;
	}

	if (stat(src, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", src);
		exit(1);
	}

	struct utimbuf utimbuf;
	utimbuf.actime = statbuf.st_atime;
	utimbuf.modtime = statbuf.st_mtime;
	utime(dest, &utimbuf);
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

	strcpy(buf, "ssu_rsync");
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
