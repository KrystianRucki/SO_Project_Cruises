#include "globals.h"

void passenger_cycle();
void create_passengers();
void wait_passengers();
void enter_molo();
void leave_molo();
void leave_ticketqueue();
int puchase_process(); // communication with cashier in order to buy a ticket, returns response from cashier - if ticket has been provided or not
int generate_age();
