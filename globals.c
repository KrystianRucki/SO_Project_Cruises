#include "globals.h"

//semaphores
sem_t* molo_capacity, *ticketq_lock, *cashier_lock;

//mmap
int protection_type = PROT_READ | PROT_WRITE;
int visibility_type = MAP_SHARED | MAP_ANONYMOUS;

//var
int *ticketq_cnt;

int passenger_cashier[2];
int cashier_passenger[2];

int* boat_state1; // stan pierwszej lodzi
int* boat_state2; // stan drugiej lodzi
int* t1; //boat1 cruise time
int* t2; // boat2 cruise time

void init_sem()
{
    molo_capacity = (sem_t*)mmap(NULL, sizeof(sem_t),protection_type,visibility_type,-1,0);
    if(molo_capacity == MAP_FAILED)
    {
        perror("molo_capacity mmap failed");
        exit(1);
    }

    ticketq_lock = (sem_t*)mmap(NULL, sizeof(sem_t),protection_type,visibility_type,-1,0);
    if(ticketq_lock == MAP_FAILED)
    {
        perror("ticketq_lock mmap failed");
        exit(1);
    }

    cashier_lock = (sem_t*)mmap(NULL, sizeof(sem_t),protection_type,visibility_type,-1,0);
    if(cashier_lock == MAP_FAILED)
    {
        perror("cashier_lock mmap failed");
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

    if(sem_init(cashier_lock, 1, 1) == -1)
    {
        perror("cashier_lock sem_init failed");
        exit(2);
    }
}

void destroy_sem()
{
    sem_destroy(ticketq_lock);
    sem_destroy(molo_capacity);
    sem_destroy(cashier_lock);

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

    if(munmap(cashier_lock, sizeof(sem_t)) == -1)
    {
        perror("cashier_lock munmap failed");
        exit(4);
    }
}

void share_var()
{
    ticketq_cnt = (int*)mmap(NULL, sizeof(int),protection_type,visibility_type,-1,0);
    boat_state1 = (int*)mmap(NULL, sizeof(int),protection_type,visibility_type,-1,0);
    boat_state2 = (int*)mmap(NULL, sizeof(int),protection_type,visibility_type,-1,0);
    t1 = (int*)mmap(NULL, sizeof(int),protection_type,visibility_type,-1,0);
    t2 = (int*)mmap(NULL, sizeof(int),protection_type,visibility_type,-1,0);
    
    if(ticketq_cnt == MAP_FAILED || boat_state1 == MAP_FAILED || boat_state2 == MAP_FAILED || t1 == MAP_FAILED || t2 == MAP_FAILED)
    {
        perror("variables mmap failed");
        exit(1);
    }
}

void init_var()
{
    *ticketq_cnt = 0;
    *boat_state1 = 1; //1 - dziala, 0 - stopped
    *boat_state2 = 1; //1 - dziala, 0 - stopped
    *t1 = 15; //init with boat cruise time
    *t2 = 20; //init with boat cruise time

    if(pipe(passenger_cashier) == -1)
    {
        perror("passenger_cashier pipe failed");
        exit(6);
    }

    if(pipe(cashier_passenger) == -1)
    {
        perror("cashier_passenger pipe failed");
        exit(6);
    }
}

void destroy_var()
{
    if (munmap(ticketq_cnt, sizeof(int)) == -1)
    {
        perror("ticketq_cnt munmap failed");
        exit(4);
    }

    if (munmap(boat_state1, sizeof(int)) == -1)
    {
        perror("boat_state1 munmap failed");
        exit(4);
    }

    if (munmap(boat_state2, sizeof(int)) == -1)
    {
        perror("boat_state2 munmap failed");
        exit(4);
    }

    if (munmap(t1, sizeof(int)) == -1)
    {
        perror("t1 munmap failed");
        exit(4);
    }

    if (munmap(t2, sizeof(int)) == -1)
    {
        perror("t2 munmap failed");
        exit(4);
    }

    close(passenger_cashier[1]);
    close(cashier_passenger[1]);
    close(passenger_cashier[0]);
    close(cashier_passenger[0]);
}