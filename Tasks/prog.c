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

#define LEN 128*6

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

    
void usage(char* name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "10000 >= n > 0 - number of children\n");
    exit(EXIT_FAILURE);
}

typedef struct 
{
    pthread_mutex_t mutexes[26];
    int arr[26];
}data_t;

void random_file(int fd)
{
    srand(getpid());
    int i;
    for(i=0;i<128*6;i++)
    {
        char b = 'a' + rand()%26;
        write(fd,&b,1);
        //printf("%c,\n",b);
    }
    char b = '\0';
    write(fd,&b,1);
}

int count(char*file,int* arr)
{
    int i=0;
    while(file[i]!='\0')
    {
        arr[file[i++] -'a']++;
    }
}

void child_work(int offset,int len,char*file,data_t* data)
{
    srand(getpid());
    if(rand()%100<3) abort();
    for(int i=0;i<len;i++)
    {
        char x = file[offset+i]-'a';
        int err = pthread_mutex_lock(&data->mutexes[x]);
        if (err != 0)
        {
            if (err == EOWNERDEAD)
                pthread_mutex_consistent(&data->mutexes[x]);
            else
                ERR("pthread_mutex_lock");
        }
        data->arr[x]++;
        pthread_mutex_unlock(&data->mutexes[x]);
    }
}

void create_children(int n, data_t* arr)
{
    for(int i=0;i<n;i++)
    {
        switch (fork())
        {
        case 0:
            /* code */
            int fd = open("dummy.txt", O_RDONLY);
            char* file = mmap(NULL,128*6,PROT_READ, MAP_SHARED, fd,0);
            close(fd);
            if(file==MAP_FAILED) ERR("mmap");
            int seg = LEN/n;
            int len=seg;
            if(i==n-1) len = strlen(file)-(seg*i);
            child_work(seg*i, len,file,arr);
            munmap(file,128*6);
            exit(0);
        case -1: 
            ERR("fork");
        }
    }
}

int main(int argc, char** argv)
{
    if(argc!=2) usage(argv[0]);
    int n = atoi(argv[1]);
    if(n<0 || n>40) usage(argv[0]);

    int fd = open("dummy.txt",O_CREAT | O_RDWR | O_TRUNC, 0666);
    ftruncate(fd,128*6);
    random_file(fd);
    close(fd);
    pthread_barrierattr_t battr;
    pthread_barrierattr_setpshared(&battr,PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    data_t* dt = (data_t*)mmap(NULL,sizeof(data_t),PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1,0);
    if(dt == MAP_FAILED) ERR("mmap");

    for(int i=0;i<26;i++)
    {
        dt->arr[i]=0;
        pthread_mutex_init(&dt->mutexes[i], &attr);
    }
    create_children(n,dt);

    int sig;
    int flag=1;
    while(n>0)
    {
        pid_t pid = waitpid(0,&sig,WNOHANG);
        if(pid>0)
            n--;
        if (WIFSIGNALED(sig))
        {
            int signal = WTERMSIG(sig);
            if(signal==SIGABRT)
            {
                flag=0;
                break;
            }
        }
    }

    if(flag)
    {
    int sum=0;
    for(int i='a';i<='z';i++)
    {
        printf("%c: %d\n",i,dt->arr[i-'a']);
        sum+=dt->arr[i-'a'];
    }
    printf("%d\n",sum);
    }
    else printf("Computations failed :C\n");

    for(int i=0;i<26;i++)
        pthread_mutex_destroy(&dt->mutexes[i]);

    munmap(dt,sizeof(data_t));
    unlink("dummy.txt");
    return 0;
}