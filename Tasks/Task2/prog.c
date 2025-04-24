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
#include <sys/wait.h>
#include <semaphore.h>
#include <stdint.h>
#include <math.h>

#define LEN 128*6

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;

void sethandler( void (*f)(int), int sigNo) {
        struct sigaction act;
        memset(&act, 0, sizeof(struct sigaction));
        act.sa_handler = f;
        if (-1==sigaction(sigNo, &act, NULL)) ERR("sigaction");
}

void sig_handler(int sig) {
        last_signal = sig;
}
// Values of this function are in range (0,1]
double func(double x)
{
    usleep(2000);
    return exp(-x * x);
}
int randomize_points(int N, float a, float b)
{
    int result = 0;
    for (int i = 0; i < N; ++i)
    {
        double rand_x = ((double)rand() / RAND_MAX) * (b - a) + a;
        double rand_y = ((double)rand() / RAND_MAX);
        double real_y = func(rand_x);

        if (rand_y <= real_y)
            result++;
    }
    return result;
}

int random_death_lock(pthread_mutex_t* mtx)
{
    int ret = pthread_mutex_lock(mtx);
    if (ret)
        return ret;

    // 2% chance to die
    if (rand() % 50 == 0)
    {
        printf("[%d] is dying\n",getpid());
        abort();
    }
    return ret;
}

void usage(char* argv[])
{
    printf("%s a b N - calculating integral with multiple processes\n", argv[0]);
    printf("a - Start of segment for integral (default: -1)\n");
    printf("b - End of segment for integral (default: 1)\n");
    printf("N - Size of batch to calculate before reporting to shared memory (default: 1000)\n");
}

double summarize_calculations(uint64_t total_randomized_points, uint64_t hit_points, float a, float b)
{
    return (b - a) * ((double)hit_points / (double)total_randomized_points);
}

typedef struct 
{
    int n;
    double a;
    double b;
    int counter;
    pthread_mutex_t mutex;
    sem_t* sem;
    int hits;
}data_t;

void child_work(data_t* dt)
{
    srand(getpid());

    pthread_mutex_lock(&dt->mutex);
    dt->counter++;
    pthread_mutex_unlock(&dt->mutex);
    int flag=1;
    while(flag)
    {
        if(last_signal==SIGINT)
            break;
        int pts = 10+rand()%10;
        int hits = randomize_points(pts, (double)dt->a, (double)dt->b);
        sem_wait(dt->sem);
        int x = random_death_lock(&dt->mutex);
        if(x!=0)
        {
            if(x==EOWNERDEAD)
            {
                dt->counter--;
                pthread_mutex_consistent(&dt->mutex);
                sem_post(dt->sem);
                if(dt->counter==0)
                {
                    break;
                }
            }
            else ERR("mutex");
        }
        dt->n +=pts;
        dt->hits += hits;
        printf("child[%d | %d] present\n",getpid(),dt->counter);
        printf("[PID %d] Total: %d, Hits: %d\n", getpid(), dt->n, dt->hits);
        pthread_mutex_unlock(&dt->mutex);
        sem_post(dt->sem);
    }
}

void create_children(int n, data_t* dt)
{
    for(int i=0;i<n;i++)
    {
        switch (fork())
        {
        case  0:
            child_work(dt);
            exit(0);       
        case -1:
            ERR("fork");
        }
    }
}

int main(int argc,char**argv)
{
    sem_unlink("sem2");
    if(argc!=4) usage(argv);
    int a = atoi(argv[1]);
    int b = atoi(argv[2]);
    int n = atoi(argv[3]);
    if(n>30 || n<0) usage(argv);

    int shm = shm_open("/task2",O_CREAT | O_RDWR,0666);
    ftruncate(shm,sizeof(data_t));
    data_t* dt = (data_t*)mmap(NULL,sizeof(data_t),PROT_READ | PROT_WRITE,MAP_SHARED,shm,0);
    dt->sem = sem_open("sem2",O_CREAT,0666,5);
    dt->counter=0;
    dt->hits = 0;
    dt->a = a;
    dt->b = b;
    dt->n = 0;
    close(shm);

    sethandler(sig_handler,SIGINT);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&dt->mutex,&attr);
    pthread_mutexattr_destroy(&attr);
    create_children(n,dt);
    while(n>0)
    {
        int sig;
        pid_t pid = waitpid(0,&sig,WNOHANG);
        if(pid>0)
            n--;
        if (WIFSIGNALED(sig))
        {
            int signal = WTERMSIG(sig);
            if(signal==SIGABRT)
            {

                n--;
            }
        }
    }
    sleep(2);

    msync(dt,sizeof(data_t),MS_SYNC);
    printf("res: %f\n",(dt->b-dt->a)*(double)dt->hits/dt->n);
    sem_close(dt->sem);
    pthread_mutex_destroy(&dt->mutex);
    munmap(dt,sizeof(data_t));
    return 0;
}

