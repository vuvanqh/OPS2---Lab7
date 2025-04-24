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


#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

typedef struct
{
    int winner;
    int alive;
    pthread_rwlock_t rwlock;
    pthread_rwlock_t block;
    pthread_barrier_t barrier;
}data_t;

    
void usage(char* name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "10000 >= n > 0 - number of children\n");
    exit(EXIT_FAILURE);
}



void child_work(int n,int m,int idx, data_t* dt, int*cards,int*choice)
{
    srand(getpid());
    pthread_barrier_wait(&dt->barrier);
    printf("PLAYER[%d] is here\n",getpid());
    pthread_barrier_wait(&dt->barrier);
    for(int i=0;i<m;i++)
    {
        pthread_rwlock_rdlock(&dt->block); //locking barrier - wait for parent to determine whether to apply any changes to the barrier
        int x;
        do{
            x = rand()%m;
        }while(cards[x]==-1);

        choice[idx] = cards[x];
        cards[x]=-1;
        pthread_rwlock_unlock(&dt->block); //all sone now parent can make changes to the barrier
        //printf("%d\n",choice[idx]);
        pthread_barrier_wait(&dt->barrier); //cards picked
        pthread_barrier_wait(&dt->barrier); //wait for winner to be selected
        
        pthread_rwlock_rdlock(&dt->rwlock); //lock to read the data
        //printf("%d\n",dt->winner);
        if(choice[idx]==dt->winner) 
            printf("[%d] ESSA: %d\n",getpid(),dt->winner);
        pthread_rwlock_unlock(&dt->rwlock);

        if(rand()%25==0) 
        {
            pthread_rwlock_wrlock(&dt->rwlock); //locking data 
            dt->alive--;
            pthread_rwlock_unlock(&dt->rwlock); //unlocking data
            pthread_barrier_wait(&dt->barrier); //im aborting && res read
            printf("[%d]: naura\n",idx);
            abort();
        }
        pthread_barrier_wait(&dt->barrier);//im not && res read
    }
}

void select_winner(int n, int m,data_t*dt, int*choice, int*players)
{
    int max=choice[0];
    int num = 0;
    for(int i=1;i<n;i++)
        if(choice[i]>max) max = choice[i];
    
    for(int i=0;i<n;i++)
        if(choice[i]==max) num++;

    for(int i=0;i<n;i++)
        if(choice[i]==max) players[i] += n/num;
    
    pthread_rwlock_wrlock(&dt->rwlock); //lock to overwrite the data
    dt->winner = max;
    pthread_rwlock_unlock(&dt->rwlock);
    msync(choice,sizeof(int)*n,MS_SYNC);
}
void parent_work(int n, int m, data_t* dt,int*choice)
{
    printf("GAME START: waiting...\n");
    pthread_barrier_wait(&dt->barrier);
    pthread_barrier_wait(&dt->barrier);
    int* players = calloc(sizeof(int),n);
    for(int i=0;i<m;i++)
    {
        printf("\nNEW ROUND\n");
        pthread_barrier_wait(&dt->barrier);//wait for cards to be picked 
        
        select_winner(n,m,dt,choice,players);
        pthread_barrier_wait(&dt->barrier); //winner selected

        pthread_rwlock_wrlock(&dt->block); //block the permission to the barrier mod
        pthread_barrier_wait(&dt->barrier); //wait maybe someone aborted && res read
        pthread_barrier_destroy(&dt->barrier);
        pthread_barrierattr_t bttr;
        pthread_barrierattr_init(&bttr);
        pthread_barrierattr_setpshared(&bttr,PTHREAD_PROCESS_SHARED);
        pthread_barrier_init(&dt->barrier,&bttr,dt->alive+1);
        pthread_rwlock_unlock(&dt->block);  //barrier is ready you can proceed

        pthread_barrierattr_destroy(&bttr);
    }

    printf("finito\n");
    for(int i=0;i<n;i++)
    {
        printf("PLAYER[%d]: %d\n",i,players[i]);
    }
    free(players);
}

int get_a_card(int* arr)
{
    srand(getpid());
    int x;
    do{
        x=rand()%52;
    } while(arr[x]==1);

    arr[x]=1;
    return x;
}

void create_children(int n, int m, data_t* dt,int*choice)
{
    int arr[52] = {0};
    for(int i=0;i<n;i++)
    {
        int*cards = calloc(sizeof(int),m);
        for(int l=0;l<m;l++)
            cards[l] = get_a_card(arr);

        switch (fork())
        {   
            case 0:
                child_work(n,m,i,dt,cards,choice);
                free(cards);
                exit(0);
            case -1:
                ERR("fork");
        }
        free(cards);
    }
}

int main(int argc,char**argv)
{
    if(argc!=3) usage(argv[0]);
    int n = atoi(argv[1]);
    int m = atoi(argv[2]);
    int fr,fw;
    if((fr = open("read.txt",O_CREAT | O_RDWR | O_TRUNC,0666))==-1) ERR("open");
    if((fw = open("write.txt",O_CREAT | O_RDWR | O_TRUNC,0666))==-1) ERR("open");
    ftruncate(fr,sizeof(data_t));
    ftruncate(fw,sizeof(int)*n);// PAMIETAC ZAWSZE TRUNCATE
    data_t*dt = (data_t*)mmap(NULL,sizeof(data_t),PROT_READ | PROT_WRITE, MAP_SHARED,fr,0);
    int* choice = (int*)mmap(NULL,sizeof(int)*n,PROT_READ | PROT_WRITE, MAP_SHARED,fw,0);
    close(fr);
    close(fw);

    dt->alive = n;

    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setpshared(&attr,PTHREAD_PROCESS_SHARED);

    pthread_barrierattr_t bttr;
    pthread_barrierattr_init(&bttr);
    pthread_barrierattr_setpshared(&bttr,PTHREAD_PROCESS_SHARED);
    pthread_rwlock_init(&dt->rwlock,&attr);
    pthread_rwlock_init(&dt->block,&attr);
    pthread_barrier_init(&dt->barrier,&bttr,n+1);

    pthread_barrierattr_destroy(&bttr);
    pthread_rwlockattr_destroy(&attr);

    create_children(n,m,dt,choice);
    parent_work(n,m,dt,choice);

    while(wait(NULL)>0);

    msync(dt,sizeof(data_t),MS_SYNC);
    msync(choice,sizeof(int)*n,MS_SYNC);
    pthread_barrier_destroy(&dt->barrier);
    pthread_rwlock_destroy(&dt->rwlock);
    pthread_rwlock_destroy(&dt->block);
    munmap(dt,sizeof(data_t));
    munmap(choice,sizeof(int)*n);
    return 0;
}