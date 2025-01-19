//kasjer.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>

#define MAX_PIDS 6000

//index odpowiada PID pasazera, wartosc 1 lub 0 - Czy dany pasażer już podrozowal (byl juz na statku)
static int has_traveled[MAX_PIDS];

int main(void)
{
    mkfifo("cashier_in_fifo", 0666);
    mkfifo("cashier_out_fifo", 0666);

    int fd_in = open("cashier_in_fifo", O_RDONLY | O_NONBLOCK);
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
                usleep(100000);
                continue;
            }
            perror("[CASHIER] read");
            break;
        }
        if (n==0)
        {
            usleep(100000);
            continue;
        }
        buffer[n]='\0'; //wstawiamy null terminator na koniec

        if (strncmp(buffer,"GET",3)==0)
        {
            int pid, age, group = 0;
            int ret_elements = sscanf(buffer, "GET %d %d %d", &pid, &age, &group); //rozpakowanie danych w buforze
            if (ret_elements < 2)
            {
                printf("[CASHIER] Incorrect: %s\n", buffer);
                continue;
            }
            if (ret_elements < 3){group=0;}

            printf("[CASHIER] Passenger %d age=%d group=%d\n", pid, age, group);

            //Wybor lodzi
            int boat=1;//mozliwa zmiana na to ze zwykli pasazerowie moga na 1 lub 2 - sprawdzenie dopiero po sprawdzeniu obecnych warunkow
            
            //group!=0: boat = 2
            if (group > 0){boat=2;}
            else
            {
                //zwykly "case", sprawdzamy dziecko < 15 => boat=2, wiek > 70 => boat = 2, inne przypadki boat = 1
                if (age<15 || age>70){boat=2;}
            }

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
            dprintf(fd_out, "OK %d BOAT=%d DISC=%d SKIP=%d GROUP=%d\n", pid, boat, discount, f_skip, group);
            // dprintf() pozwala na wskazanie konkretnego fd, ktory bedzie odbieral dane

        }
        else if(strncmp(buffer, "EXIT", 4) == 0)
        {
            printf("[CASHIER] EXIT\n");
            break;
        }
        else{printf("[CASHIER] UNKNOWN: %s\n", buffer);}
    }

    close(fd_in);
    close(fd_out);
    printf("[CASHIER] end.\n");
    return 0;
}