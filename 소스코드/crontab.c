#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>
#include "core.h"

int print_prompt();
int parse_input(char *input);
int process_add(crontab *cp);


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
	if (parse_input(buf) < 0)
		return -1;
	return 0;
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
		return -1;
	}

	p = strtok(input, " ");

	if (!strcmp(p, "add")) {
		crontab_node = calloc(1, sizeof(crontab));

		// min
		p = strtok(NULL, " ");
		if (p == NULL) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->min, p);

		// hour
		p = strtok(NULL, " ");
		if (p == NULL) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->hour, p);

		// day
		p = strtok(NULL, " ");
		if (p == NULL) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->day, p);

		// month
		p = strtok(NULL, " ");
		if (p == NULL) {
			fprintf(stderr, "add input error\n");
			free(crontab_node);
			return -1;
		}
		strcpy(crontab_node->month, p);

		// dayofweek
		p = strtok(NULL, " ");
		if (p == NULL) {
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

		process_add(crontab_node);
		return 0;
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

	if ((fp = fopen(CRONTAB_FILE, "a+")) == NULL) {
		fprintf(stderr, "fopen error for %s\n", CRONTAB_FILE);
		return -1;
	}

	if (add_crontab(&head, cp) < 0) {
		fprintf(stderr, "add_crontab error\n");
		return -1;
	}

	fprintf(fp, "%s %s %s %s %s %s\n", cp->min, cp->hour, cp->day, cp->month, cp->dayofweek, cp->op);
	fclose(fp);
	return 0;
}
