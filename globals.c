#include "globals.h"

//semaphores
sem_t* molo_capacity, *ticketq_lock;

//mmap
int protection_type = PROT_READ | PROT_WRITE;
int visibility_type = MAP_SHARED | MAP_ANONYMOUS;

//var
int *ticketq_cnt;

void init_sem()
{
    molo_capacity = (sem_t*)mmap(NULL, sizeof(sem_t),protection_type,visibility_type,-1,0);
    if(molo_capacity == MAP_FAILED)
    {
        perror("molo_capacity mmap failed");
        exit(1);
    }

    ticketq_lock = (sem_t*)mmap(NULL, sizeof(sem_t),protection_type,visibility_type,-1,0);
    if(molo_capacity == MAP_FAILED)
    {
        perror("ticketq_lock mmap failed");
        exit(1);
    }

    if(sem_init(molo_capacity, 1, 24) == -1)
    {
        perror("molo_capacity sem_init failed");
        exit(2);
    }

    if(sem_init(ticketq_lock, 1, 1) == -1)
    {
        perror("ticketq_lock sem_init failed");
        exit(2);
    }
}

void destroy_sem()
{
    sem_destroy(ticketq_lock);
    sem_destroy(molo_capacity);

    if(munmap(ticketq_lock, sizeof(sem_t)) == -1)
    {
        perror("ticketq_lock munmap failed");
        exit(4);
    }

    if(munmap(molo_capacity, sizeof(sem_t)) == -1)
    {
        perror("molo_capacity munmap failed");
        exit(4);
    }
}

void share_var()
{
    ticketq_cnt = (int*)mmap(NULL, sizeof(int),protection_type,visibility_type,-1,0);
    if(ticketq_cnt == MAP_FAILED)
    {
        perror("ticketq_cnt mmap failed");
        exit(1);
    }
}

void init_var()
{
    *ticketq_cnt = 0;
}

void destroy_var()
{
    if(munmap(ticketq_cnt, sizeof(int)) == -1)
    {
        perror("ticketq_cnt munmap failed");
        exit(4);
    }
}