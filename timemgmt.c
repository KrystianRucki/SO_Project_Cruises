#include "timemgmt.h"

void create_time()
{
    pid_t pid = fork();
    if(pid == 0)
    {
        start_time_manager();
        exit(0);
    }
}

void wait_time()
{
    if(wait(0) < 0)
    {
        perror(" wait failed");
    }
}

void start_time_manager()
{
    while(*current_time <= Tk) //sprawdz czy dobrze czas czy <> czy ()
    {
        sem_wait(time_lock);
        *current_time +=1;
        printf("global time: %d\n",*current_time);
        sem_post(time_lock);
        sleep(1);
    }
}