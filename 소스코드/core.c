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
		return t;
	}

	t.type = OP;
	t.value = *extermp;
	return t;
}

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
		return __get_token();
	}
	return t;
}

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
		if (t.type == OP) {
			result = -1;
			t.type = OP;
			t.value = 0;
			return t;
		}

		__next_token();
		t3 = __comma(n);

		// - 다음에는 숫자가 와야함
		// 연산자가 오면 잘못된 수식
		if (t3.type == OP) {
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
		return t3;
	}
	
	return t;
}

static token __slash(int n, int table[60]) {
	token t, t2, t3;

	t = __minus(n, table);
	if (result) {
		t.type = OP;
		t.value = 0;
		return t;
	}

	if (t.type == OP && t.value == '-') {
		t.type = OP;
		t.value = 0;
		result = -1;
		return t;
	}

	if (t.type == OP && t.value == '*') {
		for (int i = 0; i < 60; i++) {
			table[i] = 1;
		}
	}

	t2 = __get_token();
	if (t2.type == OP && t2.value == '/') {
		// /(슬래쉬) 앞에 *이나 숫자가 아닌 연산자가 오면 에러
		if (!(t.type == OP && t.value == '*') && !(t.type == NUM)) {
			result = -1;
			t.type = OP;
			t.value = 0;
			return t;
		}

		__next_token();
		t3 = __minus(n, table);

		// 슬래쉬 다음에 숫자가 와야함
		// 연산자가 오면 잘못된 수식
		if (t3.type == OP) {
			result = -1;
			t.type = OP;
			t.value = 0;
			return t;
		}

		for (int i = 0; i < 60; i++) {
			if (i % t3.value == 0 && table[i]) {
				table[i] = 0;
			}
		}
		return t3;
	}
	return t;
}

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

	if (strlen(extermp) > 0)
		return -1;
	return table[n];
}

// 1-15/2,16-30/3
int parse_execute_term(const char *str, int n) {
	memset(exterm, 0, sizeof(exterm));
	strcpy(exterm, str);
	result = 0;
	extermp = exterm;
	int ret = __expr(n);
	printf("%d\n", ret);
	return ret;
}
