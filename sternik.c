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
#include <stdarg.h> //zmienna ilosc elemetow do insta_print
#include <sys/socket.h> // Operacje na gniazdach
#include <sys/un.h>     // Struktury i funkcje dla gniazd domeny Unix
#include <semaphore.h>

#define STERNIK_SOCKET_PATH "/tmp/sternik_socket"
#define MAX_BUFFER_SIZE 256
int terminate_sternik = 0;  // Flaga kontrolująca zakończenie programu

//BOAT1
#define N1 6
#define T1 3

//BOAT2
#define N2 8
#define T2 7

//jesli group bedzie > 0, to obslugujemy grupe, zalozeniem jest ze dziecko+opiekun
#define GROUP_MAX 30000 
static int group_targetval[GROUP_MAX] = {0}; //target wielkosci grupy
static int group_count[GROUP_MAX] = {0}; //sledzenie ile osob z konkretnej grupy juz zaladowalismy


#define ONBOARD_TIMEOUT 6 //czas zaladunku
#define K 3 //limit pomostu

//Dlugosc kolejki
#define QUEUE_SIZE 30
// Semafory do synchronizacji kolejki
sem_t boat1_sem;
sem_t boat2_sem;
void init_semaphores() {
    sem_init(&boat1_sem, 0, QUEUE_SIZE);  // Inicjalizacja semafora dla boat1
    sem_init(&boat2_sem, 0, QUEUE_SIZE);  // Inicjalizacja semafora dla boat2
    //dodaj obsluge bledow
}
// --- Struktura pasazera ---
typedef struct
{
    int pid;
    int discount;
    int group;
} PassengerData;

// --- Struktura kolejki pasazerow---
typedef struct
{
    PassengerData items[QUEUE_SIZE];
    int head, tail;
    int count;
} PassengerQueue;

static void init_queue(PassengerQueue *q)
{
    q->head=0;
    q->tail=0;
    q->count=0;
}

static int is_Empty(PassengerQueue *q){return q->count == 0;}

static int is_Full(PassengerQueue *q){return q->count == QUEUE_SIZE;}

static int add_to_queue(PassengerQueue *q, PassengerData itm)
{
    if(is_Full(q)){return -1;}

    q->items[q->tail] = itm;
    q->tail = (q->tail+1) % QUEUE_SIZE;
    q->count++;

    return 0;
}
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

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

// --- Funkcja do natychmiastowego printowania ---
static void insta_print(const char *message, ...)
{
    va_list ap;
    va_start(ap, message);             // Inicjalizacja listy argumentow zmiennych
    vfprintf(stdout, message, ap);     // Wypisanie na stdout z formatowaniem
    fflush(stdout);                    // Natychmiastowe oproznienie bufora
    va_end(ap);                        // Zakonczenie przetwarzania listy argumentow
}

void process_passenger_request(int client_sock_fd)
{
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read = read(client_sock_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read < 0)
    {
        perror("[STERNIK] Error reading from client socket");
        return;
    }

    buffer[bytes_read] = '\0'; // Null-terminate the string
    int pid, age, discount, grp;
    
if(strncmp(buffer, "SKIP_QUEUE", 10) == 0)
{
        // Odbieranie SKIP_QUEUE
        if(sscanf(buffer, "SKIP_QUEUE %d %d %d %d", &pid, &age, &discount, &grp) == 4)
        {
            PassengerData pass_i = {pid, discount, grp};
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
            }

            pthread_mutex_unlock(&m);
        }
    }
    else if(strncmp(buffer, "QUEUE", 5) == 0)
    {
        // Odbieranie QUEUE
        if(sscanf(buffer, "QUEUE %d %d %d %d", &pid, &age, &discount, &grp) == 4)
        {
            PassengerData pass_i = {pid, discount, grp};
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
                insta_print("[STERNIK] Passenger %d => boat1 discount:%d%%\n", pid, discount);
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
                insta_print("[STERNIK] Passenger %d => boat2 discount:%d%%\n", pid, discount);
            }
            else
            {
                insta_print("[STERNIK] BOAT%d not active => REJECTED %d\n", boat_nr, pid);
            }

            pthread_mutex_unlock(&m);
        }
    }
    else if(strncmp(buffer, "QUIT", 4) == 0)
    {
        // Odbieranie QUIT
        insta_print("[STERNIK] Terminating. (QUIT RECEIVED)\n");
        terminate_sternik = 1;  // Ustawienie flagi zakończenia serwera
    }
    else
    {
        printf("[STERNIK] UNKNOWN: %s\n", buffer);
    }
}

// --- Pomost ---
static int bridge_count; //liczba osob na pomoscie

typedef enum 
{
    INCOMING, OUTGOING, FREE //stany pomostu - ruch przychodzacy(onboarding) - ruch wychodzacy(offboarding) - wolny
} BridgeState; 

static BridgeState bridge_state;


static pthread_cond_t cond_bridge_free = PTHREAD_COND_INITIALIZER;

// --- Funkcje dla pomostu ---
static void init_bridge()
{
    bridge_count = 0;
    bridge_state = FREE;
}

static int enter_bridge()
{
    if(bridge_state == FREE)
    {
        bridge_state = INCOMING;
    }
    else if(bridge_state != INCOMING)
    {
        return 0;
    }

    if(bridge_count >= K){return 0;}
    bridge_count++;
    return 1;
}

static void leave_bridge()
{
    bridge_count--;
    if(bridge_count == 0)
    {
        bridge_state = FREE;
        pthread_cond_broadcast(&cond_bridge_free); //wybudzanie watkow, ktore czekaja na przejscie pomostu w stan FREE
    }
}

static void begin_outgoing()
{
    while(bridge_state != FREE)
    {
        pthread_cond_wait(&cond_bridge_free, &m);
    }
    bridge_state = OUTGOING;
}
static void end_outgoing()
{
    bridge_state = FREE;
    pthread_cond_broadcast(&cond_bridge_free);
}



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
    insta_print("[BOAT1] START N1 = %d T1 = %ds.\n", N1, T1);

    while (1)
    {
        //blokujemy mutex - sprawdzamy aktywnosc boat
        pthread_mutex_lock(&m);
        if (!boat1_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT1] boat1_active = 0, END\n");
            break;
        }

        //sprawdzamy czy kolejki sa puste
        if (is_Empty(&boat1_skip_queue) && is_Empty(&boat1_queue))
        {
            pthread_mutex_unlock(&m);
            // usleep(50000);
            continue;
        }

        insta_print("[BOAT1] Onboarding...\n");
        time_t onboard_starttime = time(NULL);
        int onboarded = 0;
        
        //Tymczasowa struktura pasazerow na potrzebe rejsu - maksymalnie: N1 pasazerow
        PassengerData rejsStruct[N1];
        int rejsCount = 0;

        //ladujemy pasazerow dopoki jest miejsce i lodz jest aktywna (oraz dopoki tez jest czas na zaladunek - mozna odplynac nie zapelniajac calej lodzi)
        while (rejsCount < N1 && boat1_active)
        {
            PassengerQueue *q = NULL;

            //Priorytet na kolejke skip
            if (!is_Empty(&boat1_skip_queue))
            {
                q = &boat1_skip_queue;
            } 
            //jesli nikogo nie ma w "skip queue" to bierzemy zwykla kolejke
            else if (!is_Empty(&boat1_queue))
            {
                q = &boat1_queue;
            }
            else
            {
                //obie kolejki sa puste - odblokowujemy mutex na chwile
                pthread_mutex_unlock(&m);
                // usleep(50000);
                pthread_mutex_lock(&m);

                //Sprawdzamy czy lodz jest nadal aktywna
                if(!boat1_active){break;}

                //Sprawdzamy czas zaladunku
                if(difftime(time(NULL), onboard_starttime) >= ONBOARD_TIMEOUT){break;}

                continue;
            }

            //tutaj juz mamy dana kolejke q, pasazer na przodzie kolejki probuje wsiasc
            PassengerData p = q->items[q->head];

            if (bridge_state == FREE || bridge_state == INCOMING)
            {
                if (bridge_count < K)
                {
                    //jest miejsce, sciagamy pasazera z kolejki
                    remove_from_queue(q);
                    sem_post(&boat1_sem);
                    //wejdzie na pomost - jesli jest miejsce i INCOMING
                    if (enter_bridge())
                    {
                        //zajal pomost, zwalniamy go od razu - symulacja ze przeszedl wiec mozna zwolnic
                        leave_bridge();
                        rejsStruct[rejsCount++] = p; //dodajemy go do struktury rejsu
                        onboarded++;
                        insta_print("[BOAT1] Passenger %d (discount:%d%%) entered [%d/%d]\n", p.pid, p.discount, onboarded, N1);
                    }
                }
                else
                {
                    //pomost jest pelny
                    pthread_mutex_unlock(&m);
                    // usleep(50000);
                    pthread_mutex_lock(&m);
                }
            }
            else
            {
                //stan OUTGOING, czekamy az sie zwolni
                pthread_mutex_unlock(&m);
                // usleep(50000);
                pthread_mutex_lock(&m);
            }

            //sprawdzamy warunki zakonczenia petli
            if (!boat1_active){break;}
            if (onboarded == N1){break;}
            if (difftime(time(NULL), onboard_starttime) >= ONBOARD_TIMEOUT){break;}
        }

        //Jesli lodz przestanie byc active w trakcie onboardingu => trzeba zakonczyc
        if (!boat1_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT1] signal during onboarding.\n");
            break;
        }

        //Jesli nikt nie wsiadl, czekamy chwile i powtarzamy cykl petli
        if (onboarded == 0)
        {
            pthread_mutex_unlock(&m);
            // usleep(50000);
            continue;
        }
        
        while ((bridge_state == INCOMING || bridge_count > 0) && boat1_active) // Musimy zaczekac, az pomost sie zwolni - nikt nie wchodzi przed wyplynieciem
        {
            pthread_cond_wait(&cond_bridge_free, &m);
        }
        if (!boat1_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT1] interrupted before departure.\n");
            break;
        }

        //sprawdzamy czy jest jeszcze czas na rejs
        time_t now = time(NULL);
        if (now + T1 > end_time)
        {
            insta_print("[BOAT1] no time left for a cruise.\n");
            begin_outgoing();
            insta_print("[BOAT1] Passengers left.\n");
            end_outgoing();
            pthread_mutex_unlock(&m);
            break;
        }

        //mozna wyplynac
        boat1_cruising = 1;
        insta_print("[BOAT1] Departing with %d passengers.\n", rejsCount);
        pthread_mutex_unlock(&m);

        sleep(T1); //symulacja rejsu

        //wracamy z rejsu, OUTGOING - nalezy wyladowac pasazerow
        pthread_mutex_lock(&m);
        boat1_cruising = 0;
        insta_print("[BOAT1] Cruise finished => OUTGOING.\n"); //ruch wychodzacy (offboarding)
        begin_outgoing();
        insta_print("[BOAT1] Passengers left.\n");
        end_outgoing();

        if (!boat1_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT1] signal executed after offboarding\n"); //sygnal po wyladunku
            break;
        }
        pthread_mutex_unlock(&m);

        // usleep(50000);
    }
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
    insta_print("[BOAT2] START N2 = %d T2 = %ds.\n",N2,T2);

    while(1)
    {
        pthread_mutex_lock(&m);
        if(!boat2_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT2] boat2_active = 0, END\n");
            break;
        }
        if(is_Empty(&boat2_skip_queue) && is_Empty(&boat2_queue))
        {
            pthread_mutex_unlock(&m);
            // usleep(50000);
            continue;
        }
        
        insta_print("[BOAT2] Onboarding...\n");
        time_t onboard_starttime = time(NULL);
        int onboarded = 0;
        
        //Tymczasowa struktura pasazerow na potrzebe rejsu - maksymalnie: N2 pasazerow
        PassengerData rejsStruct[N2];
        int rejsCount = 0;

        while(rejsCount<N2 && boat2_active)
        {
            PassengerQueue *q=NULL;

            //Priorytet na kolejke skip
            if(!is_Empty(&boat2_skip_queue))
            {
                q = &boat2_skip_queue;
            }
            //jesli nikogo nie ma w "skip queue" to bierzemy zwykla kolejke
            else if(!is_Empty(&boat2_queue))
            {
                q = &boat2_queue;
            }
            else
            {
                //obie kolejki sa puste - odblokowujemy mutex na chwile
                pthread_mutex_unlock(&m);
                // usleep(50000);
                pthread_mutex_lock(&m);

                //Sprawdzamy czy lodz jest nadal aktywna
                if(!boat2_active){break;}

                //Sprawdzamy czas zaladunku
                if(difftime(time(NULL),onboard_starttime)>=ONBOARD_TIMEOUT){break;}

                continue;
            }

            //tutaj juz mamy dana kolejke q, pasazer na prodzie kolejki probuje wsiasc
            PassengerData p = q->items[q->head];

            if(bridge_state == FREE || bridge_state == INCOMING)
            {
                if(bridge_count < K)
                {
                    //jest miejsce, sciagamy pasazera z kolejki
                    remove_from_queue(q);
                    sem_post(&boat2_sem);
                    //wejdzie na pomost - jesli jest miejsce i INCOMING
                    if(enter_bridge())
                    {
                        //zajal pomost, zwalniamy go od razu - symulacja ze przeszedl wiec mozna zwolnic
                        leave_bridge();

                        //group handling
                        if(p.group > 0 && group_targetval[p.group] == 0)
                        {
                            group_targetval[p.group] = 2; //jesli ma grupe oraz w tablicy z wartoscia target ma 0, to zmieniamy target na 2 - zalozenie dziecko+rodzic
                        }
                        group_count[p.group]++; 
                        rejsStruct[rejsCount++] = p;
                        onboarded++;
                        insta_print("[BOAT2] Passenger %d (discount:%d%% group:%d) entered [%d/%d]\n", p.pid, p.discount, p.group, onboarded, N2);
                    }
                }
                else
                {
                    //pomost jest pelny
                    pthread_mutex_unlock(&m);
                    // usleep(50000);
                    pthread_mutex_lock(&m);
                }
            }
            else
            {
                //stan OUTGOING, czekamy az sie zwolni
                pthread_mutex_unlock(&m);
                // usleep(50000);
                pthread_mutex_lock(&m);
            }
            
            //sprawdzamy warunki zakonczenia petli
            if(!boat2_active){break;}
            if(onboarded == N2){break;}
            if(difftime(time(NULL), onboard_starttime) >= ONBOARD_TIMEOUT){break;}
        }

        //Jesli lodz przestanie byc active w trakcie onboardingu => trzeba zakonczyc
        if(!boat2_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT2] signal during onboarding.\n");
            break;
        }

        //Jesli nikt nie wsiadl, czekamy chwile i powtarzamy cykl petli
        if(onboarded == 0)
        {
            pthread_mutex_unlock(&m);
            // usleep(50000);
            continue;
        }

        while((bridge_state == INCOMING || bridge_count > 0) && boat2_active) // Musimy zaczekac, az pomost sie zwolni - nikt nie wchodzi przed wyplynieciem
        {
            pthread_cond_wait(&cond_bridge_free, &m);
        }

        if(!boat2_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT2] interrupted before departure.\n");
            break;
        }

        //sprawdzenie czy mamy wszystkie osoby z danej grupy
        //wszyscy w rejsStruct: group>0 --> group_count[group] = 2, jesli nie spelnione to probujemy zaladowac brakujaca osobe
        int f_allGroupsAreFine = 1;
        for(int i=0; i < rejsCount; i++) //bierzemy i-tego pasazera i sprawdzamy powyzsze warunki
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
            //tymczasowe wyladowanie wszystkich
            begin_outgoing();
            insta_print("[BOAT2] %d passengers offboarded (incomplete group).\n", rejsCount);
            for(int i=0; i<rejsCount;i++)
            {
                PassengerData pi = rejsStruct[i];
                if(pi.group > 0)
                {
                    group_count[pi.group]--; 
                }
            }
            end_outgoing();
            pthread_mutex_unlock(&m);
            // usleep(500000); //dajemy czas na zaladunek brakujacej osoby

            continue;
        }

        //sprawdzamy czy jest jeszcze czas na wyplyniecie
        time_t now = time(NULL);
        if(now + T2 > end_time)
        {
            insta_print("[BOAT2] no time left for a cruise.\n");
            //offboarding
            begin_outgoing();
            insta_print("[BOAT2] %d passengers offboarded (end of time).\n",rejsCount);
            for(int i=0; i < rejsCount; i++)
            {
                PassengerData pi = rejsStruct[i];
                if(pi.group > 0){group_count[pi.group]--;}
            }
            end_outgoing();
            pthread_mutex_unlock(&m);
            break;
        }

        //mozna wyplynac
        boat2_cruising = 1;
        insta_print("[BOAT2] Departing with %d passengers.\n", rejsCount);
        pthread_mutex_unlock(&m);

        sleep(T2); //symulacja rejsu

        //wracamy z rejsu, OUTGOING - nalezy wyladowac pasazerow
        pthread_mutex_lock(&m);
        boat2_cruising = 0;
        insta_print("[BOAT2] Cruise finished => OUTGOING.\n");
        begin_outgoing();
        insta_print("[BOAT2] Passengers left.\n");
        end_outgoing();

        if(!boat2_active)
        {
            pthread_mutex_unlock(&m);
            insta_print("[BOAT2] signal executed after offboarding\n");
            break;
        }
        pthread_mutex_unlock(&m);
        
        // usleep(50000);
    }

    pthread_mutex_lock(&m);
    boat2_active = 0;
    pthread_mutex_unlock(&m);
    insta_print("[BOAT2] THREAD END.\n");
    return NULL;
}

//--- MAIN ---
int main(int argc, char*argv[])
{
    setbuf(stdout,NULL);

    if(argc<2)
    {
        fprintf(stderr,"USED: %s [timeout_s]\n",argv[0]);
        return 1;
    }

    int timeout_val = atoi(argv[1]);
    start_time = time(NULL);
    end_time = start_time + timeout_val;

    // SIGNALS
    struct sigaction sig1 = {0}, sig2 = {0};

    sig1.sa_handler = SIGUSR1_handler;
    sigaction(SIGUSR1, &sig1, NULL);

    sig2.sa_handler = SIGUSR2_handler;
    sigaction(SIGUSR2, &sig2, NULL);

    // Kolejki dla boat1
    init_queue(&boat1_queue);
    init_queue(&boat1_skip_queue);

    // Kolejki dla boat2
    init_queue(&boat2_queue);
    init_queue(&boat2_skip_queue);
    init_semaphores();
    init_bridge();

    // Usunięcie istniejącego gniazda
    unlink(STERNIK_SOCKET_PATH);

    // Tworzenie gniazda do komunikacji
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sock_fd < 0)
    {
        perror("[STERNIK] Error creating socket");
        return 1;
    }
    // Adres gniazda
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, STERNIK_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Powiązanie gniazda
    if(bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[STERNIK] Error binding socket");
        close(sock_fd);
        return 1;
    }

    // Nasłuchiwanie połączeń
    if(listen(sock_fd, 5) < 0)
    {
        perror("[STERNIK] Error listening on socket");
        close(sock_fd);
        return 1;
    }
    insta_print("[STERNIK] START (TIMEOUT = %d).\n", timeout_val);
    pthread_t thread1, thread2;
    pthread_create(&thread1, NULL, boat1_thread, NULL);
    pthread_create(&thread2, NULL, boat2_thread, NULL);

    while(!terminate_sternik)
    {
        int client_sock_fd = accept(sock_fd, NULL, NULL);
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


    pthread_mutex_lock(&m);
    boat1_active = 0;
    boat2_active = 0;
    pthread_mutex_unlock(&m);
    
    //oczekiwanie na zakonczenie pracy watkow
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    close(sock_fd);
    sem_destroy(&boat1_sem);
    sem_destroy(&boat2_sem);
    insta_print("[STERNIK] Service ended.\n");

    return 0;
}