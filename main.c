#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#include "globals.h"
#include "passenger.h"

int main()
{
    init_sem();
    share_var();
    init_var();

    //create kasjer
    create_passengers();
    wait_passengers();

    destroy_var();
    destroy_sem();
    
    return 0;
}