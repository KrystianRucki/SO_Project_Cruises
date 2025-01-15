#include "cashier.h"

void cashier_cycle() //cashier uruchamiany przed passenger
{
    srand(time(NULL));
    //kasa dziala w czasie Tp Tk, do Tk sprzedaje bilety,
    //dziala takze pod warunkiem ze przynajmniej jedna lodz state = 1 i jest czas
    while(1)
    {
        *status = is_cashier_open();
        printf("cashier status:%d\n",*status);
        if(*status == 0)
        {
            printf("Dear Passenger, No more trips for today.\n"); //przy testowaniu wyszlo ze wysyla decision ale akurat nikogo nie bylo w komunikacji
            int decision = 0;
            write(cashier_passenger[1],&decision, sizeof(int)); //w teorii obecny pasazer ktory sie komunikuje
            break; //debug na razie
            //mozliwe ze zbedne
            // global time: 10
            // Passenger 8573 (age: 50) entered the molo.
            // selected:1
            // Ticket granted. Enjoy your trip!
            // Passenger 8573 bought a ticket successfully.
            // Passenger 8573 left molo
            // Dear Passenger, No more trips for today.
            //wychodzi na to, ze jak skonczy sie czas to tam ktos i tak trzyma komunikacje, zostanie obsluzony bo status jest jeszcze 1 w tej iteracji
            //wiec tutaj nie wchodzi, nastepna iteracja wchodzi do tej sekcji poniewaz status = 0; tylko ze ten write do nikogo juz nie trafia
            //^powyzsze jest tylko przy pilnowaniu czasu w is_cashier_opened();


        }
        
        // if(sem_trywait(passcash_pipe_lock) == 0)
        // {
            //printf("sprawdzam czy pipe lock prze try zanim pasazer zlapie lock\n");
            // printf("wszedl do semtrywait\n");
            if(sem_trywait(read_ready) == 0){
                process_passenger();
            }
            
        // }
        
        sleep(1);
    }
    //ci co wejda juz za kase i nastapi zamkniecie czasu badz lodek, to sa traktowani jako ostatnia wycieczka i ogranizuja taka wycieczke,
    //chyba ze lodzie nie plyna akurat juz bo poszedl sygnal -- rozpisane w .docx trzeba wprowadzic te mechanizmy kontroli przy etapie lodek
    
    //ewentualne dodatkowe rzeczy tu
}

void process_passenger()
{
    int age, decision, selected_boat; //wybierana lodz powinna byc po stronie pasazera wiec ewentualnie przerobic, na losowanie w pasazeru i przekazanie przez pipe
    close(cashier_passenger[0]);
    close(passenger_cashier[1]);

    // if(sem_trywait(passcash_pipe_lock) == -1)
    // {
        printf("processing passenger, czeka na odczyt\n");
        if(read(passenger_cashier[0], &age, sizeof(int)) == -1)
        {
            perror("read from passenger failed");
            exit(1);
        }
    // }

    
    // Warunki przyznania biletu - zastapic case albo sprawdzac tu tylko wiek albo czy dziecko, mniej niz 3 nie placi za bilet czyli by przechodzil po prostu ale nastolatek juz placi w teorii,
    //albo sprawdzac tu tylko wiek i na tej podstawie decision 0 1 2
    //a boat_state pozniej przy ustawianiu do kolejek itd
    if (age < 15 || age > 70) //child or old
    {
        //printf("Dear Passenger, No more trips available for your age group.\n");
        decision = 2;
        printf("selected:2-c\n");
        //dziecko bedzie watkiem wiec bedzie musial przekazac swoj wiek do parenta swojego a parent tutaj, albo po prostu zmienna has child czy cos podobnego i wtedy nie trzeba robic dodatkowej komunikacji - tutaj porownamy czy ma dziecko czy jest starszy niz 70
    }
    else if ((age >=15 && age<=70))
    {
        // losujemy lodke 1 lub 2 w jakis sposob
        //if(nr lodki i dostepna) -> decision 1 lub 2
        selected_boat = rand() % 2 + 1;
        printf("selected:%d\n",selected_boat);

        if(selected_boat == 1)// && *boat_state1 == 1)
        {
            decision = 1;
        }
        else
        {
            decision = 2;
        }
        printf("Ticket granted. Enjoy your trip!\n");
    }

    if(write(cashier_passenger[1], &decision, sizeof(int)) == -1)
    {
        perror("write to passenger failed");
        exit(1);
    }

    close(passenger_cashier[1]);
    close(cashier_passenger[0]);
}

bool is_cashier_open()
{

    if (*boat_state1 == 0 && *boat_state2 == 0)
    {
        // Obie łódki niedostępne - kasa zamknieta - pasażer nie płynie
        printf("Dear Passengers, No more trips for today.\n");
        //mechanizm, ktory przekaze ta informacje takze reszcie kolejki i zapewni ze opuszcza kolejke
        //kasa zamknieta reszta osob w kolejce tez ma wyjsc i opuscic both; queue and molo - implementacja here
        //musi zwracac tez decision 0 ale to juz w cashier_cycle()
        return false;
    }

    sem_wait(time_lock);
    if(*current_time > Tk)
    {
        return false;
    }
    sem_post(time_lock);
    
    return true;
}

void create_cashier()
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("cashier fork failed");
        exit(1);
    }
    else if (pid == 0)
    {
        cashier_cycle();
        exit(0);
    }
}
void wait_cashier()
{
    if(wait(0) < 0)
    {
        perror("cashier wait failed");
    }
}