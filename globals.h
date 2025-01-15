#include <semaphore.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#ifndef GLOBALS_H
#define GLOBALS_H

#define MAX_PAS 16
#define Tp 0
#define Tk 9


//entering molo, ticket office queue lock, passenger in queue counter
extern sem_t* molo_capacity;
extern sem_t* ticketq_lock; // zapobiega wyścigom do kasy
extern int* ticketq_cnt;  // Licznik pasażerów w kolejce - zastanowic sie czy wsm jest potrzebny

//communication between passenger and cashier
extern int passenger_cashier[2]; // P => C, 0 - read, 1 - write fd
extern int cashier_passenger[2]; // C => P
extern sem_t* passcash_pipe_lock;  //zapobiega sytuacji gdzie kilku pasazerow rozmawia z kasjerem w tym samym momencie
extern sem_t* read_ready; //delete if not necessary

//boats work times, also determins if given ticket will be provided/sold
extern int* boat_state1; // stan pierwszej lodzi
extern int* boat_state2; // stan drugiej lodzi

extern int* t1; //boat1 cruise time
extern int* t2; // boat2 cruise time

//time manager
extern int * current_time;
extern sem_t* time_lock;

extern int* status; //sprawia, ze nie wchodza na molo, nie ustawiaja sie do kolejki, kasa ze jest zamknieta obsluga ludzi ktorzy zostali po zamknieciu
//w kolejce do kasy, obsluzenie ich signalem albo po kolei write decision do pipe * ilosc osob w kolejce pobierana raz - ilosc iteracji write - raczej nie bedzie potrzebne bo bede sprawzdal status  w passenger cycle

#endif //GLOBALS_H

void init_sem();
void share_var();
void init_var();
void destroy_sem();
void destroy_var();
/*
exit(1); mmap
exit(2); sem_init
exit(3); pipe
exit(4); munmap
exit(6); pipe
*/