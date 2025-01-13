#include "cashier.h"

void cashier_cycle() //cashier uruchamiany przed passenger
{
    //kasa dziala w czasie Tp Tk, do Tk sprzedaje bilety, 
    //dziala takze pod warunkiem ze przynajmniej jedna lodz state = 1, nie zostala zatrzymana przez policjanta
    //zmienna na to czy otwarta kasa
    //obecny czas time(null)
    do
    {
        //funkcja sprawdzjaca czy nadal ma byc otwarta kasa - przypisuje wynik 0 lub 1 do tej zmieniej "czy otwarta kasa", ta zmienna jako warunek petli, 
        //jak f zwroci 0, to nie obsluguje pasazera czyli przekazuje decision 0 do pasazera obecnego on sobie opusci queue dzieki funkcji w passenger.c
        //jak nie ma lodek ale jest czas to nie obsluguje pasazerow, obecny dostanie decision 0 a reszta w kolejce musi jakos tez dostac ta informacje, i tak samo reszta ma opuscic molo
        //jak nie ma czasu (warunek konieczny) to decision dostane obecny pasazer, reszta tez musi i tak samo reszta musi opuscic molo
        //jesli te warunki na otwarta kase sa spelnione to procesuje pasazera i tam sprawdzam warunek na decision czyli, czy dostanie bilet na podstawie wieku i dostepnych lodek
        process_passenger();
        //sleep(1);
    } while (1);
    
    //ewentualne dodatkowe rzeczy tu
}

void process_passenger()
{
//passenger_cashier[2]; // P => C, 0 - read, 1 - write fd
//cashier_passenger[2]; // C => P
    int age, decision;
    close(cashier_passenger[0]);
    close(passenger_cashier[1]);

    if(read(passenger_cashier[0], &age, sizeof(int)) == -1)
    {
        perror("read from passenger failed");
        exit(1);
    }
    //warunki na bilet
    //ustawia decision
    if(write(cashier_passenger[1], &decision, sizeof(int)) == -1)
    {
        perror("write to passenger failed");
        exit(1);
    }

    close(passenger_cashier[1]);
    close(cashier_passenger[0]);
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