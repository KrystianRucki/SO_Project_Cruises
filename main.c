#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#include "globals.h"
#include "passenger.h"
#include "cashier.h"

int main()
{
    init_sem();
    share_var();
    init_var();
    start_time_manager();

    create_cashier(); // policjant po cashier, dzieki temu mamy pewnosc ze is_cashier_open == TRUE - pierwsza iteracja petli
    create_passengers();
    wait_passengers();
    wait_cashier();

    destroy_var();
    destroy_sem();

    return 0;
}