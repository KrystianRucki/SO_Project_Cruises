#include <stdio.h>      // Standardowe funkcje wejścia/wyjścia
#include <stdlib.h>     // Funkcje takie jak atoi() i exit()
#include <fcntl.h>      // Operacje na deskryptorach plików
#include <sys/stat.h>   // Funkcje i makra do obsługi atrybutów plików
#include <unistd.h>     // Funkcje POSIX, takie jak write(), close()
#include <sys/socket.h> // Operacje na gniazdach
#include <sys/un.h>     // Struktury i funkcje dla gniazd domeny Unix
#include <errno.h>      // Definicja zmiennej errno
#include <string.h>     // Funkcje do manipulacji łańcuchami, np. strncpy()
#include <signal.h>
#include <time.h>

#define CASHIER_SOCKET_PATH "/tmp/cashier_socket" // Ścieżka do gniazda domeny Unix dla komunikacji z kasjerem
#define STERNIK_SOCKET_PATH "/tmp/sternik_socket"  // Ścieżka do gniazda sternika
#define MAX_RETRIES 5  // Maksymalna liczba prób ponownego połączenia
#define RETRY_DELAY 1  // Opóźnienie między próbami połączenia (w sekundach)

volatile sig_atomic_t cruise_status = 0; // Zmienna do przechowywania informacji o udanym rejsie

//Obsługa sygnałów od sternika - informacja zwrotna czy udało się popłynąć czy też nie
void signal_handler(int sig)
{
    if (sig == SIGRTMIN)
    {
        cruise_status = 1; // Sukces - rejs udany
    }
    else if (sig == SIGRTMIN + 1)
    {
        cruise_status = -1; // Porażka - rejs nie powiódł się
    }
}


int main(int argc, char *argv[])
{
    setbuf(stdout, NULL); // Wyłącza buforowanie dla stdout, dzięki czemu wszystkie komunikaty są od razu wyświetlane

    // Sprawdzanie liczby argumentów programu
    if (argc < 4)
    {
        fprintf(stderr, "Usage: %s [pid] [age] [group]\n", argv[0]); //Jakie argumenty powinien dostać
        return 1; // Zakończenie programu z błędem
    }

    // Parsowanie argumentów wejściowych
    int pid = atoi(argv[1]);    // PID pasażera - ID
    int age = atoi(argv[2]);    // Wiek pasażera
    int group = atoi(argv[3]);  // Grupa pasażera

    pid_t realpid = getpid();   // Prawdziwy PID
    int discount = 0;           // Zmienna na zniżkę

    srand(time(NULL));

    // Rejestracja obsługi sygnałów
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    // Rejestracja SIGRTMIN
    if(sigaction(SIGRTMIN, &sa, NULL) == -1)
    {
        perror("[ERROR] Failed to set SIGRTMIN handler");
        exit(1);
    }

    // Rejestracja SIGRTMIN + 1
    if (sigaction(SIGRTMIN + 1, &sa, NULL) == -1)
    {
        perror("[ERROR] Failed to set SIGRTMIN + 1 handler");
        exit(1);
    }

    while(1)
    {
        // Tworzenie gniazda domeny Unix
        int cashier_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (cashier_sock_fd < 0)
        {
            perror("[PASSENGER] Error creating socket"); // Wyświetlenie błędu, jeśli tworzenie gniazda się nie powiedzie
            return 1;
        }

        // Konfiguracja adresu gniazda
        struct sockaddr_un server_addr;
        memset(&server_addr, 0, sizeof(server_addr)); // Zerowanie struktury adresu
        server_addr.sun_family = AF_UNIX;            // Ustawienie typu domeny Unix
        strncpy(server_addr.sun_path, CASHIER_SOCKET_PATH, sizeof(server_addr.sun_path) - 1); // Ustawienie ścieżki do gniazda


        if (access(CASHIER_SOCKET_PATH, F_OK) == -1) 
        {
            //Takie sprawdzenie pomaga w sytuacji kiedy kasjer skończył swoje działanie (np. kończenie symulacji - a w międzyczasie pasażer próbuje ponownie kupić bilet na rejs - pasażer zakończy swoje działanie poprawnie)
            printf("[PASSENGER %d] Cashier socket no longer exists. Leaving...\n", pid);
            close(cashier_sock_fd);
            break; // Wyjdź z pętli, bo kasjer już nie działa
        }

        // Mechanizm ponownych prób połączenia
        int retries = 0;
        while (connect(cashier_sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            if (errno == ENOENT && retries < MAX_RETRIES)
            {
                // Jeśli gniazdo jeszcze nie istnieje (ENOENT), próba połączenia zostanie ponowiona
                fprintf(stderr, "[PASSENGER] Error connecting to cashier socket: %s. Retrying...\n", strerror(errno));
                retries++;
                sleep(RETRY_DELAY); // Oczekiwanie przed kolejną próbą, jeśli nie uda się podłączyć to może znaczyć że kasjer już przestał działać nie obsługuje pasażerów
            }
            else
            {
                // Inne błędy połączenia lub przekroczenie limitu prób
                perror("[PASSENGER] Error connecting to cashier socket");
                close(cashier_sock_fd); // Zamknięcie deskryptora gniazda
                return 1;
            }
        }

        // Przygotowanie i wysłanie żądania do kasjera
        char request_buffer[256];
        snprintf(request_buffer, sizeof(request_buffer), "GET %d %d %d\n", pid, age, discount); // Tworzenie żądania GET
        if (write(cashier_sock_fd, request_buffer, strlen(request_buffer)) == -1)
        {
            perror("[PASSENGER] Error writing to cashier socket"); // Obsługa błędu zapisu do gniazda
            close(cashier_sock_fd);
            return 1;
        }

        // Odczyt odpowiedzi z kasjera
        char response_buffer[256];
        int skip_queue = 0; // Zmienna wskazująca, czy pasażer ma ominąć kolejkę

        ssize_t bytes_read = read(cashier_sock_fd, response_buffer, sizeof(response_buffer) - 1);
        if (bytes_read > 0)
        {
            // Jeśli odczytano dane z gniazda
            response_buffer[bytes_read] = '\0'; // Dodanie znaku końca łańcucha
            if (strncmp(response_buffer, "OK", 2) == 0)
            {
                // Parsowanie odpowiedzi w przypadku sukcesu
                sscanf(response_buffer, "OK DISC=%d SKIP=%d", &discount, &skip_queue);
                printf("[PASSENGER %d] OK DISC=%d SKIP=%d\n", pid, discount, skip_queue);
            }
            else
            {
                // Obsługa nieznanej odpowiedzi
                printf("[PASSENGER %d] Unknown response: %s", pid, response_buffer);
            }
        }
        else
        {
            // Obsługa błędu odczytu
            perror("[PASSENGER] Error reading from cashier socket");
        }

        close(cashier_sock_fd); // Zamknięcie gniazda

        // --- Komunikacja ze sternikiem ---

        // Tworzenie gniazda UNIX do komunikacji z sternikiem
        int sternik_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sternik_sock_fd < 0)
        {
            perror("[PASSENGER] Error creating socket for sternik");// Obsługa błędu przy tworzeniu gniazda
            return 1;
        }

        // Inicjalizacja struktury adresu gniazda sternika
        struct sockaddr_un sternik_addr;
        memset(&sternik_addr, 0, sizeof(sternik_addr)); // Zerowanie struktury adresu
        sternik_addr.sun_family = AF_UNIX;              // Ustawienie rodziny adresów na domenę Unix
        strncpy(sternik_addr.sun_path, STERNIK_SOCKET_PATH, sizeof(sternik_addr.sun_path) - 1); // Ustawienie ścieżki do gniazda sternika (maksymalna długość o 1 mniejsza dla znaku końca)


        if (access(STERNIK_SOCKET_PATH, F_OK) == -1)
        {
            //Takie sprawdzenie pomaga w sytuacji kiedy sternik skończył swoje działanie (np. kończenie symulacji - a w międzyczasie pasażer próbuje ponownie przystąpić do rejsu - pasażer zakończy swoje działanie poprawnie)
            fprintf(stderr,"[PASSENGER %d] Sternik socket no longer exists. Leaving...\n", pid);
            close(sternik_sock_fd);
            break; // Wyjdź z pętli, bo sternik już nie działa
        }
        
        // Próba połączenia z gniazdem sternika z mechanizmem ponownych prób
        retries = 0;
        while (connect(sternik_sock_fd, (struct sockaddr *)&sternik_addr, sizeof(sternik_addr)) < 0)
        {
            if (errno == ENOENT && retries < MAX_RETRIES)
            {
                // Obsługa sytuacji, gdy gniazdo jeszcze nie istnieje (ENOENT)
                fprintf(stderr, "[PASSENGER] Error connecting to sternik socket: %s. Retrying...\n", strerror(errno));
                retries++;
                sleep(RETRY_DELAY);
            }
            else
            {
                // Inne błędy połączenia lub przekroczenie limitu prób
                perror("[PASSENGER] Error connecting to sternik socket");
                close(sternik_sock_fd); // Zamknięcie gniazda w przypadku błędu
                exit(1);
            }
        }

        // Przygotowanie żądania dla sternika
        if (skip_queue)
        {
            // Tworzenie komunikatu dla sternika w przypadku omijania kolejki
            snprintf(request_buffer, sizeof(request_buffer), "SKIP_QUEUE %d %d %d %d\n", pid, age, group, realpid);
        }
        else
        {
            // Tworzenie komunikatu dla sternika w przypadku standardowego wejścia w kolejkę
            snprintf(request_buffer, sizeof(request_buffer), "QUEUE %d %d %d %d\n", pid, age, group, realpid);
        }

        // Wysłanie przygotowanego żądania do gniazda sternika
        if (write(sternik_sock_fd, request_buffer, strlen(request_buffer)) == -1)
        {
            perror("[PASSENGER] Error writing to sternik socket"); // Obsługa błędu zapisu do gniazda sternika
            close(sternik_sock_fd);
            return 1;
        }
        // Informacja o wysłaniu żądania do sternika
        printf("[PASSENGER %d] Sent to sternik (pid, age, group, realpid): %s", pid, request_buffer);

        close(sternik_sock_fd); // Zamknięcie deskryptora gniazda po zakończonej komunikacji

        // --- Oczekiwanie na sygnał ---
        printf("[PASSENGER %d] Waiting for response from sternik...\n", pid);
        pause(); // Oczekiwanie na sygnał

        if (cruise_status == -1)
        {
            printf("[PASSENGER %d] Didn't cruise. Leaving...\n", pid);
            break;
        }
        else if (cruise_status == 1)
        {
            printf("\033[38;5;10m[PASSENGER %d] Cruised successfully!\033[0m\n", pid);
        }

        // --- Decyzja pasażera czy może lub chce znowu płynąć ---
        int random_decision = rand() % 2; // Losowa decyzja (0 = kończy, 1 = kontynuuje)
        if (skip_queue || random_decision == 0)
        {
            printf("[PASSENGER %d] Won't cruise again.\n", pid);
            break; // Koniec działania, jeśli pasażer nie chce lub nie może płynąć dalej
        }

        printf("[PASSENGER %d] Will try to cruise again with discount\n", pid);
    }
    
    return 0;
}
