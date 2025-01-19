/*
scheduler.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/select.h>

// --- Sciezki do plikow ---
#define PATH_CASHIER   "./cashier"
#define PATH_PASSENGER "./passenger"

// --- FIFO (named) ---
#define FIFO_CASHIER_IN  "cashier_in_fifo"

// --- Maksymalna ilosc pasazerow ---
#define MAX_PASS 120

// --- Czas symulacji ustalany przez uzytkownika ---
static int TIMEOUT;

// --- Flaga zakonczenia symulacji ---
static volatile sig_atomic_t terminate_flag = 0;

// --- Zmienne dla generowania pasazerow ---
static int min_batch = 2;  //min liczba pasazerow w jednym cyklu generowania - normalnym
static int max_batch = 6;  //max liczba pasazerow w jednym cyklu generowania - normalnym

static int total_gen_pass = 0; //liczba wszystkich wynerowanych pasazerow

// --- PIDy procesow ---
static pid_t pid_cashier = 0;
static pid_t pid_pass[MAX_PASS]; // dla pasazerow
static int passenger_count = 0; //ilosc pasazerow

// --- Generator ---
static pthread_t generator_thread; //watek generatora
static pthread_t timeout_killer_thread; //watek konczacy symulacje po TIMEOUT
static volatile int gen_running_flag = 1; //flaga, ktora steruje jego praca

// --- Funkcje ---
void end_sim();

static pid_t do_child_job(const char *command, char *const argv[]);

static pid_t do_child_job(const char *command, char *const argv[])
{
    pid_t pc = fork();
    if (pc < 0)
    {
        perror("[SCHEDULER] fork error");
        return -1;
    }
    if (pc == 0)
    {
        execv(command, argv); //plik do uruchomienia + argumenty
        perror("[SCHEDULER] execv error");
        _exit(1); //natychmiastowe zakonczenie procesu
    }
    return pc;
}

//tworzenie pasazera z pid age i group
static void do_passenger_job(int pid, int age, int group)
{
    if (passenger_count >= MAX_PASS)
    {
        printf("[SCHEDULER] Reached MAX_PASS.\n");
        return;
    }

    char arg1[32], arg2[32], arg3[32];
    sprintf(arg1, "%d", pid);
    sprintf(arg2, "%d", age);
    sprintf(arg3, "%d", group);

    char *arguments[] = {(char*)PATH_PASSENGER, arg1, arg2, arg3, NULL};

    pid_t pc = fork();
    if (pc == 0)
    {
        //child process => tworzy pasazera
        execv(PATH_PASSENGER, arguments);
        perror("[SCHEDULER] execv passenger error");
        _exit(1); //natychmiastowe zakonczenie procesu
    }
    else if (pc > 0)
    {
        pid_pass[passenger_count++] = pc;
        printf("[SCHEDULER] Passenger pid = %d age = %d group = %d => processPID = %d.\n", pid, age, group, pc); //pid z generatora, processPID - rzeczywisty pid procesu z forka
        total_gen_pass++;
        usleep(200000);
    }
    else
    {
        perror("[SCHEDULER] fork passenger error");
    }
}


// --- Funkcja dla generatora pasazerow (generator_thread) ---
void *generator_function(void *arg)
{
    srand(time(NULL) + getpid());
    //obecny ulimit -u = 15362
    static int used_pids[6000]; //kontrola unikalnych PIDow, mechanizm do losowania powracajacych pasazerow
    static int used_count = 0;
    int origin_pid = 1000; // pierwszy/bazowy pid

    while (!terminate_flag && gen_running_flag)
    {
        int option = rand() % 3;
        if (option == 0)
        {
            //Option 0: dziecko z rodzicem
            int grp = origin_pid + used_count;

            // child
            int child_pid = grp;
            used_pids[used_count++] = child_pid;

            int child_age = rand() % 14 + 1; // wiek 1 - 14
            do_passenger_job(child_pid, child_age, grp);

            // parent
            int parent_pid = grp + 1000;
            int parent_age = rand() % 50 + 20; // wiek 20 - 69
            do_passenger_job(parent_pid, parent_age, grp);

        }
        else if (used_count > 0 && option == 1)
        {
            //Option 1: powrocil stary PID (cashier - skip=1)
            int id_x = rand() % used_count; //losowy passenger z juz "uzytych"
            int old_pid = used_pids[id_x];
            int age = rand() % 80 + 1;
            printf("[GENERATOR] Returning old_pid = %d with age = %d\n", old_pid, age);
            do_passenger_job(old_pid, age, 0);

        }
        else
        {
            //Opcja "normalna" - generujemy porcjami (min_batch - max_batch) pasazerow na raz

            int batch_size = rand() % (max_batch - min_batch + 1) + min_batch;

            for (int i = 0; i < batch_size; i++)
            {
                int age = rand() % 80 + 1; //wiek 1 - 80

                //Jesli wylosuje się dziecko (<15), to generujemy pare dziecko-rodzic (zeby dziecko nie zostalo bez opiekuna)
                if (age < 15) 
                {
                    //generujemy parę dziecko-rodzic w jednej grupie
                    int grp = origin_pid + used_count; //new group
                    
                    //child
                    int child_pid = grp;
                    used_pids[used_count++] = child_pid;
                    do_passenger_job(child_pid, age, grp);

                    //parent
                    int parent_pid = grp + 1000;
                    int parent_age = rand() % 50 + 20; //wiek 20 - 69
                    used_pids[used_count++] = parent_pid;
                    do_passenger_job(parent_pid, parent_age, grp);

                }
                else
                {
                    // zwykly dorosły, group = 0
                    int new_pid = origin_pid + used_count;
                    used_pids[used_count] = new_pid;
                    used_count++;
                    do_passenger_job(new_pid, age, 0);
                }

                if (used_count >= 6000) 
                {
                    printf("[GENERATOR] Reached 6000 pid - STOP.\n");
                    break;
                }
            }
        }

        //Zabezpieczenie, jesli bysmy doszli do 6000 procesow - case dla pozostalych opcji
        if (used_count >= 6000)
        {
            printf("[GENERATOR] Reached 6000 pid - STOP.\n");
            break;
        }

        //Pause 1s-2s
        int rnd_sleep = rand()%2 + 1;
        for (int i=0; i < rnd_sleep * 10 && !terminate_flag; i++){usleep(100000);}
    }
    return NULL;
}


// --- Funkcja dla watku (timeout_killer_thread) ---
void *time_killer_func(void *arg)
{
    sleep(TIMEOUT);
    if (!terminate_flag)
    {
        printf("[SCHEDULER-TIME] Time out = %d => end.\n", TIMEOUT);
        end_sim();
    }
    return NULL;
}

// --- Usuwanie plikow z systemu plikow ---
void remove_fifo()
{
    unlink(FIFO_CASHIER_IN);
}

// --- Konczenie symulacji---
/*Kolejnosc: 
1.sprawdzenie co juz przestalo dzialac
2.EXIT do kasjera/sternika 
3.kill 
4.czekanie 
5.sprzatanie
*/
void end_sim()
{
    if(terminate_flag){return;}
    terminate_flag = 1;
    gen_running_flag = 0;  

    printf("[SCHEDULER] end_sim() check: EXIT, kill -TERM, kill -9 (...)\n");
    printf("[SCHEDULER] Total passengers generated: %d.\n", total_gen_pass);

    //sprawdzamy, ktore przestaly dzialac
    for(int i=0; i < passenger_count; i++)
    {
        if(pid_pass[i] > 0)
        {
            pid_t w = waitpid(pid_pass[i], NULL, WNOHANG);
            if(w == pid_pass[i]){pid_pass[i] = 0;}
        }
    }
    if(pid_cashier > 0)
    {
        pid_t w = waitpid(pid_cashier, NULL, WNOHANG);
        if(w == pid_cashier){ pid_cashier = 0;}
    }

    //EXIT
    if(pid_cashier > 0)
    {
        int fk = open(FIFO_CASHIER_IN, O_WRONLY);
        if(fk >= 0)
        {
            write(fk, "EXIT\n", 5);
            close(fk);
        }
        printf("[SCHEDULER] (EXIT) cashier pid = %d\n", pid_cashier);
    
    }
    //SIGTERM
    for(int i=0; i < passenger_count; i++)
    {
        if(pid_pass[i] > 0){kill(pid_pass[i], SIGTERM);}
    }
    if(pid_cashier > 0){kill(pid_cashier, SIGTERM);}

    usleep(500000);

    //SIGKILL
    for(int i=0;i<passenger_count;i++)
    {
        if(pid_pass[i] > 0)
        {
            if(0 == waitpid(pid_pass[i], NULL, WNOHANG)){kill(pid_pass[i], SIGKILL);}
        }
    }

    if(pid_cashier > 0)
    {
        if(0 == waitpid(pid_cashier,NULL,WNOHANG)){kill(pid_cashier, SIGKILL);}
    }

    //Czekanie
    for(int i=0; i < passenger_count; i++)
    {
        if(pid_pass[i] > 0)
        {
            waitpid(pid_pass[i], NULL, 0);
            pid_pass[i] = 0;
        }
    }

    if(pid_cashier > 0)
    {
        waitpid(pid_cashier, NULL, 0);
        pid_cashier = 0;
    }

    remove_fifo();
    printf("[SCHEDULER] end_sim => completed.\n");
}

// --- Uruchomienie cashier'a ---
static void start_cashier(void)
{
    if(pid_cashier > 0)
    {
        printf("[SCHEDULER] Cashier already running.\n");
        return;
    }
    char *arguments[] = {(char*)PATH_CASHIER, NULL};

    pid_t pc = do_child_job(PATH_CASHIER, arguments);
    if(pc > 0)
    {
        pid_cashier = pc;
        printf("[SCHEDULER] Cashier pid = %d.\n", pc);
    }
}

// --- MAIN ---
int main(void)
{
    setbuf(stdout, NULL); //wylaczanie buforowania dla standardowego wyjscia (stdout)

    //Pytanie usera o czas symulacji
    int user_timeout = 0;
    while(1)
    {
        printf("[SCHEDULER] Enter simulation time [s] (>0): ");
        fflush(stdout); //natychmiastowe wypisanie na ekran

        if(scanf("%d", &user_timeout) != 1)
        {
            while(getchar() != '\n');
            printf("[SCHEDULER] Invalid format, try again.\n");
            continue;
        }
        if(user_timeout <= 0)
        {
            printf("[SCHEDULER] Error: Time must be > 0.\n");
            continue;
        }
        break;
    }

    TIMEOUT = user_timeout;
    while(getchar() != '\n'); // wczytanie entera jesli sie ewentualnie pojawi

    remove_fifo();
    mkfifo(FIFO_CASHIER_IN, 0666);

    start_cashier();
    usleep(700000);

    //tworzenie watku: generatora i timeout_killer_thread
    pthread_create(&generator_thread, NULL, generator_function, NULL);
    pthread_create(&timeout_killer_thread, NULL, time_killer_func, NULL);

    printf("[SCHEDULER] Command: q => end.\n");

    char command;
    while(!terminate_flag)
    {
        fd_set read_fds; // Zmienna do przechowywania deskryptorow do odczytu
        FD_ZERO(&read_fds); // Inicjalizacja zestawu deskryptorow
        FD_SET(STDIN_FILENO, &read_fds); // Dodanie deskryptora standardowego wejscia do zestawu

        struct timeval tv; // Struktura na czas oczekiwania
        tv.tv_sec = 0; 
        tv.tv_usec = 100000; // 0.1s (mikrosekundy czasu oczekiwania 100ms)
        int ret = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &tv);

        if(ret < 0)
        {
            if(errno == EINTR) continue; //wznowienie w sytuacji przerwania przez sygnal
            perror("[SCHEDULER] select error");
            break;
        }

        if(ret==0){/*uzytkownik nic nie wpisal*/}
        else
        {
            if(FD_ISSET(STDIN_FILENO, &read_fds)) // jest cos do odczytania na stdin
            {
                command = getchar();
                if(command == EOF){break;}

                if(command == 'q'){end_sim();break;}
                else{printf("[SCHEDULER] Unknown command.\n");}
            }
        }
    }

    if(!terminate_flag){end_sim();}

    //oczekiwanie na zakonczenie pracy watkow
    pthread_join(generator_thread, NULL);
    pthread_join(timeout_killer_thread, NULL);

    return 0;
}