#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))
#define SHM_SIZE 1024

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s N\n", name);
    fprintf(stderr, "3 < N <= 30 - board size\n");
    exit(EXIT_FAILURE);
}

typedef struct
{
    int running;
    pthread_mutex_t mutex;
    sigset_t old_mask, new_mask;
} sighandling_args_t;

int main(int argc, char**argv)
{
    if(argc!=2) usage(argv[0]);
    int n=atoi(argv[1]);
    if(n<=0 || n>20) usage(argv[0]);
    pid_t pid = getpid();
    int shm_fd;
    char shm_name[32];
    sprintf(shm_name, "/%d-board", pid);

    if ((shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666)) == -1)
        ERR("shm_open");
    if (ftruncate(shm_fd, SHM_SIZE))
        ERR("ftruncate");

    char* shm_ptr;
    if ((shm_ptr = (char*)mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
        ERR("mmap");
    return 0;
}