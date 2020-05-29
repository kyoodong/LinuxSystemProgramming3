#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>
#include "core.h"

int print_prompt();
int parse_input(char *input);
int process_add(crontab *cp);
int process_remove(int num);
int validation_check(const char *term);


crontab head;

int main(int argc, char *argv[]) {
	read_crontab_file(&head);

	while (1) {
		if (print_prompt() < 0)
			break;
	}
	exit(0);
}


/**
  프롬프트를 출력하는 함수
  @return exit 인 경우 -1, 아닌 경우 0
  */
int print_prompt() {
	char buf[BUF_SIZE];
	crontab *node;
	int i = 0;

	print_crontab(&head);
	memset(buf, 0, sizeof(buf));
	printf("\n%s> ", STD_ID); 
	fgets(buf, sizeof(buf), stdin);
	buf[strlen(buf) - 1] = '\0';
	if (parse_input(buf) == 1)
		return -1;
	return 0;
}

/**
  add 명령어에서 실행 주기에 허용되지 않은 문자가 들어있는지 확인하는 함수
  @param term 실행 주기 문자열
  @return 문제 없는 실행주기 문자열이면 1, 아니면 0
  */
int validation_check(const char *term) {
	char *str = "1234567890*-,/";
	int is_matched;
	while (*term != '\0') {
		is_matched = 0;
		for (int i = 0; str[i] != '\0'; i++) {
			if (*term == str[i]) {
				is_matched = 1;
				break;
			}
		}

		if (!is_matched)
			return 0;
		term++;
	}
	return 1;
}

/**
  입력 파싱하는 함수
  @param input 입력 문자열
  @return
  */
int parse_input(char *input) {
	char *p;
	crontab *crontab_node;

	if (!strncmp(input, "exit", 4)) {
		return 1;
	}

	p = strtok(input, " ");

	if (!strcmp(p, "add")) {
		crontab_node = calloc(1, sizeof(crontab));

		// min
		p = strtok(NULL, " ");
		if (p == NULL || !validation_check(p)) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->min, p);

		// hour
		p = strtok(NULL, " ");
		if (p == NULL || !validation_check(p)) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->hour, p);

		// day
		p = strtok(NULL, " ");
		if (p == NULL || !validation_check(p)) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->day, p);

		// month
		p = strtok(NULL, " ");
		if (p == NULL || !validation_check(p)) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->month, p);

		// dayofweek
		p = strtok(NULL, " ");
		if (p == NULL || !validation_check(p)) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->dayofweek, p);

		p += strlen(p) + 1;
		while (*p == ' ')
			p++;

		// op
		if (*p == '\0') {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}

		strcpy(crontab_node->op, p);
		if (process_add(crontab_node) < 0)
			return -1;
		return 0;
	} else if (!strcmp(p, "remove")) {
		p = strtok(NULL, " ");
		if (p == NULL) {
			fprintf(stderr, "remove input error\n");
			return -1;
		}

		int num = atoi(p);
		if (process_remove(num) < 0)
			return -1;
	}

	return -1;
}

/**
  add 명령을 처리하는 함수
  @param cp add할 crontab 포인터
  @return
  */
int process_add(crontab *cp) {
	FILE *fp;
	char buf[BUFSIZ];

	if ((fp = fopen(CRONTAB_FILE, "a+")) == NULL) {
		fprintf(stderr, "fopen error for %s\n", CRONTAB_FILE);
		return -1;
	}

	if (add_crontab(&head, cp) < 0) {
		fprintf(stderr, "add_crontab error\n");
		fclose(fp);
		return -1;
	}

	fprintf(fp, "%s %s %s %s %s %s\n", cp->min, cp->hour, cp->day, cp->month, cp->dayofweek, cp->op);
	fclose(fp);

	sprintf(buf, "add %s %s %s %s %s %s\n", cp->min, cp->hour, cp->day, cp->month, cp->dayofweek, cp->op);
	log_crontab(buf);
	return 0;
}

/**
  remove 명령을 처리하는 함수
  @param num 삭제 할 명령의 인덱스
  @return
  */
int process_remove(int num) {
	crontab *tmp;
	crontab *cpy;
	FILE *fp;
	char buf[BUFSIZ];

	tmp = &head;
	for (int i = 0; i <= num; i++) {
		if (tmp->next == NULL) {
			printf("잘못된 번호 입니다.\n");
			return 1;
		}

		tmp = tmp->next;
	}

	// 노드 삭제 도중 에러 발생 시 복구시키기 위함
	cpy = calloc(1, sizeof(crontab));
	strcpy(cpy->min, tmp->min);
	strcpy(cpy->hour, tmp->hour);
	strcpy(cpy->day, tmp->day);
	strcpy(cpy->month, tmp->month);
	strcpy(cpy->dayofweek, tmp->dayofweek);
	strcpy(cpy->op, tmp->op);

	if (remove_crontab(tmp) < 0) {
		free(cpy);
		return -1;
	}

	// 파일 재작성
	if ((fp = fopen(CRONTAB_FILE, "w")) == NULL) {
		fprintf(stderr, "fopen error for %s\n", CRONTAB_FILE);

		// 파일 적용에 실패하면 다시 리스트에 복귀시킴
		add_crontab(&head, cpy);
		return -1;
	}

	tmp = head.next;
	while (tmp != NULL) {
		fprintf(fp, "%s %s %s %s %s %s\n", tmp->min, tmp->hour, tmp->day, tmp->month, tmp->dayofweek, tmp->op);
		tmp = tmp->next;
	}

	fclose(fp);

	sprintf(buf, "remove %s %s %s %s %s %s\n", cpy->min, cpy->hour, cpy->day, cpy->month, cpy->dayofweek, cpy->op);
	log_crontab(buf);
	free(cpy);
	cpy = NULL;
	return 0;
}
