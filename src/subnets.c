#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <linux/limits.h>

#define THREAD_COUNT 1

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

static NODE *root[THREAD_COUNT];
static NODE none[THREAD_COUNT];
static NODE all[THREAD_COUNT];
FILE *res_fd[THREAD_COUNT];

char *ips = NULL;

pthread_barrier_t threads_barrier;

//Add node
static void free_tree(NODE *n, NODE *none, NODE *all)
{
    if ((n == none) || (n == all)) {
        return;
    }
    free_tree(n->sub[0], none, all);
    free_tree(n->sub[1], none, all);
    free(n);
}

static void add_to_node(NODE **np, NODE *none, NODE *all, unsigned long int a, int bit, int end)
{
    NODE *n;

    n = *np;
    if (n == all) {
        return;
    }
    if (bit <= end) {
        if (n != none)
            free_tree(n, none, all);
        *np = all;
        return;
    }
    if (n == none) {
        n = malloc(sizeof(NODE));
        n->sub[0] = none;
        n->sub[1] = none;
        *np = n;
    }
    add_to_node(&n->sub[(a >> bit) & 1], none, all, a, bit - 1, end);
    if ((n->sub[0] == all) && (n->sub[1] == all)) {
        free(n);
        *np = all;
    }
}

static void save_one_addr(int32_t thread_id, unsigned long int add)
{
    add_to_node(&root[thread_id], &none[thread_id], &all[thread_id], add, 31, -1);
}
//Add node

//Dump
static void dump_tree(NODE *n, NODE *none, NODE *all, FILE *res_fd_l, unsigned long int v, int bit)
{
    if (n == none) {
        return;
    }
    if (n == all) {
        fprintf(res_fd_l, "%lu.%lu.%lu.%lu/%d\n", v >> 24 & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff,
                v & 0xff, 31 - bit);
        return;
    }
    if (bit < 0) {
        abort();
    }
    dump_tree(n->sub[0], none, all, res_fd_l, v, bit - 1);
    dump_tree(n->sub[1], none, all, res_fd_l, v | (1 << bit), bit - 1);
}

static void dump_output(int32_t thread_id)
{
    dump_tree(root[thread_id], &none[thread_id], &all[thread_id], res_fd[thread_id], 0, 31);
}
//Dump

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

void *process_thread_func(void *arg)
{
    int32_t thread_id;
    thread_id = (int64_t)arg;

    root[thread_id] = &none[thread_id];

    printf("Start %d %lu-%lu\n", thread_id, ((1UL << 32) / THREAD_COUNT) * thread_id,
           ((1UL << 32) / THREAD_COUNT) * (thread_id + 1));

    for (uint64_t i = ((1UL << 32) / THREAD_COUNT) * thread_id;
         i < ((1UL << 32) / THREAD_COUNT) * (thread_id + 1); i++) {
        if (i % ((1UL << 32) / THREAD_COUNT / 100) == 0) {
            printf("%d %lu%%\n", thread_id, i / ((1UL << 32) / THREAD_COUNT / 100));
        }
        if (ips[i] == 1) {
            save_one_addr(thread_id, i);
        }
    }

    pthread_barrier_wait(&threads_barrier);

    printf("End %d\n", thread_id);

    return NULL;
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

    //Alloc ips
    {
        ips = (char *)malloc((UINT32_MAX + 1UL) * sizeof(char));
        if (ips == NULL) {
            errmsg("Not free memory for ips\n");
        }
        memset(ips, 0, (UINT32_MAX + 1UL) * sizeof(char));
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
                        uint64_t subnet_size = 1UL << (32 - tmp_prefix);
                        uint32_t mask = 0xFFFFFFFFFFFFFFFF << (32 - tmp_prefix);
                        ip &= mask;
                        for (uint64_t i = 0; i < subnet_size; i++) {
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

    //Subtract subnets
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
                        uint64_t subnet_size = 1UL << (32 - tmp_prefix);
                        uint32_t mask = 0xFFFFFFFFFFFFFFFF << (32 - tmp_prefix);
                        ip &= mask;
                        for (uint64_t i = 0; i < subnet_size; i++) {
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
    //Subtract subnets

    pthread_barrier_init(&threads_barrier, NULL, THREAD_COUNT + 1);

    //Calc result
    {
        for (int32_t i = 0; i < THREAD_COUNT; i++) {
            pthread_t thread;
            void *set_arg;
            set_arg = (void *)((int64_t)i);
            if (pthread_create(&thread, NULL, process_thread_func, set_arg)) {
                errmsg("Can't create process_thread %d\n", i);
            }
            if (pthread_detach(thread)) {
                errmsg("Can't detach process_thread %d\n", i);
            }
        }
    }
    //Calc result

    pthread_barrier_wait(&threads_barrier);
    fflush(stdout);

    //Dump result
    {
        for (int32_t i = 0; i < THREAD_COUNT; i++) {
            char tmp_file_name[PATH_MAX];
            sprintf(tmp_file_name, "result_%d.txt", i);

            res_fd[i] = fopen(tmp_file_name, "w");
            if (res_fd[i] == NULL) {
                errmsg("Can't open %s file\n", tmp_file_name);
            }

            dump_output(i);
        }
    }
    //Dump result

    return EXIT_SUCCESS;
}
