#include "passenger.h"

void passenger_cycle()
{
    //wiek <1-80>, "losowy"
    int age = generate_age();
    
}

void enter_molo()
{
    sem_wait(molo_capacity);

    sem_wait(ticketq_lock);
    *ticketq_cnt += 1;
    sem_post(ticketq_lock);

}

void leave_molo()
{
    sem_post(molo_capacity);
    printf("passenger %d left molo\n",getpid());
    exit(0); //tutaj lub pod passenger_cycle
}
void leave_ticketqueue()
{
    sem_wait(ticketq_lock);
    *ticketq_cnt -= 1;
    sem_post(ticketq_lock);
}

void create_passengers()
{
    srand(time(NULL));
    int wait_time;
    pid_t pid;
    for(int i=0;i<MAX_PAS;i++)
    {
        pid = fork();

        if (pid < 0)
        {
            perror("passenger fork failed");
            exit(1);
        }
        else if (pid == 0)
        {
            passenger_cycle();
        }
        wait_time = rand() % 10 + 1;
        sleep(wait_time);
    }
}

void wait_passengers()
{
    for(int i =0;i<MAX_PAS;i++)
    {
        if(wait(0) < 0)
        {
            perror("passenger wait failed");
            //exit(1) -- czy krytyczne dla dzialania programu
        }
    }
}

int generate_random_age()
{
    int age;
    
    if (rand() % 100 < 20) 
    {  
        age = rand() % 30 + 1; // 20% szans na wiek w przedziale 1-30
    }
    else if (rand() % 100 < 70)
    {  
        age = rand() % 20 + 31; // 50% szans na wiek w przedziale 31-50
    }
    else
    {  
        age = rand() % 30 + 51;  // 30% szans na wiek w przedziale 51-80
    }
    
    return age;
}
