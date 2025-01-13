#include "globals.h"

void passenger_cycle();
void create_passengers();
void wait_passengers();
void enter_molo();
void leave_molo();
void leave_ticketqueue();
int puchase_process(int age); // communication with cashier in order to buy a ticket, returns decision from cashier - if ticket has been provided or not
int generate_random_age();
