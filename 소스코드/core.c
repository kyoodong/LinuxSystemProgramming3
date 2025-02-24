#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "core.h"

char exterm[BUFSIZ];
char *extermp;
int result = 0;

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
	crontab list 가 비어있는지 확인
	@param head 확인할 리스트 헤드 노드
	@return 비어있으면 1 아니면 0
	*/
int is_empty_crontab(crontab *head) {
	if (head == NULL)
		return 1;
	return head->next == NULL;
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
	fprintf(fp, "[%s] %s", strtok(asctime(tm), "\n"), str);

	fclose(fp);
	return 0;
}

/**
  주기 문자열에서 토큰 분리해주는 함수
  */
static token __get_token() {
	token t;
	char *tmp;

	t.type = OP;
	t.value = 0;
	if (*extermp == '\0') {
		return t;
	}

	while (*extermp == ' ')
		extermp++;

	if (isdigit(*extermp)) {
		tmp = extermp;
		while (isdigit(*tmp))
			t.value = t.value * 10 + *tmp++ - '0';
		t.type = NUM;

		if (t.value < 0 || t.value >= 60) {
			result = -1;
			t.type = OP;
			t.value = 0;
			return t;
		}

		return t;
	}

	if (*extermp == '*')
		t.type = RANGE;
	else 
		t.type = OP;

	t.value = *extermp;
	return t;
}

/**
  다음 토큰으로 넘어가는 함수
  */
static void __next_token() {
	while (*extermp == ' ')
		extermp++;

	if (isdigit(*extermp)) {
		while (isdigit(*extermp))
			extermp++;
		return;
	}
	extermp++;
}

/**
  , 파싱 함수
  */
static token __comma(int n) {
	token t, t2;
	if (result)
		return t;

	t = __get_token();
	if (t.type == OP && t.value == 0) {
		result = -1;
		t.type = OP;
		t.value = 0;
		return t;
	}

	__next_token();
	t2 = __get_token();
	if (t2.type == OP && t2.value == ',') {
		__next_token();
		__expr(n);
		return t;
	}
	return t;
}

/**
  - 파싱 함수
  */
static token __minus(int n, int table[60]) {
	token t, t2, t3;

	t = __comma(n);
	if (result)
		return t;

	if (t.type == OP && t.value == ',') {
		result = -1;
		t.type = OP;
		t.value = 0;
		return t;
	}

	t2 = __get_token();
	if (t2.type == OP && t2.value == '-') {
		// - 앞에는 무조건 숫자가 와야함
		if (t.type != NUM) {
			result = -1;
			t.type = OP;
			t.value = 0;
			return t;
		}

		__next_token();
		t3 = __comma(n);

		// - 다음에는 숫자가 와야함
		// 연산자가 오면 잘못된 수식
		if (t3.type != NUM) {
			result = -1;
			t.type = OP;
			t.value = 0;
			return t;
		}

		if (t.value > t3.value) {
			printf("- 연산자는 오른쪽의 수가 더 커야합니다.\n");
			t.type = OP;
			t.value = 0;
			result = -1;
			return t;
		}

		for (int i = t.value; i <= t3.value; i++)
			table[i] = 1;

		t3.type = RANGE;
		return t3;
	}
	
	return t;
}

/**
  / 파싱 함수
  */
static token __slash(int n, int table[60]) {
	token t, t2, t3;

	t = __minus(n, table);
	if (result) {
		t.type = OP;
		t.value = 0;
		return t;
	}

	//if (t.type == OP && t.value == '-') {
	if (t.type == OP) {
		t.type = OP;
		t.value = 0;
		result = -1;
		return t;
	}

	if (t.type == RANGE && t.value == '*') {
		for (int i = 0; i < 60; i++) {
			table[i] = 1;
		}
	}

	t2 = __get_token();
	if (t2.type == OP && t2.value == '/') {
		// /(슬래쉬) 앞에 *이나 범위가 아닌 숫자나 연산자가 오면 에러
		if (t.type != RANGE) {
			result = -1;
			t.type = OP;
			t.value = 0;
			return t;
		}

		__next_token();
		t3 = __minus(n, table);

		// 슬래쉬 다음에 숫자가 와야함
		// 연산자가 오면 잘못된 수식
		if (t3.type != NUM) {
			result = -1;
			t.type = OP;
			t.value = 0;
			return t;
		}

		int count = t3.value - 1;
		for (int i = 0; i < 60; i++) {
			if (table[i]) {
				if (count <= 0) {
					count = t3.value - 1;
				} else {
					table[i] = 0;
					count--;
				}
			}
		}
		return t3;
	}

	table[t.value] = 1;
	return t;
}

/**
  파싱 스타트 함수
  */
static int __expr(int n) {
	token t;
	int table[60];

	memset(table, 0, sizeof(table));
	t = __slash(n, table);

	if (result)
		return result;

	if (t.type == OP && t.value == '/') {
		return -1;
	}

	/*
	for (int i = 0; i < 60; i++) {
		if (table[i])
			printf("%d ", i);
	}
	printf("\n");
	*/

	if (n >= 0 && table[n] > 0) {
		result = 1;
		return 1;
	}

	if (strlen(extermp) > 0)
		return -1;
	return 0;
}

/**
  외부에서 파싱 요청을 하는 함수
  @param str 주기 문자열
  @param n n이 해당 주기문자열에서 실행가능한지 확인
  @return n 이 해당 주기 문자열에서 실행가능하면 1 아니면 0 에러 시 -1 리턴
  예를 들어 1-10/2 은 2, 4, 6, 8, 10 은 실행 가능하므로 n이 2면 1 리턴
  */
int parse_execute_term(const char *str, int n) {
	memset(exterm, 0, sizeof(exterm));
	strcpy(exterm, str);
	result = 0;
	extermp = exterm;
	int ret = __expr(n);
	return ret;
}
