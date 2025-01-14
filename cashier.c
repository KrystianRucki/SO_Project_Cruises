#include "cashier.h"
#include <stdbool.h>
void cashier_cycle() //cashier uruchamiany przed passenger
{
    srand(time(NULL));
    //kasa dziala w czasie Tp Tk, do Tk sprzedaje bilety,
    //dziala takze pod warunkiem ze przynajmniej jedna lodz state = 1, nie zostala zatrzymana przez policjanta
    while(1)
    {
        *status = is_cashier_open();
        if(*status == 0)
        {
            printf("Dear Passenger, No more trips for today.\n");
            int decision = 0;
        }
    }
    while(is_cashier_open())
    {
        process_passenger();
    }
    do
    {
        //is_cashier_open() - sprawdza czas i czy jest chociaz jedna lodka (czyli jak obie nieodstepne - bedzie closed)

        //funkcja sprawdzjaca czy nadal ma byc otwarta kasa - przypisuje wynik 0 lub 1 do tej zmieniej "czy otwarta kasa", ta zmienna jako warunek petli, 
        //jak f zwroci 0, to nie obsluguje pasazera czyli przekazuje decision 0 do pasazera obecnego on sobie opusci queue dzieki funkcji w passenger.c
        //jak nie ma lodek ale jest czas to nie obsluguje pasazerow, obecny dostanie decision 0 a reszta w kolejce musi jakos tez dostac ta informacje, i tak samo reszta ma opuscic molo
        //jak nie ma czasu (warunek konieczny) to decision dostane obecny pasazer, reszta tez musi i tak samo reszta musi opuscic molo
        //jesli te warunki na otwarta kase sa spelnione to procesuje pasazera i tam sprawdzam warunek na decision czyli, czy dostanie bilet na podstawie wieku i dostepnych lodek
        process_passenger(); //ci co wejda juz za kase i nastapi zamkniecie czasu badz lodek, to sa traktowani jako ostatnia wycieczka i ogranizuja taka wycieczke, chyba ze lodzie nie plyna akurat juz bo poszedl sygnal -- rozpisane w .docx trzeba wprowadzic te mechanizmy kontroli przy etapie lodek
        //sleep(1);
    } while (1);
    
    //ewentualne dodatkowe rzeczy tu
}

void process_passenger()
{
    int age, decision, selected_boat; //wybierana lodz powinna byc po stronie pasazera wiec ewentualnie przerobic, na losowanie w pasazeru i przekazanie przez pipe
    close(cashier_passenger[0]);
    close(passenger_cashier[1]);

    if(read(passenger_cashier[0], &age, sizeof(int)) == -1)
    {
        perror("read from passenger failed");
        exit(1);
    }
    
    // Warunki przyznania biletu - zastapic case albo sprawdzac tu tylko wiek albo czy dziecko, mniej niz 3 nie placi za bilet czyli by przechodzil po prostu ale nastolatek juz placi w teorii,
    
    //albo sprawdzac tu tylko wiek i na tej podstawie decision 0 1 2
    //a boat_state pozniej przy ustawianiu do kolejek itd
    if ((age < 15 || age > 70) && *boat_state2 == 0)
    {
        // Wiek pasażera poza przedziałem 15-70, a łódka nr 2 jest niedostępna - pasażer nie płynie
        printf("Dear Passenger, No more trips available for your age group.\n");
        decision = 0;
            // dziecko bedzie watkiem wiec bedzie musial przekazac swoj wiek do parenta swojego a parent tutaj, albo po prostu zmienna has child czy cos podobnego i wtedy nie trzeba robic dodatkowej komunikacji - tutaj porownamy czy ma dziecko czy jest starszy niz 70 i boat_state2 == 0
    }
    else if ((age >=15 && age<=70))
    {
        // losujemy lodke 1 lub 2 w jakis sposob
        //if(nr lodki i dostepna) -> decision 1 lub 2

        // Pasażer spełnia warunki i przynajmniej jedna łódka jest dostępna - może płynąć
        selected_boat = rand() % 2 + 1;
        if(selected_boat == 1 && *boat_state1 == 1)
        {
            decision = 1;
        }
        printf("Ticket granted. Enjoy your trip!\n");
        decision = 1; //tutaj mozna ktora lodka to bedzie albo decision to po prostu 0 1 2 - nie, boat1, boat2
    }

    //ustawia decision i trzeba tez ustawic jakas zmienna ktora informuje pozniej do jakiej kolejki sie ma ustawic (do lodki 1 lub lodki 2)
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