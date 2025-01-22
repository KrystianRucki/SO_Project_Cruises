//police.c
// => SIGUSR1 zabrania boat1 dalszego plywania
// => SIGUSR2 zabrania boat2 dalszego plywania

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    if (argc<2)
    {
        fprintf(stderr, "[POLICE] USED: %s [pid_sternika]\n", argv[0]);
        return 1;
    }
    pid_t pid_sternik = atoi(argv[1]);
    printf("\033[1;38;5;9m[POLICE] TARGET: pid sternika = %d\033[0m\n", pid_sternik);

    srand(time(NULL));

    //Wyslij signal SIGUSR1
    printf("\033[1;38;5;9m[POLICE] SIGUSR1 => BOAT1\033[0m\n");
    kill(pid_sternik, SIGUSR1);

    //Wyslij signal SIGUSR2
    printf("\033[1;38;5;9m[POLICE] SIGUSR2 => BOAT2\033[0m\n");
    kill(pid_sternik, SIGUSR2);

    printf("[POLICE] END\n");
    return 0;
}