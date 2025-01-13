#include "globals.h"

int generate_age();
void enter_molo();
void leave_molo();
void leave_ticketqueue();
int purchase_process(int age); // communication with cashier in order to buy a ticket, returns decision from cashier - if ticket has been provided or not - passenger side, cashier will have similiar function
void passenger_cycle();
void create_passengers();
void wait_passengers();
