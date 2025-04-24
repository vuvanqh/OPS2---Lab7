#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAXLINE 4096
#define DEFAULT_THREADCOUNT 10
#define DEFAULT_SAMPLESIZE 100

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

typedef unsigned int uint;
typedef struct dog
{
    pthread_t tid;
    int noDogs;
    int noTracks;
    int index;
    int pos;
    int inRace; //if dog is in race
    uint seed;

    int* order;
    int* onTrack; //no dogs stil in race
    int* track;
    int* endRace; //flag
    pthread_mutex_t* mutex;
    pthread_mutex_t* mOnTrack;
    pthread_mutex_t* mPodium;
}dog_t;
typedef struct dog_handler
{
    pthread_t tid;
    int noDogs;

    int*interrupted;
    dog_t* dogs;
    sigset_t* mask;
    pthread_mutex_t* mInterrupted;
}dog_handler_t;

void msleep(int time);
void advance(dog_t* dog, int pos);

void* task1(void*arg)
{
    dog_t* dog = (dog_t*)arg;

    int idx = rand_r(&dog->seed)%dog->noTracks;
    pthread_mutex_lock(&dog->mutex[idx]);
    dog->track[idx]++;
    pthread_mutex_unlock(&dog->mutex[idx]); 
    printf("dog[%d]: track[%d]\n",dog->index,idx);
    return NULL;
}

void* dogLife(void*arg)
{
    dog_t* dog = (dog_t*)arg;

    while(dog->inRace)
    {
        int idx = rand_r(&dog->seed) % 5 + 1;
        int time = rand_r(&dog->seed)%1321 +200;

        msleep(time);
        advance(dog,dog->pos + idx);
    }
    return NULL;
}

void* dogHandler(void* arg)
{
    dog_handler_t* dog = (dog_handler_t*)arg;
    int sig;
    sigwait(dog->mask,&sig);

    pthread_mutex_lock(dog->dogs->mOnTrack);
    if(*dog->dogs->endRace)
    {
        pthread_mutex_unlock(dog->dogs->mOnTrack);
        return NULL;
    }
    pthread_mutex_unlock(dog->dogs->mOnTrack);
    pthread_mutex_lock(dog->mInterrupted);
    *dog->interrupted=1;
    for(int i=0;i<dog->noDogs;i++)
        pthread_cancel(dog->dogs[i].tid);
    pthread_mutex_unlock(dog->mInterrupted);
    return NULL;
}
//n = track len;
//m = no. dogs;
int main(int argc,char**argv)
{
    if(argc!=3) ERR(argv[0]);
    int n = atoi(argv[1]);
    int m = atoi(argv[2]);
    if(!(n>20 || m>2)) ERR(argv[0]);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask,SIGINT);
    pthread_sigmask(SIG_BLOCK,&mask,NULL);

    int* interrupted = calloc(sizeof(int),1);
    int* track = calloc(sizeof(int),n);
    pthread_mutex_t* mutex = calloc(sizeof(pthread_mutex_t),n);
    pthread_mutex_t mOnTrack = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mInterrupted = PTHREAD_MUTEX_INITIALIZER;
    int* onTrack = calloc(sizeof(int),1);
    int* order = calloc(sizeof(int),3);
    int* endRace = calloc(sizeof(int),1);
    *onTrack = m;
    dog_t* dogs = calloc(sizeof(dog_t),m);
    dog_handler_t* dog_handler = calloc(sizeof(dog_t),1);
    dog_handler->dogs = dogs;
    dog_handler->interrupted = interrupted;
    dog_handler->mask = &mask;
    dog_handler->noDogs = m;
    pthread_create(&dog_handler->tid,NULL,dogHandler,dog_handler);
    
    for(int i=0;i<n;i++)
        pthread_mutex_init(&mutex[i],NULL);

    srand(time(NULL));
    for(int i=0;i<m;i++)
    {
        dogs[i].noDogs = m;
        dogs[i].noTracks = n;
        dogs[i].index = i+1;
        dogs[i].pos = 0;
        dogs[i].track = track;
        dogs[i].mutex = mutex;
        dogs[i].mOnTrack = &mOnTrack;
        dogs[i].onTrack = onTrack;
        dogs[i].endRace = endRace;
        dogs[i].order = order;
        dogs[i].inRace = 1;
        dogs[i].seed = rand();

        //pthread_create(&dogs[i].tid,NULL,task1,&dogs[i]);
        pthread_create(&dogs[i].tid,NULL,dogLife,&dogs[i]);
    }
    
    while(1)
    {
        pthread_mutex_lock(&mInterrupted);
        if(*interrupted)
        {
            pthread_mutex_unlock(&mInterrupted);
            printf("interrupted...\n");
            break;
        }
        pthread_mutex_unlock(&mInterrupted);
        pthread_mutex_lock(&mOnTrack);
        if(*endRace)
        {
            pthread_mutex_unlock(&mInterrupted);
            pthread_mutex_unlock(&mOnTrack);
            pthread_kill(dog_handler->tid,SIGINT);
            break;
        }
        pthread_mutex_unlock(&mOnTrack);
        pthread_mutex_unlock(&mInterrupted);
        msleep(1000);
        system("clear");
        for(int i=0;i<n;i++)
        printf("Track[%d]: %d\n",i,track[i]);
        
    }
    
    for(int i=0;i<m;i++)
        pthread_join(dogs[i].tid,NULL);
    
    pthread_join(dog_handler->tid,NULL);

    printf("\nPodium...\n");
    for(int i=0;i<3;i++)
        printf("%d. dog[%d]\n",i+1,order[i]);
    
    for(int i=0;i<n;i++)
        pthread_mutex_destroy(&mutex[i]);

    pthread_sigmask(SIG_UNBLOCK,&mask,NULL);
    pthread_mutex_destroy(&mOnTrack);
    pthread_mutex_destroy(&mInterrupted);
    free(order);
    free(onTrack);
    free(mutex);
    free(track);
    free(dogs);
    free(interrupted);
    free(dog_handler);
    free(endRace);
    return 0;
}

void msleep(int milisec)
{
    time_t sec = milisec / 1000;                    
    long nsec = (milisec % 1000) * 1000000L;  
    // Prepare the timespec structure
    struct timespec req;
    if (nsec >= 1000000000L) {
        sec += 1;
        nsec -= 1000000000L;
    }
    req.tv_sec = sec;
    req.tv_nsec = nsec;
    if (nanosleep(&req, NULL))
        ERR("nanosleep");
}

void advance(dog_t* dog, int pos)
{
    if(pos>=dog->noTracks) pos=dog->noTracks-1;
    pthread_mutex_lock(&dog->mutex[dog->pos]);
    pthread_mutex_lock(&dog->mutex[pos]);

    if(dog->pos!=0) dog->track[dog->pos]--;
    dog->track[pos]++;
    if(dog->track[pos]>1) printf("waf waf waf\n");

    pthread_mutex_unlock(&dog->mutex[dog->pos]);
    pthread_mutex_unlock(&dog->mutex[pos]);
    
    dog->pos = pos;

    //printf("dog[%d] at %d\n",dog->index,dog->pos);
    if(dog->pos == dog->noTracks-1)
    {
        dog->inRace = 0;
        pthread_mutex_lock(dog->mOnTrack);
        printf("dog[%d] has finished the race!!!\n",dog->index);
        *dog->onTrack-=1;
        for(int i=0;i<3;i++)
        {
            if(dog->order[i]==0) 
            { 
                dog->order[i]=dog->index;
                break;
            }
        }
        if(dog->order[2]!=0) *dog->endRace=1;
        pthread_mutex_unlock(dog->mOnTrack);
    }
}
