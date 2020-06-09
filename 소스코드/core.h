#ifndef H_CORE
#define HCORE 1

#define BUF_SIZE 1024
#define SM_BUF_SIZE 64
#define STD_ID "20162489"
#define CRONTAB_FILE "ssu_crontab_file"
#define CRONTAB_LOG "ssu_crontab_log"

#define NUM 0
#define OP 1
#define RANGE 2

typedef struct crontab {
	char min[SM_BUF_SIZE];
	char hour[SM_BUF_SIZE];
	char day[SM_BUF_SIZE];
	char month[SM_BUF_SIZE];
	char dayofweek[SM_BUF_SIZE];
	char op[BUF_SIZE];
	struct crontab *next, *prev;
} crontab;

typedef struct token {
	int type;
	int value;
} token;

char err_str[BUF_SIZE];

int read_crontab_file(crontab *head);
int add_crontab(crontab *head, crontab *cp);
int print_crontab(crontab *cp);
int remove_crontab(crontab *cp);
int is_empty_crontab(crontab *head);
int log_crontab(const char *str);
int parse_execute_term(const char *exterm, int n);
int lock_file(int fd);
int unlock_file(int fd);
static int __expr(int n);

#endif
