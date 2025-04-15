#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <arpa/inet.h>
#include <linux/limits.h>

void errmsg(const char *format, ...)
{
    va_list args;

    printf("Error: ");

    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    fflush(stdout);

    exit(EXIT_FAILURE);
}

typedef struct node NODE;
struct node {
    NODE *sub[2];
};
static NODE *root;
static NODE none;
#define NONE (&none)
static NODE all;
#define ALL (&all)

FILE *res_fd;

static void free_tree(NODE *n)
{
    if ((n == NONE) || (n == ALL)) {
        return;
    }
    free_tree(n->sub[0]);
    free_tree(n->sub[1]);
    free(n);
}

static void add_to_node(NODE **np, unsigned long int a, int bit, int end)
{
    NODE *n;

    n = *np;
    if (n == ALL) {
        return;
    }
    if (bit <= end) {
        if (n != NONE)
            free_tree(n);
        *np = ALL;
        return;
    }
    if (n == NONE) {
        n = malloc(sizeof(NODE));
        n->sub[0] = NONE;
        n->sub[1] = NONE;
        *np = n;
    }
    add_to_node(&n->sub[(a >> bit) & 1], a, bit - 1, end);
    if ((n->sub[0] == ALL) && (n->sub[1] == ALL)) {
        free(n);
        *np = ALL;
    }
}

static void dump_tree(NODE *n, unsigned long int v, int bit)
{
    if (n == NONE) {
        return;
    }
    if (n == ALL) {
        fprintf(res_fd, "%lu.%lu.%lu.%lu/%d\n", v >> 24 & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff,
                v & 0xff, 31 - bit);
        return;
    }
    if (bit < 0) {
        abort();
    }
    dump_tree(n->sub[0], v, bit - 1);
    dump_tree(n->sub[1], v | (1 << bit), bit - 1);
}

static void save_one_addr(unsigned long int a)
{
    add_to_node(&root, a, 31, -1);
}

static void dump_output(void)
{
    dump_tree(root, 0, 31);
}

void print_help(void)
{
    printf("Commands:\n"
           "  Required parameters:\n"
           "    +  \"/test.txt\"  Path to the subnets to add\n"
           "    -  \"/test.txt\"  Path to the subnets to subtract\n");
}

static void main_catch_function(int32_t signo)
{
    if (signo == SIGINT) {
        errmsg("SIGINT catched main\n");
    } else if (signo == SIGSEGV) {
        errmsg("SIGSEGV catched main\n");
    } else if (signo == SIGTERM) {
        errmsg("SIGTERM catched main\n");
    }
}

int32_t main(int32_t argc, char *argv[])
{
    printf("Subnets calc started\n\n");

    if (signal(SIGINT, main_catch_function) == SIG_ERR) {
        errmsg("Can't set SIGINT signal handler main\n");
    }

    if (signal(SIGSEGV, main_catch_function) == SIG_ERR) {
        errmsg("Can't set SIGSEGV signal handler main\n");
    }

    if (signal(SIGTERM, main_catch_function) == SIG_ERR) {
        errmsg("Can't set SIGTERM signal handler main\n");
    }

    char add_subnets_path[PATH_MAX];
    memset(add_subnets_path, 0, PATH_MAX);

    char sub_subnets_path[PATH_MAX];
    memset(sub_subnets_path, 0, PATH_MAX);

    //Args
    {
        printf("Launch parameters:\n");
        for (int32_t i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "+")) {
                if (i != argc - 1) {
                    printf("  Path to the subnets to add  \"%s\"\n", argv[i + 1]);
                    if (strlen(argv[i + 1]) < PATH_MAX - 100) {
                        strcpy(add_subnets_path, argv[i + 1]);
                    }
                    i++;
                }
                continue;
            }
            if (!strcmp(argv[i], "-")) {
                if (i != argc - 1) {
                    printf("  Path to the subnets to subtract  \"%s\"\n", argv[i + 1]);
                    if (strlen(argv[i + 1]) < PATH_MAX - 100) {
                        strcpy(sub_subnets_path, argv[i + 1]);
                    }
                    i++;
                }
                continue;
            }
            print_help();
            errmsg("Unknown command %s\n", argv[i]);
        }

        if (add_subnets_path[0] == 0) {
            print_help();
            errmsg("Programm need path to the subnets to add\n");
        }
    }
    //Args

    char *ips = NULL;
    //Alloc ips
    {
        ips = (char *)malloc(UINT32_MAX * sizeof(char));
        if (ips == NULL) {
            errmsg("Not free memory for ips\n");
        }
        memset(ips, 0, UINT32_MAX * sizeof(char));
    }
    //Alloc ips

    //Add subnets
    {
        if (add_subnets_path[0] != 0) {
            FILE *add_subnets_fd;
            add_subnets_fd = fopen(add_subnets_path, "r");
            if (add_subnets_fd == NULL) {
                errmsg("Can't open Add subnets file %s\n", add_subnets_path);
            }

            char tmp_line[100];

            int32_t in_subnet_count = 0;

            while (fscanf(add_subnets_fd, "%s", tmp_line) != EOF) {
                char *slash_ptr = strchr(tmp_line, '/');
                if (slash_ptr) {
                    in_subnet_count++;
                    uint32_t tmp_prefix = 0;
                    sscanf(slash_ptr + 1, "%u", &tmp_prefix);
                    *slash_ptr = 0;
                    if (strlen(tmp_line) < INET_ADDRSTRLEN) {
                        uint32_t ip = ntohl(inet_addr(tmp_line));
                        uint32_t mask = 1;
                        uint32_t subnet_size = 1;
                        if (tmp_prefix != 0) {
                            subnet_size <<= 32 - tmp_prefix;
                            mask = (0xFFFFFFFF << (32 - tmp_prefix)) & 0xFFFFFFFF;
                        } else {
                            subnet_size = UINT32_MAX;
                            mask = 0;
                        }
                        ip &= mask;
                        for (uint32_t i = 0; i < subnet_size; i++) {
                            ips[ip] = 1;
                            ip++;
                        }
                    }
                    *slash_ptr = '/';
                } else {
                    errmsg("Every subnets line \"x.x.x.x/xx\"\n");
                }
            }

            printf("Add subnets count %d\n", in_subnet_count);
        }
    }
    //Add subnets

    //Sub subnets
    {
        if (sub_subnets_path[0] != 0) {
            FILE *sub_subnets_fd;
            sub_subnets_fd = fopen(sub_subnets_path, "r");
            if (sub_subnets_fd == NULL) {
                errmsg("Can't open Subtract subnets file %s\n", sub_subnets_path);
            }

            char tmp_line[100];

            int32_t in_subnet_count = 0;

            while (fscanf(sub_subnets_fd, "%s", tmp_line) != EOF) {
                char *slash_ptr = strchr(tmp_line, '/');
                if (slash_ptr) {
                    in_subnet_count++;
                    uint32_t tmp_prefix = 0;
                    sscanf(slash_ptr + 1, "%u", &tmp_prefix);
                    *slash_ptr = 0;
                    if (strlen(tmp_line) < INET_ADDRSTRLEN) {
                        uint32_t ip = ntohl(inet_addr(tmp_line));
                        uint32_t mask = 1;
                        uint32_t subnet_size = 1;
                        if (tmp_prefix != 0) {
                            subnet_size <<= 32 - tmp_prefix;
                            mask = (0xFFFFFFFF << (32 - tmp_prefix)) & 0xFFFFFFFF;
                        } else {
                            subnet_size = UINT32_MAX;
                            mask = 0;
                        }
                        ip &= mask;
                        for (uint32_t i = 0; i < subnet_size; i++) {
                            if (ips[ip] == 0) {
                                printf("Incorrect intersection of subnets %s/%u\n", tmp_line,
                                       tmp_prefix);
                            }
                            ips[ip] = 0;
                            ip++;
                        }
                    }
                    *slash_ptr = '/';
                } else {
                    errmsg("Every subnets line \"x.x.x.x/xx\"\n");
                }
            }

            printf("Subtract subnets count %d\n", in_subnet_count);
        }
    }
    //Sub subnets

    root = NONE;

    //Calc result
    {
        res_fd = fopen("result.txt", "w");
        if (res_fd == NULL) {
            errmsg("Can't open result file\n");
        }

        uint32_t res_count = 0;

        for (uint32_t i = 0; i < UINT32_MAX; i++) {
            if (ips[i] == 1) {
                save_one_addr(i);
                res_count++;
            }
        }

        printf("Result subnets count %u\n", res_count);
    }
    //Calc result

    dump_output();

    return EXIT_SUCCESS;
}
