#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "core.h"

/**
  ssu_crontab_file 을 읽는 함수

  @param head 파싱된 데이터를 링크드 리스트 형태로 넣어줌
  @return 성공 시 0, 에러 시 -1 리턴하고 err_str 설정
  */
int read_crontab_file(crontab *head) {
	FILE *fp;
	crontab *node, *tail;

	if (head == NULL) {
		sprintf(err_str, "head is NULL\n");
		return -1;
	}

	tail = head;

	if ((fp = fopen(CRONTAB_FILE, "r")) == NULL) {
		sprintf(err_str, "[read_crontab_file] %s fopen error\n", CRONTAB_FILE);
		return -1;
	}

	while (!feof(fp)) {
		node = calloc(1, sizeof(crontab));
		if (fscanf(fp, "%s", node->min) < 0)
			break;
		fscanf(fp, "%s", node->hour);
		fscanf(fp, "%s", node->day);
		fscanf(fp, "%s", node->month);
		fscanf(fp, "%s", node->dayofweek);

		fgets(node->op, sizeof(node->op), fp);
		node->op[strlen(node->op) - 1] = '\0';
		tail->next = node;
		node->prev = tail;
		tail = node;
	}

	return 0;
}

/**
  crontab 노드를 리스트에 추가하는 함수
  @param head 원본 리스트
  @param cp 추가할 노드
  @return 성공 시 0, 에러 시 -1 리턴하고 err_str 설정
  */
int add_crontab(crontab *head, crontab *cp) {
	if (head == NULL) {
		sprintf(err_str, "head is NULL\n");
		return -1;
	}

	while (head->next != NULL) {
		head = head->next;
	}
	head->next = cp;
	cp->prev = head;
	return 0;
}

/**
  crontab 리스트를 출력하는 함수
  @param cp 리스트 헤드
  @return 성공 시 리스트 엔트리 갯수, 에러 시 -1
  */
int print_crontab(crontab *cp) {
	int count = 0;
	crontab *tmp;

	if (cp == NULL) {
		sprintf(err_str, "cp is NULL\n");
		return -1;
	}

	tmp = cp->next;
	while (tmp != NULL) {
		printf("%d. %s %s %s %s %s %s\n", count, tmp->min, tmp->hour, tmp->day, tmp->month, tmp->dayofweek, tmp->op);
		tmp = tmp->next;
		count++;
	}
	return count;
}

/**
  crontab 노드를 리스트에서 삭제하는 함수
  @param cp 삭제할 노드 (헤드 노드는 불가)
  @return 성공 시 0 에러 시 -1 리턴하고 err_str 설정
  */
int remove_crontab(crontab *cp) {
	if (cp->prev == NULL) {
		sprintf(err_str, "Cannot remove head node\n");
		return -1;
	}

	cp->prev->next = cp->next;
	if (cp->next != NULL) {
		cp->next->prev = cp->prev;
	}

	cp->prev = NULL;
	cp->next = NULL;
	free(cp);
	return 0;
}

/**
  로그를 남기는 함수
  @param str 로그로 남길 문자열
  @return 성공 시 0 에러 시 -1 리턴하고 err_str 지정
  */
int log_crontab(const char *str) {
	struct tm *tm;
	time_t t;
	FILE *fp;

	if ((fp = fopen(CRONTAB_LOG, "a+")) == NULL) {
		sprintf(err_str, "fopen error for %s\n", CRONTAB_LOG);
		return -1;
	}

	t = time(NULL);
	tm = localtime(&t);
	fprintf(fp, "[%s] %s\n", strtok(asctime(tm), "\n"), str);
	fclose(fp);
	return 0;
}
