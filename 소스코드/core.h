#ifndef H_CORE
#define HCORE 1

#define BUF_SIZE 512
#define SM_BUF_SIZE 64
#define STD_ID "20162489"
#define CRONTAB_FILE "ssu_crontab_file"

typedef struct crontab {
	char min[SM_BUF_SIZE];
	char hour[SM_BUF_SIZE];
	char day[SM_BUF_SIZE];
	char month[SM_BUF_SIZE];
	char dayofweek[SM_BUF_SIZE];
	char op[BUF_SIZE];
	struct crontab *next, *prev;
} crontab;

char err_str[BUF_SIZE];

int read_crontab_file();
int add_crontab(crontab *head, crontab *cp);
int print_crontab(crontab *cp);

#endif
