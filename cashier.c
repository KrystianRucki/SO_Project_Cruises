#include <stdio.h>      // Standardowe funkcje wejścia/wyjścia
#include <stdlib.h>     // Funkcje takie jak exit()
#include <unistd.h>     // Funkcje POSIX, takie jak read(), write(), close()
#include <sys/socket.h> // Operacje na gniazdach
#include <sys/un.h>     // Struktury i funkcje dla gniazd domeny Unix
#include <errno.h>      // Definicja zmiennej errno
#include <string.h>     // Funkcje do manipulacji łańcuchami, np. strncpy()
#include <semaphore.h>

#define CASHIER_SOCKET_PATH "/tmp/cashier_socket"   // Ścieżka do gniazda domeny Unix
#define MAX_PASSENGER_IDS 6000                      // Maksymalna liczba identyfikatorów pasażerów

// Tablica przechowująca informacje, czy pasażer o danym ID podróżował wcześniej
static int has_traveled[MAX_PASSENGER_IDS] = {0};

// Flaga używana do zakończenia działania kasjera
static volatile int terminate_cashier = 0;

// Funkcja przetwarzająca żądania od klientów (pasażerów).
static void process_request(int client_fd)
{
    char buffer[256]; // Bufor do przechowywania danych od klienta
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1); // Odczyt danych z gniazda

    if (bytes_read > 0)
    {
        // Dodanie znaku końca łańcucha, aby poprawnie obsługiwać dane jako string
        buffer[bytes_read] = '\0';

        if (strncmp(buffer, "GET", 3) == 0)
        {
            // Obsługa żądania "GET", zawierającego PID i wiek pasażera
            int pid, age, discount;
            sscanf(buffer, "GET %d %d %d", &pid, &age, &discount); // Parsowanie danych

            printf("[CASHIER] Passenger %d age = %d discount = %d\n", pid, age, discount);
            
            // Inicjalizacja odpowiedzi
            int skip_queue = 0;   // Domyślnie brak priorytetu omijania kolejki - flaga na 0

            // Sprawdzanie, czy ID pasażera mieści się w dopuszczalnym zakresie
            if (pid >= 0 && pid < MAX_PASSENGER_IDS)
            {
                if (!has_traveled[pid])
                {
                    // Jeśli pasażer podróżuje po raz pierwszy
                    has_traveled[pid] = 1; // Oznaczenie, że pasażer już podróżował
                    discount = (age < 3) ? 100 : 50; // Zniżka 50% lub 100% dla dzieci - Dzieci poniżej 3 roku życia nie płacą za bilet
                }
                else
                {
                    // Jeśli pasażer podróżował już wcześniej
                    skip_queue = 1; // Ustawienie priorytetu omijania kolejki - flaga na 1
                }
            }

            // Przygotowanie odpowiedzi dla klienta
            char response[128];
            snprintf(response, sizeof(response), "OK DISC=%d SKIP=%d\n", discount, skip_queue);
            write(client_fd, response, strlen(response)); // Wysłanie odpowiedzi
        }
        else if(strncmp(buffer, "QUIT", 4) == 0)
        {
            // Obsługa komendy "QUIT", kończącej działanie kasjera
            printf("[CASHIER] Terminating. (QUIT RECEIVED)\n");
            terminate_cashier = 1; // Ustawienie flagi końca działania
        }
        else
        {
            // Obsługa nieznanych komend
            printf("[CASHIER] Unknown command: %s\n", buffer);
        }
    }
    else if(bytes_read < 0)
    {
        // Obsługa błędów odczytu danych
        perror("[CASHIER] Error reading from client");
    }
}


// --- Główna funkcja programu kasjera. ---
int main()
{
    // Otwieramy semafor o nazwie "/can_generate"
    sem_t *sem = sem_open("/can_generate", 0);  // Otwieramy semafor bez tworzenia go

    if (sem == SEM_FAILED)
    {
        perror("sem_open");
        return 1;
    }

    // Usuwanie poprzedniego gniazda, jeśli istnieje
    unlink(CASHIER_SOCKET_PATH);

    // Tworzenie gniazda domeny Unix
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0); // socket typu strumieniowego, komunikacja na polaczeniach zamiast wysyłaniu pojedynczych wiadomości
    if(server_fd < 0)
    {
        perror("[CASHIER] Error creating socket"); // Obsługa błędu tworzenia gniazda
        return 1;
    }

    // Konfiguracja adresu gniazda - do ktorego cashier będzie nasłuchiwał
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Zerowanie struktury adresu
    server_addr.sun_family = AF_UNIX;            // Ustawienie domeny gniazda na Unix, sun_family - rodzina adresow AF_UNIX
    strncpy(server_addr.sun_path, CASHIER_SOCKET_PATH, sizeof(server_addr.sun_path) - 1); // Ustawienie ścieżki gniazda

    // Powiązanie gniazda z adresem - "serwer" łączy swój socket z określoną ścieżką - inne procesy beda mogly sie połączyć z tym socketem
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[CASHIER] Error binding socket"); // Obsługa błędu wiązania
        close(server_fd);
        return 1;
    }

    // Nasłuchiwanie połączeń przychodzących za pomocą listen
    if (listen(server_fd, 5) < 0) 
    {
        perror("[CASHIER] Error listening on socket"); // Obsługa błędu nasłuchiwania
        close(server_fd);
        return 1;
    }

    printf("[CASHIER] Service started.\n");

    sem_post(sem); //kasjer wystartował - można rozpocząć generowanie pasażerów

    // Główna pętla obsługująca klientów
    while (!terminate_cashier)
    {
        int client_fd = accept(server_fd, NULL, NULL); // Akceptowanie połączenia od klienta (passenger'a), blokuje proces czekając na połączenie od klienta
        if (client_fd < 0)
        {
            perror("[CASHIER] Error accepting client"); // Obsługa błędu akceptacji połączenia
            continue; // Kontynuacja w przypadku błędu
        }

        process_request(client_fd); // Przetwarzanie żądania od klienta, odczytuje dane z client_fd i wysyła odpowiedź
        close(client_fd);           // Zamknięcie połączenia z klientem
    }

    // Sprzątanie po zakończeniu działania
    close(server_fd);               // Zamknięcie gniazda nasłuchującego
    unlink(CASHIER_SOCKET_PATH);    // Usunięcie pliku gniazda
    sem_close(sem);
    printf("\033[38;5;10m[CASHIER] Service ended.\033[0m\n");
    return 0; // Zakończenie programu
}
