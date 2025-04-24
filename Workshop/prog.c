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

#define SHOP_FILENAME "./shop"
#define MIN_SHELVES 8
#define MAX_SHELVES 256
#define MIN_WORKERS 1
#define MAX_WORKERS 64

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

#define SWAP(x, y)         \
    do                     \
    {                      \
        typeof(x) __x = x; \
        typeof(y) __y = y; \
        x = __y;           \
        y = __x;           \
    } while (0)

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n - number of items (shelves), %d <= n <= %d\n", MIN_SHELVES, MAX_SHELVES);
    fprintf(stderr, "\t  m - number of workers, %d <= m <= %d\n", MIN_WORKERS, MAX_WORKERS);
    exit(EXIT_FAILURE);
}

void msleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (nanosleep(&ts, &ts))
        ERR("nanosleep");
}

void shuffle(int* array, int n)
{
    for (int i = n - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        SWAP(array[i], array[j]);
    }
}

void print_array(int* array, int n)
{
    for (int i = 0; i < n; ++i)
    {
        printf("%3d ", array[i]);
    }
    printf("\n");
}

typedef struct 
{
    pthread_mutex_t* mutexes;
    pthread_mutex_t mWork;
    pthread_mutex_t mDead;
    int work;
    int dead;
    int*arr;
}data_t;

void mutex_lock(data_t* dt, int i)
{
    int ret = pthread_mutex_lock(&dt->mutexes[i]);
    if(ret!=0)
    {
        if(ret==EOWNERDEAD)
        {
            printf("[%d] Found a dead body in aisle [%d]\n",getpid(),i);
            pthread_mutex_consistent(&dt->mutexes[i]);
        }
        else ERR("mutex lock");
    }
}

void child_work(int n, int m, int*arr,data_t* dt)
{
    srand(getpid());
    printf("[%d] Worker reports for a night shift\n",getpid());
    while(1)
    {
        pthread_mutex_lock(&dt->mWork);
        if(dt->work==0)
        {
            pthread_mutex_unlock(&dt->mWork);
            break;
        }
        pthread_mutex_unlock(&dt->mWork);
        int x = rand()%n;
        int y;
        do
        {
            y=rand()%n;
        } while (y==x);

        if(x>y)
        {
            int t = x;
            x = y;
            y=t;
        }

        mutex_lock(dt,x);
        mutex_lock(dt,y);
        if(arr[x]>arr[y])
        {
            if (rand() % 100 == 0)
             {
                 printf("[%d] Trips over pallet and dies\n", getpid());
                 pthread_mutex_lock(&dt->mDead);
                dt->dead+=1;
                pthread_mutex_unlock(&dt->mDead);
                 abort();
             }
            int t = arr[x];
            arr[x] = arr[y];
            arr[y] = t;
        }
        pthread_mutex_unlock(&dt->mutexes[x]);
        pthread_mutex_unlock(&dt->mutexes[y]);
        msleep(100);
    }
}

void create_children(int n, int m, int*arr,data_t* dt)
{
    for(int i=0;i<m;i++)
    {
        switch (fork())
        {
        case 0:
            /* code */
            child_work(n,m,arr,dt);
            for(int i=0;i<n;i++)
                pthread_mutex_destroy(&dt->mutexes[i]);
            free(dt->mutexes);
            exit(0);
        case -1:
            ERR("fork");
        }
    }
}

void manager_work(int n, int m, int*arr,data_t* dt)
{
    printf("[%d] Manager reports for a night shift\n",getpid());
    while(1)
    {
        msleep(500);
        for(int i=0;i<n;i++)
            mutex_lock(dt,i);
        
        print_array(arr,n);

        pthread_mutex_lock(&dt->mDead);
        printf("[PID] Workers alive: [%d]\n",m  -dt->dead);
        if(dt->dead==m)
        {
            printf("[%d] All workers died, I hate my job\n",getpid());
            pthread_mutex_unlock(&dt->mDead);
            break;
        }
        pthread_mutex_unlock(&dt->mDead);

        int flag=1;
        for(int i=0;i<n-1;i++)
        {
            if(arr[i]>arr[i+1])
            {
                flag=0;
                break;
            }
        }
        for(int i=0;i<n;i++)
            pthread_mutex_unlock(&dt->mutexes[i]);
        
        if(flag)
        {
            pthread_mutex_lock(&dt->mWork);
            dt->work=0;
            pthread_mutex_unlock(&dt->mWork);
            printf("[%d] The shop shelves are sorted\n",getpid());
            break;
        }
    }
}
/*
8<=N<=256 - #products
1<=M<=64 - #workers
store = 1dim array
*/
int main(int argc, char** argv) 
{
    if(argc!=3) usage(argv[0]);
    int n = atoi(argv[1]);
    int m = atoi(argv[2]);
    if(n<8 || n>256 || m<1 || m>64) usage(argv[0]);

    int fd = open(SHOP_FILENAME,O_CREAT | O_RDWR | O_TRUNC,0666);
    if(fd==-1) ERR("open");
    ftruncate(fd,sizeof(int)*n);
    int*arr = mmap(NULL,sizeof(int)*n,PROT_READ | PROT_WRITE, MAP_SHARED,fd,0);
    if(arr == MAP_FAILED) ERR("map");
    for(int i=0;i<n;i++)
        arr[i] = i+1;
    close(fd);
    data_t* dt = (data_t*)mmap(NULL,sizeof(data_t),PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON,-1,0);
    if(dt == MAP_FAILED) ERR("map");
    dt->mutexes = calloc(sizeof(pthread_mutex_t),n);
    dt->work=1;
    dt->dead = 0;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr,PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr,PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(&dt->mWork,&attr);
    pthread_mutex_init(&dt->mDead,&attr);
    for(int i=0;i<n;i++)
        pthread_mutex_init(&dt->mutexes[i],&attr);
    
    pthread_mutexattr_destroy(&attr);
    shuffle(arr,n);
    print_array(arr,n);

    
    create_children(n,m,arr,dt);
    manager_work(n,m,arr,dt);

    while(wait(NULL)>0);

    printf("Night shift in Bitronka is over\n");
    
    msync(arr,sizeof(int)*n,MS_SYNC);
    for(int i=0;i<n;i++)
        pthread_mutex_destroy(&dt->mutexes[i]);
    pthread_mutex_destroy(&dt->mWork);
    pthread_mutex_destroy(&dt->mDead);
    free(dt->mutexes);
    munmap(arr,sizeof(int)*n);
    munmap(dt,sizeof(data_t));
    return 0;

}