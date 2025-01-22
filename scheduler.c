/*
scheduler.c
Dostepne komendy:
p - call police - wysyla sygnal do lodzi
q - QUIT, konczy symulacje
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
#include <sys/socket.h> // Operacje na gniazdach
#include <sys/un.h>     // Struktury i funkcje dla gniazd domeny Unix

// --- Sciezki do plikow ---
#define PATH_CASHIER   "./cashier"
#define PATH_PASSENGER "./passenger"
#define PATH_POLICE    "./police"
#define PATH_STERNIK   "./sternik"

// --- Sockety ---
#define CASHIER_SOCKET_PATH "/tmp/cashier_socket" // Ścieżka do gniazda
#define STERNIK_SOCKET_PATH "/tmp/sternik_socket"

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
static pid_t pid_sternik = 0;
static pid_t pid_cashier = 0;
static pid_t pid_police = 0;
static pid_t pid_pass[MAX_PASS]; // dla pasazerow
static int passenger_count = 0; //ilosc pasazerow

// --- Generator ---
static pthread_t generator_thread; //watek generatora
static pthread_t timeout_killer_thread; //watek konczacy symulacje po TIMEOUT
static volatile int gen_running_flag = 1; //flaga, ktora steruje jego praca

// --- Funkcje ---
// Funkcja wysyłająca komendę QUIT do kasjera
void send_quit_to_cashier()
{
    // Tworzenie gniazda
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("[SCHEDULER] Error creating socket");
        exit(1);
    }

    // Konfiguracja adresu gniazda kasjera
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, CASHIER_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Łączenie z gniazdem kasjera
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[SCHEDULER] Error connecting to cashier");
        close(client_fd);
        exit(1);
    }

    // Wysłanie komendy "QUIT" do kasjera
    const char *quit_message = "QUIT\n";
    if (write(client_fd, quit_message, strlen(quit_message)) < 0) {
        perror("[SCHEDULER] Error sending QUIT message");
        close(client_fd);
        exit(1);
    }

    printf("[SCHEDULER] Sent QUIT command to cashier.\n");

    // Zamknięcie połączenia
    close(client_fd);
}

void send_quit_to_sternik()
{
    // Tworzenie gniazda
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0)
    {
        perror("[SCHEDULER] Error creating socket");
        return; // Nie przerywamy procesu
    }

    // Konfiguracja adresu gniazda sternika
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, STERNIK_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Łączenie z gniazdem sternika
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        if (errno == ECONNREFUSED || errno == ENOENT)
        {
            // Jeśli sternik zakończył pracę, ignorujemy błąd
            printf("[SCHEDULER] Sternik is not active, skipping QUIT.\n");
        }
        else
        {
            perror("[SCHEDULER] Error connecting to sternik");
        }
        close(client_fd);
        return;
    }

    // Wysłanie komendy "QUIT" do sternika
    const char *quit_message = "QUIT\n";
    if (write(client_fd, quit_message, strlen(quit_message)) < 0)
    {
        perror("[SCHEDULER] Error sending QUIT message");
    }
    else
    {
        printf("[SCHEDULER] Sent QUIT command to sternik.\n");
    }

    // Zamknięcie połączenia
    close(client_fd);
}


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
            if (passenger_count >= MAX_PASS) //przed kazdym generowaniem sprawdzamy, zeby nie przekroczyc MAX_PASS
            {
                printf("[GENERATOR] Reached MAX_PASS.\n");
                break;
            }
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
            if (passenger_count >= MAX_PASS)
            {
                printf("[GENERATOR] Reached MAX_PASS.\n");
                break;
            }
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
                if (passenger_count >= MAX_PASS)
                {
                    printf("[GENERATOR] Reached MAX_PASS.\n");
                    break;
                }

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
                    printf("[GENERATOR] Reached 6000 pid - STOP.\n"); //zabezpiecznie dla batcha
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

        // przychodza losowo co pewien czas 1s-2s
        int rnd_sleep = (rand() % 2 + 1) * 1000000; // Losowy czas 1–2 sekundy w mikrosekundach
        usleep(rnd_sleep);
    }
    return NULL;
}


void *time_killer_function(void *arg)
{
    time_t start_time, current_time;

    // Pobieramy czas startu (w sekundach)
    start_time = time(NULL);

    while(1)
    {
        // Pobieramy bieżący czas
        current_time = time(NULL);

        // Sprawdzamy, czy minął czas (TIMEOUT)
        if (current_time - start_time >= TIMEOUT)
        {
            if (!terminate_flag)
            {
                printf("[SCHEDULER-TIME] Time out = %d seconds => END\n", TIMEOUT);
                end_sim();
            }
            return NULL;
        }

        // Jeśli symulacja została przerwana, kończymy przed czasem
        if (terminate_flag)
        {
            printf("[SCHEDULER-TIME] Simulation interrupted after %d seconds.\n", (int)(current_time - start_time));
            return NULL;
        }

        // Opóźnienie przed kolejną kontrolą (10ms)
        usleep(10000); // 10ms
    }

    return NULL;
}

// --- Usuwanie plikow z systemu plikow ---
void cleanup()
{
    unlink(STERNIK_SOCKET_PATH);
    unlink(CASHIER_SOCKET_PATH);
}

// --- Konczenie symulacji---
/*Kolejnosc: 
1.Sprawdzenie, czy dany proces juz zakonczyl sie samodzielnie
2.QUIT do kasjera/sternika - pozwolenie procesom na zakonczenie dzialania w sposob kontrolowany
3.SIGTERM - lagodnie informujemy o koniecznosci zakonczenia dzialania
4.SIGKILL - zakonczenie dzialania procesow, ktore mogly nie odpowiedziec na wczesniejsze sygnaly
5.Czekanie - Ostateczne oczekiwanie na zakonczenie procesow
6.Sprzatanie
*/
void end_sim()
{
    if(terminate_flag){return;}
    terminate_flag = 1;
    gen_running_flag = 0;  

    // printf("[SCHEDULER] end_sim(): CHECK, QUIT, SIGTERM, SIGKILL (...)\n");
    printf("\033[38;5;15m[SCHEDULER] end_sim(): \033[38;5;15mCHECK, \033[38;5;226mQUIT, \033[38;5;214mSIGTERM, \033[38;5;1mSIGKILL \033[0m(...)\n");

    printf("[SCHEDULER] Total passengers generated: %d.\n", total_gen_pass);

    //sprawdzamy, ktore przestaly dzialac
    if (pid_police > 0)
    {
        pid_t w = waitpid(pid_police, NULL, WNOHANG); // WNOHANG - nie wstrzymuje procesu
        if (w == pid_police){pid_police = 0;}
    }

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

    if(pid_sternik > 0)
    {
        pid_t w = waitpid(pid_sternik, NULL, WNOHANG);
        if(w == pid_sternik){pid_sternik = 0;}
    }

    //QUIT
    if(pid_sternik > 0){send_quit_to_sternik();}

    if(pid_cashier > 0){send_quit_to_cashier();}

    usleep(500000);

    //SIGTERM
    if(pid_police > 0){kill(pid_police, SIGTERM);}

    for(int i=0; i < passenger_count; i++)
    {
        if(pid_pass[i] > 0){kill(pid_pass[i], SIGTERM);}
    }
    if(pid_cashier > 0){kill(pid_cashier, SIGTERM);}
    if(pid_sternik > 0){kill(pid_sternik, SIGTERM);}
    usleep(500000);

    //SIGKILL
    if(pid_police > 0)
    {
        if(0 == waitpid(pid_police, NULL, WNOHANG)){kill(pid_police, SIGKILL);}
    }

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

    if(pid_sternik > 0)
    {
        if(0 == waitpid(pid_sternik,NULL,WNOHANG)){kill(pid_sternik, SIGKILL);}
    }

    //Czekanie
    if(pid_police > 0)
    {
        waitpid(pid_police, NULL, 0);
        pid_police = 0;
    }

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

    if(pid_sternik > 0)
    {
        waitpid(pid_sternik, NULL, 0);
        pid_sternik = 0;
    }

    cleanup();
    printf("\033[1;38;5;10m[SCHEDULER] end_sim => FINISHED.\033[0m\n");
}

// --- Uruchomienie sternika ---
static void start_sternik()
{
    if(pid_sternik > 0)
    {
        printf("[SCHEDULER] Sternik already running.\n");
        return;
    }
    char arg[32];
    sprintf(arg, "%d", TIMEOUT);
    char *arguments[] = {(char*)PATH_STERNIK, arg, NULL};

    pid_t pc = do_child_job(PATH_STERNIK, arguments);
    if(pc > 0)
    {
        pid_sternik = pc;
        printf("[SCHEDULER] Sternik pid = %d.\n", pc);
    }
}

// --- Uruchomienie cashier'a ---
static void start_cashier()
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

// --- Uruchomienia police ---
static void start_police()
{
    if(pid_sternik <= 0)
    {
        printf("[SCHEDULER] No sternik => police no signals.\n");
        return;
    }

    if(pid_police > 0)
    {
        printf("[SCHEDULER] Police already running.\n");
        return;
    }

    char arg[32];
    sprintf(arg,"%d", pid_sternik);
    char *arguments[] = {(char*)PATH_POLICE, arg, NULL};

    pid_t pc = do_child_job(PATH_POLICE, arguments);
    if(pc > 0)
    {
        pid_police = pc;
        printf("[SCHEDULER] Police_pid = %d, sternik = %d.\n", pc, pid_sternik);
    }
}

// --- MAIN ---
int main()
{
    setbuf(stdout, NULL); //wylaczanie buforowania dla standardowego wyjscia (stdout)
    system("clear");
    printf("\033[1;38;5;15m                 __/\\__\n               /~~~~~~/\n           ~~~~~~~~~~~\n        ~~~~~~~~~~~~~\033[4;1;38;5;15m\t\t\tCRUISES\033[0m\n\033[1;38;5;15m      ~~~~~~~~~~~~~~~~\n       ~~~~~~~~~~~~~~~~~\n              | _ |\n              | _ |\n        \033[1;38;5;130m|=================|\033[0m\n\n");
    
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

    cleanup();
    
    start_sternik();
    usleep(200000);
    start_cashier();
    usleep(200000);

    //tworzenie watku: generatora i timeout_killer_thread
    pthread_create(&generator_thread, NULL, generator_function, NULL);
    pthread_create(&timeout_killer_thread, NULL, time_killer_function, NULL);

    printf("[SCHEDULER] Commands: p => call police | q => end simulation (quit)\n");

    char command;
    while(!terminate_flag)
    {
        //sprawdzamy czy sternik sie skonczyl
        if(pid_sternik > 0)
        {
            int status;
            pid_t w = waitpid(pid_sternik, &status, WNOHANG);
            if(w == pid_sternik)
            {
                printf("[SCHEDULER] sternik ended => end.\n");
                end_sim();
                break;
            }
        }

        fflush(stdout);      

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

        if(ret == 0 ){/*uzytkownik nic nie wpisal*/}
        else
        {
            if(FD_ISSET(STDIN_FILENO, &read_fds)) // jest coś do odczytania na stdin
            {
                int command = getchar(); // Odczytaj główną komendę
                if(command == EOF){break;}

                if(command == '\n'){continue;} // Ignoruj znak nowej linii 

                if(command == 'q'){end_sim();break;}
                else if(command == 'p'){start_police();}
                else{printf("[SCHEDULER] Unknown command.\n");}

                // Usuń pozostałe znaki w buforze
                int ch;
                while((ch = getchar()) != '\n' && ch != EOF);
            }
        }
    }

    if(!terminate_flag){end_sim();}

    //oczekiwanie na zakonczenie pracy watkow
    pthread_join(generator_thread, NULL);
    pthread_join(timeout_killer_thread, NULL);

    return 0;
}