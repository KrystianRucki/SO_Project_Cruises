//kasjer.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>

#define MAX_PIDS 6000

//index odpowiada PID pasazera, wartosc 1 lub 0 - Czy dany pasażer już podrozowal (byl juz na statku)
static int has_traveled[MAX_PIDS];

int main(void)
{
    mkfifo("cashier_in_fifo", 0666);
    mkfifo("cashier_out_fifo", 0666);

    int fd_in = open("cashier_in_fifo", O_RDONLY /*| O_NONBLOCK*/);
    int fd_out = open("cashier_out_fifo", O_WRONLY);

    if (fd_in < 0 || fd_out < 0)
    {
        perror("[CASHIER] error while opening cashier FIFO");
        return 1;
    }

    for(int i=0; i < MAX_PIDS; i++){has_traveled[i] = 0;}

    printf("[CASHIER] BEGIN\n");

    setbuf(stdout,NULL);

    char buffer[256]; // bufor na dane od pasazera
    while (1)
    {
        ssize_t n = read(fd_in, buffer, sizeof(buffer)-1); // "-1" - zostawiamy miejsce na (null-terminator)
        if (n<0)
        {
            if (errno == EAGAIN || errno == EINTR)// Brak danych do odczytu (potok jest pusty, proces czeka na dane) || Operacja odczytu zostala przerwana przez signal
            {
                //usleep(100000);
                continue;
            }
            perror("[CASHIER] read");
            break;
        }
        if (n == 0)
        {
           // usleep(100000);
            continue;
        }
        buffer[n]='\0'; //wstawiamy null terminator na koniec

        if (strncmp(buffer,"GET",3)==0)
        {
            int pid, age;
            int ret_elements = sscanf(buffer, "GET %d %d", &pid, &age); //rozpakowanie danych w buforze
            if (ret_elements < 2)
            {
                printf("[CASHIER] Incorrect: %s\n", buffer);
                continue;
            }

            printf("[CASHIER] Passenger %d age = %d\n", pid, age);

            int discount = 0, f_skip = 0; //znizka, flaga pomijania (skip)
            if (pid >= 0 && pid < MAX_PIDS)
            {
                if (!has_traveled[pid])
                {
                    has_traveled[pid] = 1; 
                    if(age<3){discount = 100;} //Dzieci ponizej 3 roku zycia nie placa za bilet
                }
                else
                {
                    //pasazer powraca
                    f_skip = 1;
                    if(age<3){discount=100;}
                    else{discount=50;}
                }
            }

            //Odpowiedz CASHIER => PASSENGER
            dprintf(fd_out, "OK %d DISC=%d SKIP=%d\n", pid, discount, f_skip);
            // dprintf() pozwala na wskazanie konkretnego fd, ktory bedzie odbieral dane

        }
        else if(strncmp(buffer, "QUIT", 4) == 0)
        {
            printf("[CASHIER] QUIT\n");
            break;
        }
        else{printf("[CASHIER] UNKNOWN: %s\n", buffer);}
    }

    close(fd_in);
    close(fd_out);
    printf("[CASHIER] end.\n");
    return 0;
}