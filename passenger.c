#include "passenger.h"

void passenger_cycle()
{
    //generowanie losowo czy bedzie z dzieckiem -> dziecko jako watek, w watku wywolujemy drugiego sem_wait na molo_capacity
    //int passenger_data[3];
    int age = generate_age();
    // int haschild = generate_haschild();
    // int child_age = generate_child_age();
    //przerobic albo na strukture, albo tablice int [age, haschild, child_age]
    
    // if(haschild == 1)
    // {
    //     pthread_t child;

    //     if(pthread_create(&child, NULL, child_function, NULL) != 0)
    //     {
    //         perror("child pthread_create failed");
    //         exit(1);
    //     }
    // }

    if(*status == 0)
    {
        printf("Passenger %d didn't enter molo\n",getpid());
        exit(0);
        // leave_molo();
    }
    else
    {
        enter_molo();
        printf("Passenger %d (age: %d) entered the molo.\n",getpid(), age);
    }

    if(*status == 0)
    {
        leave_molo();
    }
    else
    {
        enter_ticket_queue();printf("Passenger %d entered the queue.\n",getpid());
    }
    
    if(*status == 0)
    {
        leave_ticketqueue();
        leave_molo();
    }
    //jak juz wejdzie to tez zeby sprawdzal czy status zeby nie potrzebnie nie ustawiac sie w kolejce do kasy, rozdzielic enter_molo z enter queue
    //ci co wejda na molo ale nie beda jeszcze w queue to niech leave_molo
    //ci co wejda i ustawia sie do kolejki, beda dostawac po prostu decision 0 po kolei z komunikacji
    //- musze miec tam liczbe osob w kolejce zeby wiedziec ile razy napisac decision do pipe, przekazemy queue ctn do cashier_cycle
    //- albo zamiast tego zrobic signal po prostu zeby sie rozeszli, jak signal to lapie semafor i wychodzi z kolejki i molo (czyli wywola leave_queue())
    
    printf("Passenger %d przed pipe lock\n",getpid());
    //przy kasie, komunikacja z cashier
    sem_wait(passcash_pipe_lock);
    printf("ma pipe lock\n");
    int decision = purchase_process(age); // 0 1 2, nie boat1 boat2
    sem_post(passcash_pipe_lock);
    printf("nie ma pipe lock\n");
    if(decision == 0)
    {
        printf("Passenger %d couldn't buy a ticket. Leaving the queue.\n", getpid());
        leave_ticketqueue();
        leave_molo();
    }
    else
    {
        printf("Passenger %d bought a ticket successfully.\n", getpid());
        //goes to boat1 boat2
        leave_ticketqueue();
    }
    //tu pasazer udaje sie do kolejki do lodzi...
    // if(decision == 1 && *boat_state1 == 1)
    // {
    //     enter_boat1_queue();
    // }
    // else if(decision == 2 && *boat_state2 == 1)
    // {
    //     enter_boat2_queue();
    // }
    // else
    // {
    //     printf("Sorry, your boat is no longer available - please request a refund on our website.\n"); //uzytkownik kupil juz bilet ale jego lodka juz nie plywa
    //     leave_molo();
    // }

    //jak wroci z wycieczki to opuszcza molo albo idzie kupic bilet jeszcze raz z znizka - zalozenie z projektu
    leave_molo();
}

void enter_molo()
{
    sem_wait(molo_capacity);
}

void leave_molo()
{
    sem_post(molo_capacity);
    printf("Passenger %d left molo\n",getpid());
    exit(0); //tutaj lub pod passenger_cycle
}
void enter_ticket_queue()
{
    sem_wait(ticketq_lock);
    *ticketq_cnt += 1;
    sem_post(ticketq_lock);
}
void leave_ticketqueue()
{
    sem_wait(ticketq_lock);
    *ticketq_cnt -= 1;
    sem_post(ticketq_lock);
    printf("Passenger %d left the ticket queue.\n",getpid());
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
            srand(time(NULL) + getpid());
            passenger_cycle();
        }
        wait_time = rand() % 10 + 1;
        sleep(wait_time);
        //sleep(1);
    }
}

void wait_passengers()
{
    for(int i =0;i<MAX_PAS;i++)
    {
        if(wait(0) < 0)
        {
            perror("passenger wait failed");
        }
    }
}

int purchase_process(int age)
{
    int decision;
    close(passenger_cashier[0]);
    close(cashier_passenger[1]);

    printf("entered purchase_process\n");
    if(*status == 0)
    {
        printf("entered status 0 purchase process");
        decision = 0;
        close(passenger_cashier[1]);
        close(cashier_passenger[0]);
        return decision;

    }
    if(write(passenger_cashier[1], &age, sizeof(int)) == -1)
    {
        perror("write to cashier failed");
        close(passenger_cashier[1]);
        close(cashier_passenger[0]);
        exit(1);
    }
    printf("wrote age to cashier\n");
    sem_post(read_ready);

    if(read(cashier_passenger[0], &decision, sizeof(int)) == -1)
    {
        perror("read from cashier failed");
        close(passenger_cashier[1]);
        close(cashier_passenger[0]);
        exit(1);
    }
    printf("read from cashier\n");
    close(passenger_cashier[1]);
    close(cashier_passenger[0]);

    return decision;
}

int generate_age()
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

// void enter_boat1_queue()
// {

// }

// int generate_haschild()
// {
//     int haschild;

//     if(rand() % 100 < 30) //30% szans na posiadanie dziecka
//     {
//         haschild = 1;
//     }
//     else
//     {
//         haschild = 0;
//     }
//     return haschild;
// }

// int generate_child_age()
// {
//     int age;

//     if (rand() % 100 < 70) 
//     {  
//         age = rand() % 3 + 1; // 70% szans na wiek w przedziale 1 - 3
//     }
//     else
//     {  
//         age = rand() % 13 + 4;  // 30% szans na wiek w przedziale 4 - 15
//     }
    
//     return age;
// }
