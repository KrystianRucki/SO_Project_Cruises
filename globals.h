#include <semaphore.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef GLOBALS_H
#define GLOBALS_H

//entering molo, ticket office queue lock, passenger in queue counter
extern sem_t* molo_capacity;
extern sem_t* ticketq_lock;
extern int* ticketq_cnt;  // Licznik pasażerów w kolejce
#endif //GLOBALS_H


/*
exit(1); mmap
exit(2); sem_init
exit(3); pipe
exit(4); munmap
*/