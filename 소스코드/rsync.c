#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

int is_same_file(const char *src, const char *dest);
void onexit();
int sync_file(const char *src, const char *dest);
void sync_dir(const char *src, const char *dest);
void lock_file(int fd);
void unlock_file(int fd);


char backup_filepath[BUFSIZ];

int main(int argc, char *argv[]) {
	char buf[BUFSIZ];
	char *fname;
	struct stat statbuf;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <src> <dest>\n", argv[0]);
		exit(1);
	}

	atexit(onexit);

	// src, dest 가 없는 파일인 경우
	if (access(argv[1], F_OK) != 0 || access(argv[2], F_OK) != 0) {
		fprintf(stderr, "usage: %s <src> <dest>\n", argv[0]);
		exit(1);
	}

	if (stat(argv[2], &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", argv[2]);
		exit(1);
	}

	// dest 가 디렉토리가 아닌 경우
	if (!S_ISDIR(statbuf.st_mode)) {
		fprintf(stderr, "usage: %s <src> <dest>\n", argv[0]);
		exit(1);
	}

	if (stat(argv[1], &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", argv[1]);
		exit(1);
	}

	// src 가 디렉토리인 경우
	if (S_ISDIR(statbuf.st_mode))
		sync_dir(argv[1], argv[2]);
	else {
		fname = strrchr(argv[1], '/');
		if (fname == 0)
			fname = argv[1];
		else
			fname++;
	
		sprintf(buf, "%s/%s", argv[2], fname);

		// src와 dest 가 다른 파일이라면 동기화
		if (!is_same_file(argv[1], buf)) {
			sync_file(argv[1], buf);
		}
	}

	exit(0);
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
  @param src 입력 파일 경로
  @param dest 대상 파일 경로
  @return 동기화 성공 시 0, 에러 시 -1 리턴
  */
int sync_file(const char *src, const char *dest) {
	int fd;
	int srcfd;
	char buf[BUFSIZ];
	ssize_t length;
	struct stat statbuf;
	struct utimbuf utimbuf;

	sprintf(backup_filepath, "%sXXXXXX", dest);

	// 임시 파일, SIGINT 발생 시 되돌리기 백업용
	if ((fd = mkstemp(backup_filepath)) < 0) {
		fprintf(stderr, "mkstemp error\n");
		exit(1);
	}
	
	// src 파일 열기
	if ((srcfd = open(src, O_RDONLY)) < 0) {
		fprintf(stderr, "open error for %s\n", src);
		exit(1);
	}

	lock_file(srcfd);

	length = lseek(srcfd, 0, SEEK_END);
	if (length < 0 || lseek(srcfd, 0, SEEK_SET) < 0) {
		fprintf(stderr, "lseek error for %s\n", src);
		unlock_file(srcfd);
		exit(1);
	}


	// src 파일을 임시파일에 복사
	while ((length = read(srcfd, buf, sizeof(buf))) > 0)
		write(fd, buf, length);

	close(srcfd);
	close(fd);

	if (stat(src, &statbuf) < 0) {
		fprintf(stderr, "stat error for %s\n", src);
		unlock_file(srcfd);
		exit(1);
	}

	// 원본 파일과 권한을 똑같이 맞춤
	if (chmod(backup_filepath, statbuf.st_mode) < 0) {
		fprintf(stderr, "chmod error for %s\n", backup_filepath);
		unlock_file(srcfd);
		exit(1);
	}

	// 원본 파일과 수정 시간을 맞춤
	utimbuf.actime = statbuf.st_atime;
	utimbuf.modtime = statbuf.st_mtime;
	if (utime(backup_filepath, &utimbuf) < 0) {
		fprintf(stderr, "utime error\n");
		unlock_file(srcfd);
		exit(1);
	}

	// @TODO 디버깅용
	sleep(15);

	// 임시 파일을 dest 로 바꿔치기
	if (rename(backup_filepath, dest) < 0) {
		fprintf(stderr, "rename error for %s to %s\n", backup_filepath, dest);
		unlock_file(srcfd);
		exit(1);
	}

	// 성공적 종료
	backup_filepath[0] = '\0';
	return 0;
}

/**
  프로세스 종료 직전 정리하는 함수
  동기화 하기위해 만들어진 백업 임시 파일을 삭제함
  */
void onexit() {
	printf("onexit()\n");
	if (backup_filepath[0] != 0) {
		unlink(backup_filepath);
	}
}

void sync_dir(const char *src, const char *dest) {

}

/**
  파일을 잠그는 함수
  @param fd 파일 디스크립터
  */
void lock_file(int fd) {
	struct flock lock;

	// 파일 잠금
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
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
