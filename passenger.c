//passenger.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>


int main(int argc, char *argv[])
{
    setbuf(stdout,NULL);

    if (argc < 4)
    {
        fprintf(stderr, "USED: %s [pid] [age] [group]\n",argv[0]);
        return 1;
    }
    int pid = atoi(argv[1]);
    int age = atoi(argv[2]);
    int grp = atoi(argv[3]);

    int fd_ci = open("cashier_in_fifo", O_WRONLY);
    int fd_co = open("cashier_out_fifo", O_RDONLY);
    if (fd_ci < 0 || fd_co < 0)
    {
        perror("[PASSENGER] error while opening cashier FIFO");
        return 1;
    }

    // Wyslanie zapytania do CASHIER
    char buffer[256];

    snprintf(buffer, sizeof(buffer), "GET %d %d\n", pid, age); //zapytanie umieszczamy w buforze
    write(fd_ci, buffer, strlen(buffer)); //pisanie do FIFO CASHIER'a

    int disc = 0;
    int f_skip = 0;
    int is_ok = 0;

    int r_attempts = 0; //proby odczytu
    while (r_attempts < 70)
    {
        ssize_t n = read(fd_co, buffer, sizeof(buffer)-1);
        if (n > 0)
        {
            buffer[n] = '\0';
            if (strncmp(buffer, "OK", 2) == 0)
            {
                int obtained_pid;
                sscanf(buffer, "OK %d DISC=%d SKIP=%d", &obtained_pid, &disc, &f_skip);
                if (obtained_pid == pid) //jesli odp dotyczy pasazera o odpowiednim pid
                {
                    printf("[PASSENGER %d] OK DISC=%d SKIP=%d\n", pid, disc, f_skip);
                    is_ok=1;
                }
                break;
            }
        }
        r_attempts++;
    }
    close(fd_ci);
    close(fd_co);

    if (!is_ok)
    {
        printf("[PASSENGER %d] CASHIER didn't respond.\n", pid);
        return 1;
    }

    //Czesc dotyczaca sternika
    int fd_sin = open("sternik_in_fifo", O_WRONLY);

    if (fd_sin < 0)
    {
        fprintf(stderr, "[PASSENGER %d] open sternik_in_fifo error\n", pid);
        return 1;
    }

    if (f_skip == 1)
    {
        snprintf(buffer, sizeof(buffer), "SKIP_QUEUE %d %d %d %d\n", pid, age, disc, grp); //flaga skip = 1, kolejka omijajaca
    }
    else
    {
        snprintf(buffer, sizeof(buffer),"QUEUE %d %d %d %d\n", pid, age, disc, grp); //zwykla kolejka
    }

    write(fd_sin, buffer, strlen(buffer)); // pisanie do FIFO sternika
    close(fd_sin);

    return 0;
}