//sternik.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <stdarg.h>     // Zmienna ilość elementów do insta_print
#include <sys/socket.h> // Operacje na gniazdach
#include <sys/un.h>     // Struktury i funkcje dla gniazd domeny Unix
#include <semaphore.h>

#define STERNIK_SOCKET_PATH "/tmp/sternik_socket" // Ścieżka do gniazda dla procesu sternika
#define MAX_BUFFER_SIZE 256                       // Maksymalny rozmiar bufora do przesyłania danych

int terminate_sternik = 0;  // Flaga kontrolująca zakończenie programu

//BOAT1
#define N1 6
#define T1 3

//BOAT2
#define N2 8
#define T2 7

// Jeśli grupa będzie większa niż 0, obsługujemy grupę (np. dziecko + opiekun)
#define GROUP_MAX 30000                         // Maksymalna liczba grup, które możemy obsługiwać
static int group_targetval[GROUP_MAX] = {0};    // Tablica przechowująca docelową wielkość grupy
static int group_count[GROUP_MAX] = {0};        // Tablica śledząca, ile osób z konkretnej grupy zostało już załadowanych

// Czas załadunku na pokład (ONBOARD_TIMEOUT) i limity dla pomostów
#define ONBOARD_TIMEOUT 6   // Czas załadunku w sekundach
#define K1 3                // Limit pasażerów na pomost 1 (mniejszy niż N1)
#define K2 3                // Limit pasażerów na pomost 2 (mniejszy niż N2)

// Długość kolejki pasażerów
#define QUEUE_SIZE 30

// Semafory do synchronizacji kolejek
sem_t boat1_sem;
sem_t boat2_sem;

// Inicjalizacja semaforów dla boat1 i boat2
void init_semaphores()
{
    if(sem_init(&boat1_sem, 0, QUEUE_SIZE) == -1 || sem_init(&boat2_sem, 0, QUEUE_SIZE) == -1)
    {
        perror("sem_init boat1 boat2 failed");
        exit(1);
    }
}

// --- Struktura pasazera ---
typedef struct
{
    int pid;
    pid_t realpid;
    int group;
} PassengerData;

// --- Struktura kolejki pasazerow ---
typedef struct
{
    PassengerData items[QUEUE_SIZE];    // Tablica przechowująca elementy kolejki (pasażerów)
    int head, tail;                     // Indeksy głowy i ogona kolejki
    int count;                          // Liczba pasażerów w kolejce
} PassengerQueue;

// Funkcja inicjalizująca kolejkę pasażerów
static void init_queue(PassengerQueue *q)
{
    q->head=0;
    q->tail=0;
    q->count=0;
}

// Funkcja sprawdzająca, czy kolejka jest pusta
static int is_Empty(PassengerQueue *q){return q->count == 0;}

// Funkcja sprawdzająca, czy kolejka jest pełna
static int is_Full(PassengerQueue *q){return q->count == QUEUE_SIZE;}

// Funkcja dodająca pasażera do kolejki
static int add_to_queue(PassengerQueue *q, PassengerData itm)
{
    if(is_Full(q)){return -1;}

    q->items[q->tail] = itm;
    q->tail = (q->tail+1) % QUEUE_SIZE; //modulo dla okrężnej kolejki - cykliczne fifo
    q->count++;

    return 0;
}

// Funkcja ściągająca pasażera z kolejki
static PassengerData remove_from_queue(PassengerQueue *q)
{
    PassengerData tmp = {0,0,0};

    if(is_Empty(q)){return tmp;}

    tmp = q->items[q->head];
    q->head = (q->head+1) % QUEUE_SIZE;
    q->count--;

    return tmp;
}

// --- Kolejki ---
static PassengerQueue boat1_queue, boat1_skip_queue;
static PassengerQueue boat2_queue, boat2_skip_queue;

// --- Stany BOAT1 i BOAT2 ---
static volatile sig_atomic_t boat1_active = 1, boat1_cruising = 0;
static volatile sig_atomic_t boat2_active = 1, boat2_cruising = 0;

// --- Czas ---
static time_t start_time;
static time_t end_time;

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER; // Muteks w celu synchronizacji stanu aktywności łodzi oraz innych działań

// --- Funkcja do natychmiastowego printowania ---
static void insta_print(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);             // Inicjalizacja listy argumentow zmiennych
    vfprintf(stdout, message, ap);     // Wypisanie na stdout z formatowaniem
    fflush(stdout);                    // Natychmiastowe oproznienie bufora
    va_end(ap);                        // Zakonczenie przetwarzania listy argumentow
}

//Funkcja do przetwarzania żądań pasażerów
void process_passenger_request(int client_sock_fd)
{
    char buffer[MAX_BUFFER_SIZE]; // Bufor do przechowywania danych przychodzących z klienta
    ssize_t bytes_read = read(client_sock_fd, buffer, sizeof(buffer) - 1); // Odczytanie danych z gniazda
    
    if (bytes_read < 0)
    {
        perror("[STERNIK] Error reading from client socket");
        return;
    }

    buffer[bytes_read] = '\0';  // Zakończenie ciągu znaków (null-terminate)
    int pid, age, grp;
    pid_t realpid;
    if(strncmp(buffer, "SKIP_QUEUE", 10) == 0)
    {
        // Odbieranie SKIP_QUEUE
        if(sscanf(buffer, "SKIP_QUEUE %d %d %d %d", &pid, &age, &grp, &realpid) == 4)
        {
            PassengerData pass_i = {pid, realpid, grp};
            pthread_mutex_lock(&m);

            // Wybór łodzi
            srand(time(NULL));
            int boat_nr = (grp > 0 || age < 15 || age > 70) ? 2 : 1;

            if(boat_nr == 1 && boat1_active)
            {
                // Sprawdzanie, czy kolejka jest pełna
                if(is_Full(&boat1_skip_queue))
                {
                    insta_print("[STERNIK] boat1_skip_queue is full => Passenger %d waiting...\n", pid);
                    pthread_mutex_unlock(&m);
                    sem_wait(&boat1_sem);  // Czekaj na semaforze, aż miejsce w kolejce się zwolni
                    pthread_mutex_lock(&m);
                    insta_print("[STERNIK] Passenger %d added to boat1_skip_queue after waiting.\n", pid);
                }

                add_to_queue(&boat1_skip_queue, pass_i);
                insta_print("[STERNIK] Passenger %d skipping queue => boat1_skip_queue\n", pid);
            }
            else if(boat_nr == 2 && boat2_active)
            {
                // Sprawdzanie, czy kolejka jest pełna
                if(is_Full(&boat2_skip_queue))
                {
                    insta_print("[STERNIK] boat2_skip_queue is full => Passenger %d waiting...\n", pid);
                    pthread_mutex_unlock(&m);
                    sem_wait(&boat2_sem);  // Czekaj na semaforze, aż miejsce w kolejce się zwolni
                    pthread_mutex_lock(&m);
                    insta_print("[STERNIK] Passenger %d added to boat2_skip_queue after waiting.\n", pid);
                }

                add_to_queue(&boat2_skip_queue, pass_i);
                insta_print("[STERNIK] Passenger %d skipping queue => boat2_skip_queue\n", pid);
            }
            else
            {
                insta_print("[STERNIK] BOAT%d not active => REJECTED %d\n", boat_nr, pid);
                //SIGRTMIN + 1 - nie udało się popłynąć pasażerowi
                // Wysłanie sygnału SIGRTMIN+1
                if (kill(realpid, SIGRTMIN+1) == -1)
                {
                    perror("Nie udało się wysłać sygnału");
                    return;
                }
            }

            pthread_mutex_unlock(&m);
        }
    }
    else if(strncmp(buffer, "QUEUE", 5) == 0)
    {
        // Odbieranie QUEUE
        if(sscanf(buffer, "QUEUE %d %d %d %d", &pid, &age, &grp, &realpid) == 4)
        {
            PassengerData pass_i = {pid, realpid, grp};
            pthread_mutex_lock(&m);

            // Wybór łodzi
            srand(time(NULL));
            int boat_nr = (grp > 0 || age < 15 || age > 70) ? 2 : 1;

            if(boat_nr == 1 && boat1_active)
            {
                // Sprawdzanie, czy kolejka jest pełna
                if(is_Full(&boat1_queue))
                {
                    insta_print("[STERNIK] boat1_queue is full => Passenger %d waiting...\n", pid);
                    pthread_mutex_unlock(&m);
                    sem_wait(&boat1_sem);  // Czekaj na semaforze, aż miejsce w kolejce się zwolni
                    pthread_mutex_lock(&m);
                    insta_print("[STERNIK] Passenger %d added to boat1_queue after waiting.\n", pid);
                }

                add_to_queue(&boat1_queue, pass_i);
                insta_print("[STERNIK] Passenger %d => boat1\n", pid);
            }
            else if(boat_nr == 2 && boat2_active)
            {
                // Sprawdzanie, czy kolejka jest pełna
                if (is_Full(&boat2_queue))
                {
                    insta_print("[STERNIK] boat2_queue is full => Passenger %d waiting...\n", pid);
                    pthread_mutex_unlock(&m);
                    sem_wait(&boat2_sem);  // Czekaj na semaforze, aż miejsce w kolejce się zwolni
                    pthread_mutex_lock(&m);
                    insta_print("[STERNIK] Passenger %d added to boat2_queue after waiting.\n", pid);
                }

                add_to_queue(&boat2_queue, pass_i);
                insta_print("[STERNIK] Passenger %d => boat2\n", pid);
            }
            else
            {
                insta_print("[STERNIK] BOAT%d not active => REJECTED %d\n", boat_nr, pid); //Poinformowanie, że dana łódź nie jest aktywna
                //SIGRTMIN + 1 - nie udało się popłynąć pasażerowi
                // Wysłanie sygnału SIGRTMIN+1
                if (kill(realpid, SIGRTMIN+1) == -1)
                {
                    perror("Nie udało się wysłać sygnału");
                    return;
                }
            }

            pthread_mutex_unlock(&m);
        }
    }
    else if(strncmp(buffer, "QUIT", 4) == 0)
    {
        // Odbieranie QUIT
        insta_print("[STERNIK] Terminating. (QUIT RECEIVED)\n");
        terminate_sternik = 1;  // Ustawienie flagi zakończenia
    }
    else
    {
        printf("[STERNIK] UNKNOWN: %s\n", buffer);
    }
}

// --- Pomost ---
static int bridge1_count; //liczba osob na pomoscie1
static int bridge2_count; //liczba osob na pomoscie2

typedef enum 
{
    INCOMING, OUTGOING, FREE //stany pomostu - ruch przychodzacy(onboarding) - ruch wychodzacy(offboarding) - wolny
} BridgeState;

static BridgeState bridge1_state, bridge2_state;

// --- Funkcje dla pomostu ---
static void init_bridges()
{
    bridge1_count = 0;
    bridge1_state = FREE;
    bridge2_count = 0;
    bridge2_state = FREE;
}

static int enter_bridge1()
{
    if(bridge1_state == FREE){bridge1_state = INCOMING;}
    else if(bridge1_state != INCOMING){return 0;}

    if(bridge1_count >= K1){return 0;}
    bridge1_count++;
    return 1;
}
static int enter_bridge2()
{
    if(bridge2_state == FREE){bridge2_state = INCOMING;}
    else if(bridge2_state != INCOMING){return 0;}

    if(bridge2_count >= K1){return 0;}
    bridge2_count++;
    return 1;
}

static void leave_bridge1()
{
    bridge1_count--;
    if(bridge1_count == 0){bridge1_state = FREE;}
}
static void leave_bridge2()
{
    bridge2_count--;
    if(bridge2_count == 0){bridge2_state = FREE;}
}

static void begin_outgoing1()
{
    while(bridge1_state != FREE){/*zajęty nie można zmienić na outgoing*/}
    bridge1_state = OUTGOING;
}
static void begin_outgoing2()
{
    while(bridge2_state != FREE){/*zajęty nie można zmienić na outgoing*/}
    bridge2_state = OUTGOING;
}

static void end_outgoing1(){bridge1_state = FREE;}
static void end_outgoing2(){bridge2_state = FREE;}

// --- SIGNAL HANDLERS ---
static void SIGUSR1_handler(int signal1)
{
    if(!boat1_cruising){insta_print("[BOAT1] received (SIGUSR1) in port => end.\n");}
    else{insta_print("[BOAT1] received (SIGUSR1) while cruising => will end after cruise.\n");}
    terminate_sternik = 1;
    boat1_active = 0;
}

static void SIGUSR2_handler(int signal2)
{
    if(!boat2_cruising){insta_print("[BOAT2] received (SIGUSR2) in port => end.\n");}
    else{insta_print("[BOAT2] received (SIGUSR2) while cruising => will end after cruise.\n");}
    terminate_sternik = 1;
    boat2_active = 0;
}

// --- WATEK BOAT1 ---
void *boat1_thread(void *arg)
{
    insta_print("[BOAT1] START N1: %d T1: %ds.\n", N1, T1);

    while (1)
    {
        // Blokowanie mutexu, sprawdzanie aktywności łodzi
        pthread_mutex_lock(&m);
        if (!boat1_active) // Jeśli łódź jest nieaktywna, kończymy wątek
        {
            pthread_mutex_unlock(&m); // Zwalniamy mutex
            insta_print("[BOAT1] boat1_active = 0, END\n");
            break; // Zakończenie pętli, łódź kończy działanie
        }

        // Sprawdzamy, czy obie kolejki są puste
        if (is_Empty(&boat1_skip_queue) && is_Empty(&boat1_queue))
        {
            pthread_mutex_unlock(&m);
            continue;
        }

        insta_print("[BOAT1] Onboarding...\n"); // Informacja o rozpoczęciu załadunku
        time_t onboard_starttime = time(NULL);  // Czas rozpoczęcia załadunku
        int onboarded = 0;                      // Licznik załadowanych pasażerów
        
        // Tymczasowa struktura do przechowywania pasażerów na pokładzie
        PassengerData rejsStruct[N1];
        int rejsCount = 0;

        // Ładujemy pasażerów dopóki jest miejsce, łódź jest aktywna i czas na załadunek
        while (rejsCount < N1 && boat1_active)
        {
            PassengerQueue *q = NULL;

            // Priorytetowo próbujemy załadować pasażerów ze skip_queue
            if (!is_Empty(&boat1_skip_queue))
            {
                q = &boat1_skip_queue;
            } 
            // Jeśli nie ma pasażerów w skip_queue, ładujemy ze zwykłej kolejki
            else if (!is_Empty(&boat1_queue))
            {
                q = &boat1_queue;
            }
            else
            {
                // Jeśli obie kolejki są puste, zwalniamy mutex i ponownie blokujemy, aby sprawdzić warunki
                pthread_mutex_unlock(&m);
                pthread_mutex_lock(&m);

                // Sprawdzamy, czy łódź jest nadal aktywna i czy nie przekroczyliśmy limitu czasu na załadunek
                if(!boat1_active){break;}
                if(difftime(time(NULL), onboard_starttime) >= ONBOARD_TIMEOUT){break;}
                
                continue;
            }

            // Mamy kolejkę do załadunku, bierzemy pasażera z przodu
            PassengerData p = q->items[q->head];

            if (bridge1_state == FREE || bridge1_state == INCOMING)
            {
                if (bridge1_count < K1) // Sprawdzamy, czy na pomoście jest miejsce
                {
                    // Zdejmujemy pasażera z kolejki
                    remove_from_queue(q);
                    sem_post(&boat1_sem); //Zwiększamy semafor, aby kolejni pasażerowie mogli zostać dodani do kolejki
                    
                    // Pasażer wchodzi na pomost, jeśli jest miejsce i stan pomostu to INCOMING
                    if (enter_bridge1())
                    {
                        leave_bridge1();
                        rejsStruct[rejsCount++] = p;    // Dodajemy pasażera do struktury rejsu
                        onboarded++;                    // Zwiększamy licznik załadowanych pasażerów
                        insta_print("\033[1;38;5;14m[BOAT1] \033[38;5;15mPassenger %d \033[38;5;10mentered [%d/%d]\033[0m\n", p.pid, onboarded, N1);
                    }
                }
                else
                {
                    // Jeśli pomost jest pełny, zwalniamy mutex i czekamy na miejsce
                    pthread_mutex_unlock(&m);
                    pthread_mutex_lock(&m);
                }
            }
            else
            {
                // Jeśli stan pomostu to OUTGOING, czekamy, aż się zwolni
                pthread_mutex_unlock(&m);
                pthread_mutex_lock(&m);
            }

            // Sprawdzamy warunki zakończenia pętli
            if (!boat1_active){break;}
            if (onboarded == N1){break;}
            if (difftime(time(NULL), onboard_starttime) >= ONBOARD_TIMEOUT){break;}
        }

        // Jeśli łódź przestaje być aktywna w trakcie załadunku, kończymy wątek
        if (!boat1_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT1] signal during onboarding.\n");

            // Wysłanie sygnału SIGRTMIN+1 do pasażerów, których nie udało się załadować
            for (int i = 0; i < rejsCount; i++)
            {
                if (kill(rejsStruct[i].realpid, SIGRTMIN+1) == -1)
                {
                    perror("[STERNIK] Couldn't send signal to passenger\n");
                    continue;
                }
            }
            break;
        }
        // Jeśli nikt nie wszedł na pokład, czekamy chwilę i powtarzamy cykl
        if (onboarded == 0)
        {
            pthread_mutex_unlock(&m);
            continue;
        }
        // Czekamy, aż pomost się zwolni (nikt nie wchodzi przed wypłynięciem)
        while ((bridge1_state == INCOMING || bridge1_count > 0) && boat1_active){}
        
        if (!boat1_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT1] interrupted before departure.\n");
            // Wysłanie sygnału SIGRTMIN+1 do pasażerów
            for (int i = 0; i < rejsCount; i++)
            {
                if (kill(rejsStruct[i].realpid, SIGRTMIN+1) == -1)
                {
                    perror("[STERNIK] Couldn't send signal to passenger\n");
                    continue;
                }
            }
            break;
        }

        // Sprawdzamy, czy jest jeszcze czas na rejs
        time_t now = time(NULL);
        if (now + T1 > end_time)
        {
            insta_print("[BOAT1] no time left for a cruise.\n");
            begin_outgoing1();  // Rozpoczynamy ruch wychodzący
            for (int i = 0; i < rejsCount; i++)
            {
                if (kill(rejsStruct[i].realpid, SIGRTMIN+1) == -1)
                {
                    perror("[STERNIK] Couldn't send signal to passenger\n");
                    continue;
                }
            }
            insta_print("[BOAT1] Passengers left.\n");
            end_outgoing1();    // Kończymy ruch wychodzący
            pthread_mutex_unlock(&m);
            break;
        }

        // Rozpoczynamy rejs
        boat1_cruising = 1;
        insta_print("\033[1;38;5;14m[BOAT1] \033[38;5;10mDeparting with \033[1;38;5;15m%d\033[38;5;15m passengers.\033[0m\n", rejsCount);
        pthread_mutex_unlock(&m);

        sleep(T1); // Symulacja czasu trwania rejsu

        // Wracamy po rejsie, rozpoczynamy offboarding
        pthread_mutex_lock(&m);
        boat1_cruising = 0;
        insta_print("[BOAT1] Cruise finished => offboarding.\n"); //ruch wychodzacy (offboarding)
        begin_outgoing1();

        // Wysłanie sygnału SIGRTMIN do pasażerów, którzy odbyli rejs
        for (int i = 0; i < rejsCount; i++)
        {
            if (kill(rejsStruct[i].realpid, SIGRTMIN) == -1)
            {
                perror("[STERNIK] Couldn't send signal to passenger\n");
                continue;
            }
        }
        insta_print("[BOAT1] Passengers left.\n");
        end_outgoing1();
        
        // Sprawdzamy, czy łódź została wyłączona po zakończeniu rejsu
        if (!boat1_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT1] signal executed after offboarding\n"); //Sygnał po wyładunku
            break; // Kończymy wątek
        }
        pthread_mutex_unlock(&m);

    }

    // Kończenie działania wątku, ustawienie łodzi na nieaktywną
    pthread_mutex_lock(&m);
    boat1_active = 0;
    pthread_mutex_unlock(&m);
    insta_print("[BOAT1] THREAD END.\n");
    return NULL;
}

// --- WATEK BOAT2 ---
//group_targetval domyslnie 2
//nie wyplywamy dopoki nie mamy pelnej grupy
void *boat2_thread(void *arg)
{
    insta_print("[BOAT2] START N2: %d T2: %ds.\n",N2,T2);

    while(1)
    {
        // Blokowanie mutexu, sprawdzanie aktywności łodzi
        pthread_mutex_lock(&m);
        if(!boat2_active) // Jeśli łódź jest nieaktywna, kończymy wątek
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT2] boat2_active = 0, END\n");
            break; // Zakończenie pętli, łódź kończy działanie
        }

        // Sprawdzamy, czy obie kolejki są puste
        if(is_Empty(&boat2_skip_queue) && is_Empty(&boat2_queue))
        {
            pthread_mutex_unlock(&m);
            continue;
        }
        
        insta_print("[BOAT2] Onboarding...\n"); // Informacja o rozpoczęciu załadunku
        time_t onboard_starttime = time(NULL);  // Czas rozpoczęcia załadunku
        int onboarded = 0;                      // Licznik załadowanych pasażerów
        
        // Tymczasowa struktura do przechowywania pasażerów na pokładzie
        PassengerData rejsStruct[N2];
        int rejsCount = 0;

        // Ładujemy pasażerów dopóki jest miejsce, łódź jest aktywna i czas na załadunek
        while(rejsCount<N2 && boat2_active)
        {
            PassengerQueue *q=NULL;

            // Priorytetowo próbujemy załadować pasażerów ze skip_queue
            if(!is_Empty(&boat2_skip_queue))
            {
                q = &boat2_skip_queue;
            }
            // Jeśli nie ma pasażerów w skip_queue, ładujemy ze zwykłej kolejki
            else if(!is_Empty(&boat2_queue))
            {
                q = &boat2_queue;
            }
            else
            {
                // Jeśli obie kolejki są puste, zwalniamy mutex i ponownie blokujemy, aby sprawdzić warunki
                pthread_mutex_unlock(&m);
                pthread_mutex_lock(&m);

                // Sprawdzamy, czy łódź jest nadal aktywna i czy nie przekroczyliśmy limitu czasu na załadunek
                if(!boat2_active){break;}
                if(difftime(time(NULL),onboard_starttime)>=ONBOARD_TIMEOUT){break;}

                continue;
            }

            // Mamy kolejkę do załadunku, bierzemy pasażera z przodu
            PassengerData p = q->items[q->head];

            if(bridge2_state == FREE || bridge2_state == INCOMING)
            {
                if(bridge2_count < K2) // Sprawdzamy, czy na pomoście jest miejsce
                {
                    // Zdejmujemy pasażera z kolejki
                    remove_from_queue(q);
                    sem_post(&boat2_sem); //Zwiększamy semafor, aby kolejni pasażerowie mogli zostać dodani do kolejki
                    
                    // Pasażer wchodzi na pomost, jeśli jest miejsce i stan pomostu to INCOMING
                    if(enter_bridge2())
                    {
                        leave_bridge2();
                        //group handling
                        if(p.group > 0 && group_targetval[p.group] == 0)
                        {
                            group_targetval[p.group] = 2; //Jeśli ma grupe oraz w tablicy z wartoscia target ma 0, to zmieniamy target na 2 - zalozenie dziecko+rodzic
                        }
                        group_count[p.group]++;         //Zwiększamy group_count danej grupy
                        rejsStruct[rejsCount++] = p;    // Dodajemy pasażera do struktury rejsu
                        onboarded++;                    // Zwiększamy licznik załadowanych pasażerów
                        insta_print("\033[1;38;5;33m[BOAT2] \033[38;5;15mPassenger %d (group:%d) \033[38;5;10mentered [%d/%d]\033[0m\n", p.pid, p.group, onboarded, N2);
                    }
                }
                else
                {
                    // Jeśli pomost jest pełny, zwalniamy mutex i czekamy na miejsce
                    pthread_mutex_unlock(&m);
                    pthread_mutex_lock(&m);
                }
            }
            else
            {
                // Jeśli stan pomostu to OUTGOING, czekamy, aż się zwolni
                pthread_mutex_unlock(&m);
                pthread_mutex_lock(&m);
            }
            
            // Sprawdzamy warunki zakończenia pętli
            if(!boat2_active){break;}
            if(onboarded == N2){break;}
            if(difftime(time(NULL), onboard_starttime) >= ONBOARD_TIMEOUT){break;}
        }

        // Jeśli łódź przestaje być aktywna w trakcie załadunku, kończymy wątek
        if(!boat2_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT2] signal during onboarding.\n");
            // Wysłanie sygnału SIGRTMIN+1 do pasażerów, których nie udało się załadować
            for (int i = 0; i < rejsCount; i++)
            {
                if (kill(rejsStruct[i].realpid, SIGRTMIN+1) == -1)
                {
                    perror("[STERNIK] Couldn't send signal to passenger\n");
                    continue;
                }
            }
            break;
        }

        // Jeśli nikt nie wszedł na pokład, czekamy chwilę i powtarzamy cykl
        if(onboarded == 0)
        {
            pthread_mutex_unlock(&m);
            continue;
        }

        // Czekamy, aż pomost się zwolni (nikt nie wchodzi przed wypłynięciem)
        while((bridge2_state == INCOMING || bridge2_count > 0) && boat2_active){}

        if(!boat2_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT2] interrupted before departure.\n");
            // Wysłanie sygnału SIGRTMIN+1 do pasażerów
            for (int i = 0; i < rejsCount; i++)
            {
                if (kill(rejsStruct[i].realpid, SIGRTMIN+1) == -1)
                {
                    perror("[STERNIK] Couldn't send signal to passenger\n");
                    continue;
                }
            }
            break;
        }


        // Sprawdzenie, czy wszyscy pasażerowie z danej grupy zostali załadowani na pokład
        //Wszyscy w rejsStruct z group > 0 muszą mieć group_count[group] = 2, jeśli nie spełnione to nie płyną
        int f_allGroupsAreFine = 1;
        for(int i=0; i < rejsCount; i++) //Bierzemy i-tego pasazera i sprawdzamy powyższe warunki
        {
            PassengerData p = rejsStruct[i];
            if(p.group > 0 && group_count[p.group] < group_targetval[p.group])
            {
                f_allGroupsAreFine = 0;
                break;
            }
        }

        if(!f_allGroupsAreFine)
        {
            insta_print("[BOAT2] Missing partner from the group => canceling cruise and offboarding.\n");
            begin_outgoing2();
            for(int i=0; i<rejsCount;i++)
            {
                PassengerData pi = rejsStruct[i];
                if(pi.group > 0)
                {
                    group_count[pi.group]--; 
                }
            }
            // Wysłanie sygnału SIGRTMIN+1 do pasażerów
            for (int i = 0; i < rejsCount; i++)
            {
                if (kill(rejsStruct[i].realpid, SIGRTMIN+1) == -1)
                {
                    perror("[STERNIK] Couldn't send signal to passenger\n");
                    continue;
                }
            }
            insta_print("[BOAT2] %d passengers offboarded (incomplete group).\n", rejsCount);
            end_outgoing2();
            pthread_mutex_unlock(&m);

            continue;
        }

        // Sprawdzamy, czy jest jeszcze czas na rejs
        time_t now = time(NULL);
        if(now + T2 > end_time)
        {
            insta_print("[BOAT2] no time left for a cruise.\n");
            //offboarding
            begin_outgoing2();  // Rozpoczynamy ruch wychodzący
            for(int i=0; i < rejsCount; i++)
            {
                PassengerData pi = rejsStruct[i];
                if(pi.group > 0){group_count[pi.group]--;}
            }
            // Wysłanie sygnału SIGRTMIN+1 do pasażerów
            for (int i = 0; i < rejsCount; i++)
            {
                if (kill(rejsStruct[i].realpid, SIGRTMIN+1) == -1)
                {
                    perror("[STERNIK] Couldn't send signal to passenger\n");
                    continue;
                }
            }
            insta_print("[BOAT2] %d passengers offboarded (end of time).\n",rejsCount);
            end_outgoing2();    // Kończymy ruch wychodzący
            pthread_mutex_unlock(&m);
            break;
        }

        // Rozpoczynamy rejs
        boat2_cruising = 1;
        insta_print("\033[1;38;5;33m[BOAT2] \033[38;5;10mDeparting with \033[1;38;5;15m%d\033[38;5;15m passengers.\033[0m\n", rejsCount);
        pthread_mutex_unlock(&m);

        sleep(T2); // Symulacja czasu trwania rejsu

        // Wracamy po rejsie, rozpoczynamy offboarding
        pthread_mutex_lock(&m);
        boat2_cruising = 0;
        insta_print("[BOAT2] Cruise finished => offboarding.\n");
        begin_outgoing2();

        // Wysłanie sygnału SIGRTMIN do pasażerów, którzy odbyli rejs
        for (int i = 0; i < rejsCount; i++)
        {
            if (kill(rejsStruct[i].realpid, SIGRTMIN) == -1)
            {
                perror("[STERNIK] Couldn't send signal to passenger\n");
                continue;
            }
        }
        insta_print("[BOAT2] Passengers left.\n");
        end_outgoing2();

        // Sprawdzamy, czy łódź została wyłączona po zakończeniu rejsu
        if(!boat2_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT2] signal executed after offboarding\n");
            break;
        }
        pthread_mutex_unlock(&m);
        
    }

    // Kończenie działania wątku, ustawienie łodzi na nieaktywną
    pthread_mutex_lock(&m);
    boat2_active = 0;
    pthread_mutex_unlock(&m);
    insta_print("[BOAT2] THREAD END.\n");
    return NULL;
}

// --- MAIN FUNCTION ---
// Główna funkcja programu, która ustawia czas działania, obsługuje sygnały, 
// tworzy gniazdo "serwera" i uruchamia wątki do obsługi łodzi.
int main(int argc, char*argv[])
{
    setbuf(stdout,NULL);  // Ustawienie, by stdout nie był buforowany (istotne dla natychmiastowego wypisywania)

    if(argc<2) // Sprawdzenie, czy został przekazany argument dla czasu (timeout)
    {
        fprintf(stderr,"USED: %s [timeout_s]\n",argv[0]); 
        return 1;
    }

    if(N1 == 0 | N2 == 0 | K1 == 0 | K2 == 0){printf("Boat capacity, bridge capacity > 0\n");kill(getppid(), SIGRTMIN + 2);}
    
    // Ustawienie wartości timeout na podstawie argumentu
    int timeout_val = atoi(argv[1]);
    start_time = time(NULL);
    end_time = start_time + timeout_val;

    // --- SIGNAL HANDLERS ---
    struct sigaction sig1 = {0}, sig2 = {0};

    // Ustawienie obsługi sygnału SIGUSR1
    sig1.sa_handler = SIGUSR1_handler;
    if(sigaction(SIGUSR1, &sig1, NULL) == -1)
    {
        perror("[ERROR] Failed to set SIGUSR1 handler");
        exit(1);  // Zakończenie programu w przypadku błędu
    }

    // Ustawienie obsługi sygnału SIGUSR2
    sig2.sa_handler = SIGUSR2_handler;
    if(sigaction(SIGUSR2, &sig2, NULL) == -1)
    {
        perror("[ERROR] Failed to set SIGUSR2 handler");
        exit(1);  // Zakończenie programu w przypadku błędu
    }

    // --- INICJALIZACJA KOLEJEK ---

    // Kolejki dla boat1
    init_queue(&boat1_queue);
    init_queue(&boat1_skip_queue);

    // Kolejki dla boat2
    init_queue(&boat2_queue);
    init_queue(&boat2_skip_queue);

    init_semaphores();  // Inicjalizacja semaforów
    init_bridges();     // Inicjalizacja pomostów

    // Usunięcie gniazda, jeśli istnieje
    unlink(STERNIK_SOCKET_PATH);

    // Tworzenie gniazda do komunikacji
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sock_fd < 0)
    {
        perror("[STERNIK] Error creating socket");
        return 1;
    }
    // Przygotowanie adresu gniazda
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, STERNIK_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Powiązanie gniazda z adresem
    if(bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[STERNIK] Error binding socket");
        close(sock_fd);
        return 1;
    }

    // Nasłuchiwanie połączeń przychodzących na gnieździe
    if(listen(sock_fd, 5) < 0)
    {
        perror("[STERNIK] Error listening on socket");
        close(sock_fd);
        return 1;
    }
    insta_print("[STERNIK] START (TIMEOUT = %ds).\n", timeout_val);

    // Tworzenie wątków
    pthread_t thread1, thread2;

    if(pthread_create(&thread1, NULL, boat1_thread, NULL) != 0)
    {
        perror("pthread_create boat1_thread failed");
        return 1;
    }

    if(pthread_create(&thread2, NULL, boat2_thread, NULL) != 0)
    {
        perror("pthread_create boat2_thread failed");
        return 1;
    }

    // Pętla do przetwarzania żądań pasażerów
    while(!terminate_sternik && (boat1_active == 1 || boat2_active == 1))
    {
        int client_sock_fd = accept(sock_fd, NULL, NULL); // Akceptowanie połączenia klienta
        if(client_sock_fd < 0)
        {
            if (errno == EINTR)
            {
                // Jeśli accept został przerwany przez sygnał, kontynuuj pętlę
                close(client_sock_fd);
                continue;
            }
            perror("[STERNIK] Error accepting connection");
            continue;
        }
        process_passenger_request(client_sock_fd);
        close(client_sock_fd);
    }

    // Zatrzymanie obu łodzi
    pthread_mutex_lock(&m);
    boat1_active = 0;
    boat2_active = 0;
    pthread_mutex_unlock(&m);
    
    // Czekanie na zakończenie pracy wątków
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    // Zamknięcie gniazda i zniszczenie semaforów
    close(sock_fd);
    unlink(STERNIK_SOCKET_PATH);
    sem_destroy(&boat1_sem);
    sem_destroy(&boat2_sem);
    insta_print("\033[38;5;10m[STERNIK] Service ended.\033[0m\n");
    kill(getppid(), SIGRTMIN + 2);
    return 0;
}