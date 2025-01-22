#include <stdio.h>      // Standardowe funkcje wejścia/wyjścia
#include <stdlib.h>     // Funkcje takie jak atoi() i exit()
#include <fcntl.h>      // Operacje na deskryptorach plików
#include <sys/stat.h>   // Funkcje i makra do obsługi atrybutów plików
#include <unistd.h>     // Funkcje POSIX, takie jak write(), close()
#include <sys/socket.h> // Operacje na gniazdach
#include <sys/un.h>     // Struktury i funkcje dla gniazd domeny Unix
#include <errno.h>      // Definicja zmiennej errno
#include <string.h>     // Funkcje do manipulacji łańcuchami, np. strncpy()

#define CASHIER_SOCKET_PATH "/tmp/cashier_socket" // Ścieżka do gniazda domeny Unix dla komunikacji z kasjerem
#define STERNIK_SOCKET_PATH "/tmp/sternik_socket"  // Ścieżka do gniazda sternika
#define MAX_RETRIES 5  // Maksymalna liczba prób ponownego połączenia
#define RETRY_DELAY 1  // Opóźnienie między próbami połączenia (w sekundach)

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL); // Wyłącza buforowanie dla stdout, dzięki czemu wszystkie komunikaty są od razu wyświetlane

    // Sprawdzanie liczby argumentów programu
    if (argc < 4)
    {
        fprintf(stderr, "Usage: %s [pid] [age] [group]\n", argv[0]); // Informacja o poprawnym użyciu programu
        return 1; // Zakończenie programu z błędem
    }

    // Parsowanie argumentów wejściowych
    int pid = atoi(argv[1]);  // PID pasażera
    int age = atoi(argv[2]);  // Wiek pasażera
    int group = atoi(argv[3]); // Grupa pasażera

    // Tworzenie gniazda domeny Unix
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        perror("[PASSENGER] Error creating socket"); // Wyświetlenie błędu, jeśli tworzenie gniazda się nie powiedzie
        return 1;
    }

    // Konfiguracja adresu gniazda
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Zerowanie struktury adresu
    server_addr.sun_family = AF_UNIX;            // Ustawienie typu domeny Unix
    strncpy(server_addr.sun_path, CASHIER_SOCKET_PATH, sizeof(server_addr.sun_path) - 1); // Ustawienie ścieżki do gniazda

    // Mechanizm ponownych prób połączenia
    int retries = 0;
    while (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
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
            // Jeśli błąd nie jest związany z brakiem gniazda lub przekroczono limit prób
            perror("[PASSENGER] Error connecting to cashier socket");
            close(sock_fd); // Zamknięcie deskryptora gniazda
            return 1;
        }
    }

    // Przygotowanie i wysłanie żądania do kasjera
    char request_buffer[256];
    snprintf(request_buffer, sizeof(request_buffer), "GET %d %d\n", pid, age); // Tworzenie żądania GET z PID i wiekiem
    if (write(sock_fd, request_buffer, strlen(request_buffer)) == -1)
    {
        perror("[PASSENGER] Error writing to cashier socket"); // Obsługa błędu zapisu do gniazda
        close(sock_fd);
        return 1;
    }

    // Odczyt odpowiedzi z kasjera
    char response_buffer[256];
    int discount = 0; // Zmienna na zniżkę
    int skip_queue = 0; // Zmienna wskazująca, czy pasażer ma ominąć kolejkę

    ssize_t bytes_read = read(sock_fd, response_buffer, sizeof(response_buffer) - 1);
    if (bytes_read > 0)
    {
        // Jeśli odczytano dane z gniazda
        response_buffer[bytes_read] = '\0'; // Dodanie znaku końca łańcucha
        if (strncmp(response_buffer, "OK", 2) == 0)
        {
            // Parsowanie odpowiedzi w przypadku sukcesu
            sscanf(response_buffer, "OK %*d DISC=%d SKIP=%d", &discount, &skip_queue);
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

    close(sock_fd); // Zamknięcie gniazda

    // --- Komunikacja ze sternikiem ---

    // Tworzenie gniazda UNIX do komunikacji z sternikiem
    int sternik_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sternik_sock_fd < 0)
    {
        perror("[PASSENGER] Error creating socket for sternik");
        return 1;
    }

    struct sockaddr_un sternik_addr;
    memset(&sternik_addr, 0, sizeof(sternik_addr));
    sternik_addr.sun_family = AF_UNIX;
    strncpy(sternik_addr.sun_path, STERNIK_SOCKET_PATH, sizeof(sternik_addr.sun_path) - 1);

    retries = 0;
    while (connect(sternik_sock_fd, (struct sockaddr *)&sternik_addr, sizeof(sternik_addr)) < 0)
    {
        if (errno == ENOENT && retries < MAX_RETRIES)
        {
            fprintf(stderr, "[PASSENGER] Error connecting to sternik socket: %s. Retrying...\n", strerror(errno));
            retries++;
            sleep(RETRY_DELAY);
        }
        else
        {
            perror("[PASSENGER] Error connecting to sternik socket");
            close(sternik_sock_fd);
            return 1;
        }
    }

    // Przygotowanie żądania dla sternika
    if (skip_queue)
    {
        snprintf(request_buffer, sizeof(request_buffer), "SKIP_QUEUE %d %d %d %d\n", pid, age, discount, group);
    }
    else
    {
        snprintf(request_buffer, sizeof(request_buffer), "QUEUE %d %d %d %d\n", pid, age, discount, group);
    }

    if (write(sternik_sock_fd, request_buffer, strlen(request_buffer)) == -1)
    {
        perror("[PASSENGER] Error writing to sternik socket");
        close(sternik_sock_fd);
        return 1;
    }

    printf("[PASSENGER %d] Sent to sternik (pid, age, discount, group): %s", pid, request_buffer);

    close(sternik_sock_fd); // Zamknięcie gniazda

    return 0;
}
