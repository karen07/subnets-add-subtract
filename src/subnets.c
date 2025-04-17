#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <linux/limits.h>

#define THREAD_COUNT 64

typedef struct node NODE;
struct node {
    NODE *sub[2];
};

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

static NODE *root_g[THREAD_COUNT + 1];
static NODE none_g[THREAD_COUNT + 1];
static NODE all_g[THREAD_COUNT + 1];

static char *ips = NULL;

static int32_t thread_count = 0;

static pthread_barrier_t threads_barrier_start;
static pthread_barrier_t threads_barrier_end;

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
        if (n != none) {
            free_tree(n, none, all);
        }
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
    add_to_node(&root_g[thread_id], &none_g[thread_id], &all_g[thread_id], add, 31, -1);
}

static void save_cidr(int32_t thread_id, unsigned long int add, int pref)
{
    add_to_node(&root_g[thread_id], &none_g[thread_id], &all_g[thread_id],
                pref ? add & 0xffffffff & (0xffffffff << (32 - pref)) : 0, 31, 31 - pref);
}
//Add node

//Dump
static void dump_tree(NODE *n, NODE *none, NODE *all, FILE *res_fd, unsigned long int v, int bit)
{
    if (n == none) {
        return;
    }
    if (n == all) {
        fprintf(res_fd, "%lu.%lu.%lu.%lu/%d\n", v >> 24 & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff,
                v & 0xff, 31 - bit);
        return;
    }
    if (bit < 0) {
        abort();
    }
    dump_tree(n->sub[0], none, all, res_fd, v, bit - 1);
    dump_tree(n->sub[1], none, all, res_fd, v | (1 << bit), bit - 1);
}

static void dump_output(int32_t thread_id, FILE *res_fd_g)
{
    dump_tree(root_g[thread_id], &none_g[thread_id], &all_g[thread_id], res_fd_g, 0, 31);
}
//Dump

static void print_help(void)
{
    printf("Commands:\n"
           "  Required parameters:\n"
           "    -t  \"x\"          Thread count\n"
           "    -a  \"/test.txt\"  Path to the subnets to add\n"
           "    -s  \"/test.txt\"  Path to the subnets to subtract\n");
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

    root_g[thread_id] = &none_g[thread_id];

    //printf("Start %2d %10lu - %10lu\n", thread_id, ((1UL << 32) / thread_count) * thread_id,
    //       ((1UL << 32) / thread_count) * (thread_id + 1));
    //fflush(stdout);

    pthread_barrier_wait(&threads_barrier_start);

    for (uint64_t i = ((1UL << 32) / thread_count) * thread_id;
         i < ((1UL << 32) / thread_count) * (thread_id + 1); i++) {
        //uint64_t offset_i = (i - ((1UL << 32) / thread_count) * thread_id);
        //uint64_t part = ((1UL << 32) / thread_count / 100);
        //if (offset_i % part == 0) {
        //    printf("%d %lu%%\n", thread_id, offset_i / part);
        //}
        if ((ips[i / CHAR_BIT] & ((char)1 << i % CHAR_BIT)) != 0) {
            save_one_addr(thread_id, i);
        }
    }

    pthread_barrier_wait(&threads_barrier_end);

    //printf("End %2d\n", thread_id);
    //fflush(stdout);

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
            if (!strcmp(argv[i], "-a")) {
                if (i != argc - 1) {
                    printf("  Path to the subnets to add  \"%s\"\n", argv[i + 1]);
                    if (strlen(argv[i + 1]) < PATH_MAX - 100) {
                        strcpy(add_subnets_path, argv[i + 1]);
                    }
                    i++;
                }
                continue;
            }
            if (!strcmp(argv[i], "-s")) {
                if (i != argc - 1) {
                    printf("  Path to the subnets to subtract  \"%s\"\n", argv[i + 1]);
                    if (strlen(argv[i + 1]) < PATH_MAX - 100) {
                        strcpy(sub_subnets_path, argv[i + 1]);
                    }
                    i++;
                }
                continue;
            }
            if (!strcmp(argv[i], "-t")) {
                if (i != argc - 1) {
                    printf("  Thread count  \"%s\"\n", argv[i + 1]);
                    sscanf(argv[i + 1], "%d", &thread_count);
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

        if (thread_count == 0 || thread_count > THREAD_COUNT) {
            print_help();
            errmsg("Programm need thread_count form 1 to %d\n", THREAD_COUNT);
        }

        if ((thread_count & (thread_count - 1)) != 0) {
            print_help();
            errmsg("Programm need thread_count pow of 2\n");
        }
    }
    //Args

    //Alloc ips
    {
        ips = (char *)malloc((UINT32_MAX + 1UL) * sizeof(char) / CHAR_BIT);
        if (ips == NULL) {
            errmsg("Not free memory for ips\n");
        }
        memset(ips, 0, (UINT32_MAX + 1UL) * sizeof(char) / CHAR_BIT);
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
                            ips[ip / CHAR_BIT] |= (char)1 << ip % CHAR_BIT;
                            ip++;
                        }
                    }
                    *slash_ptr = '/';
                } else {
                    errmsg("Every subnets line \"x.x.x.x/xx\"\n");
                }
            }

            fclose(add_subnets_fd);

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
                            //if ((ips[ip / CHAR_BIT] & ((char)1 << ip % CHAR_BIT)) == 0) {
                            //    printf("Incorrect intersection of subnets %s/%u\n", tmp_line,
                            //           tmp_prefix);
                            //}
                            ips[ip / CHAR_BIT] &= ~((char)1 << ip % CHAR_BIT);
                            ip++;
                        }
                    }
                    *slash_ptr = '/';
                } else {
                    errmsg("Every subnets line \"x.x.x.x/xx\"\n");
                }
            }

            fclose(sub_subnets_fd);

            printf("Subtract subnets count %d\n", in_subnet_count);
        }
    }
    //Subtract subnets

    pthread_barrier_init(&threads_barrier_start, NULL, thread_count + 1);
    pthread_barrier_init(&threads_barrier_end, NULL, thread_count + 1);

    //Calc result
    {
        for (int32_t i = 0; i < thread_count; i++) {
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

    pthread_barrier_wait(&threads_barrier_start);
    pthread_barrier_wait(&threads_barrier_end);

    fflush(stdout);

    //Dump result
    {
        FILE *res_fd_g;
        res_fd_g = fopen("result.txt", "w");
        if (res_fd_g == NULL) {
            errmsg("Can't open result.txt file\n");
        }

        for (int32_t i = 0; i < thread_count; i++) {
            dump_output(i, res_fd_g);
        }

        fclose(res_fd_g);
    }
    //Dump result

    //Final result
    {
        char tmp_line[100];

        root_g[THREAD_COUNT] = &none_g[THREAD_COUNT];

        FILE *res_fd_g;
        res_fd_g = fopen("result.txt", "r");
        if (res_fd_g == NULL) {
            errmsg("Can't open result.txt file\n");
        }

        while (fscanf(res_fd_g, "%s", tmp_line) != EOF) {
            char *slash_ptr = strchr(tmp_line, '/');
            if (slash_ptr) {
                uint32_t tmp_prefix = 0;
                sscanf(slash_ptr + 1, "%u", &tmp_prefix);
                *slash_ptr = 0;
                if (strlen(tmp_line) < INET_ADDRSTRLEN) {
                    uint32_t ip = ntohl(inet_addr(tmp_line));
                    save_cidr(THREAD_COUNT, ip, tmp_prefix);
                }
                *slash_ptr = '/';
            } else {
                errmsg("Every subnets line \"x.x.x.x/xx\"\n");
            }
        }

        fclose(res_fd_g);

        res_fd_g = fopen("result.txt", "w");
        if (res_fd_g == NULL) {
            errmsg("Can't open result.txt file\n");
        }

        dump_output(THREAD_COUNT, res_fd_g);

        fclose(res_fd_g);
    }
    //Final result

    return EXIT_SUCCESS;
}
