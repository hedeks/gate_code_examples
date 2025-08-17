#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <endian.h>
#include <net/if.h>

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

// Структура IPv6 заголовка
struct ipv6_header {
    union {
        struct {
#if __BYTE_ORDER == __LITTLE_ENDIAN
            uint32_t traffic_class:8;
            uint32_t flow_label:20;
            uint32_t version:4;
#else
            uint32_t version:4;
            uint32_t traffic_class:8;
            uint32_t flow_label:20;
#endif
            uint16_t payload_len;
            uint8_t  next_header;
            uint8_t  hop_limit;
            struct in6_addr src_addr;
            struct in6_addr dst_addr;
        } fields;
        uint8_t raw[40];
    };
};

// Структура для опций назначения
struct dest_options {
    uint8_t next_header;
    uint8_t hdr_ext_len;
    uint8_t opt_type;
    uint8_t opt_len;
    uint64_t ram_address;
    uint8_t padding[6];
};

// Информация о клиенте
typedef struct {
    int sockfd;
    struct sockaddr_in6 addr;
    pthread_t thread_id;
} client_t;

// Глобальные переменные сервера
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
client_t clients[MAX_CLIENTS];
int active_clients = 0;
int server_active = 1;

// Прототипы функций
void start_server();
void start_client(const char *ipv6_addr);
void* handle_client(void *client_data);
void setup_server_socket(int *server_fd);
void accept_connections(int server_fd);
void cleanup_resources(int server_fd);
void* receive_messages(void *sock_ptr);
void connect_to_ipv6_server(const char *ipv6_addr, int *sockfd);
void send_ipv6_packet(int sockfd, const char *message);
void print_ipv6_header(const struct ipv6_header *hdr);
void print_dest_options(const struct dest_options *opts);
uint64_t htonll(uint64_t value);
uint64_t ntohll(uint64_t value);

// ===================== СЕРВЕРНАЯ ЧАСТЬ =====================

// Настройка IPv6 сокета
void setup_server_socket(int *server_fd) {
    struct sockaddr_in6 server_addr;
    
    // Создание IPv6 TCP сокета
    if ((*server_fd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
        perror("Ошибка создания IPv6 сокета");
        exit(EXIT_FAILURE);
    }
    
    // Настройка параметров сокета
    int opt = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Ошибка SO_REUSEADDR");
    }
    
    // Только IPv6 (без IPv4 совместимости)
    int v6only = 1;
    if (setsockopt(*server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only))) {
        perror("Ошибка IPV6_V6ONLY");
    }
    
    // Настройка hop limit
    int hop_limit = 64;
    if (setsockopt(*server_fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hop_limit, sizeof(hop_limit))) {
        perror("Ошибка IPV6_UNICAST_HOPS");
    }
    
    // Конфигурация адреса сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(PORT);
    server_addr.sin6_addr = in6addr_any;  // Все IPv6 интерфейсы
    
    // Привязка сокета
    if (bind(*server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Ошибка привязки IPv6 сокета");
        close(*server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Начало прослушивания
    if (listen(*server_fd, MAX_CLIENTS) < 0) {
        perror("Ошибка прослушивания");
        close(*server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Сервер IPv6 запущен на порту %d\n", PORT);
    printf("Ожидание IPv6 подключений...\n");
}

// Прием новых подключений
void accept_connections(int server_fd) {
    struct sockaddr_in6 client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (server_active) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            if (server_active) perror("Ошибка accept");
            continue;
        }
        
        pthread_mutex_lock(&clients_mutex);
        
        if (active_clients >= MAX_CLIENTS) {
            printf("Достигнут лимит IPv6 клиентов\n");
            close(client_fd);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }
        
        // Поиск свободного слота
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sockfd == -1) {
                slot = i;
                break;
            }
        }
        
        if (slot == -1) {
            printf("Нет свободных слотов для IPv6 клиента\n");
            close(client_fd);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }
        
        // Сохранение информации о клиенте
        clients[slot].sockfd = client_fd;
        clients[slot].addr = client_addr;
        active_clients++;
        
        // Запуск обработчика клиента
        if (pthread_create(&clients[slot].thread_id, NULL, handle_client, &clients[slot])) {
            perror("Ошибка создания потока IPv6 клиента");
            close(client_fd);
            clients[slot].sockfd = -1;
            active_clients--;
        }
        
        pthread_mutex_unlock(&clients_mutex);
    }
}

// Вывод IPv6 заголовка
void print_ipv6_header(const struct ipv6_header *hdr) {
    char src_ip[INET6_ADDRSTRLEN], dst_ip[INET6_ADDRSTRLEN];
    
    inet_ntop(AF_INET6, &hdr->fields.src_addr, src_ip, sizeof(src_ip));
    inet_ntop(AF_INET6, &hdr->fields.dst_addr, dst_ip, sizeof(dst_ip));
    
    printf("\n=== IPv6 Header ===\n");
    printf("Version: %u\n", hdr->fields.version);
    printf("Traffic class: %u\n", hdr->fields.traffic_class);
    printf("Flow label: %u\n", hdr->fields.flow_label);
    printf("Payload length: %u\n", ntohs(hdr->fields.payload_len));
    printf("Next header: %u\n", hdr->fields.next_header);
    printf("Hop limit: %u\n", hdr->fields.hop_limit);
    printf("Source ip: %s\n", src_ip);
    printf("Destination ip: %s\n", dst_ip);
}

// Вывод опций назначения
void print_dest_options(const struct dest_options *opts) {
    printf("\n=== Destination options header ===\n");
    printf("Next header: %u\n", opts->next_header);
    printf("Extension length: %u\n", opts->hdr_ext_len);
    printf("Option type: 0x%02X\n", opts->opt_type);
    printf("Option length: %u\n", opts->opt_len);
    printf("LOCN: 0x%016lX\n", ntohll(opts->ram_address));
}

// Обработчик клиента
void* handle_client(void *client_data) {
    client_t *client = (client_t *)client_data;
    int sockfd = client->sockfd;
    char client_ip[INET6_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];
    
    inet_ntop(AF_INET6, &client->addr.sin6_addr, client_ip, sizeof(client_ip));
    printf("IPv6 клиент подключен: %s\n", client_ip);
    
    while (server_active) {
        ssize_t recv_bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        
        if (recv_bytes <= 0) {
            if (recv_bytes < 0) perror("Ошибка чтения IPv6");
            break;
        }
        
        // Проверка на IPv6 пакет
        if (recv_bytes >= sizeof(struct ipv6_header)) {
            struct ipv6_header *ip6hdr = (struct ipv6_header *)buffer;
            
            if (ip6hdr->fields.version == 6) {
                print_ipv6_header(ip6hdr);
                
                // Проверка на опции назначения
                if (ip6hdr->fields.next_header == 60 && 
                    recv_bytes >= sizeof(struct ipv6_header) + sizeof(struct dest_options)) {
                    
                    struct dest_options *dest_opt = (struct dest_options *)(buffer + sizeof(struct ipv6_header));
                    print_dest_options(dest_opt);
                    
                    // Вывод данных
                    char *payload = buffer + sizeof(struct ipv6_header) + sizeof(struct dest_options);
                    size_t payload_size = recv_bytes - sizeof(struct ipv6_header) - sizeof(struct dest_options);
                    
                    if (payload_size > 0) {
                        printf("Payload: %.*s\n", (int)payload_size, payload);
                    }
                }
            }
        }
        
        // Эхо-ответ
        if (send(sockfd, buffer, recv_bytes, 0) < 0) {
            perror("Ошибка отправки IPv6");
            break;
        }
    }
    
    printf("IPv6 клиент отключен: %s\n", client_ip);
    close(sockfd);
    
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd == sockfd) {
            clients[i].sockfd = -1;
            active_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    return NULL;
}

// Очистка ресурсов
void cleanup_resources(int server_fd) {
    close(server_fd);
    
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].sockfd != -1) {
            close(clients[i].sockfd);
            pthread_join(clients[i].thread_id, NULL);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    printf("Сервер IPv6 остановлен\n");
}

// Запуск сервера
void start_server() {
    int server_fd;
    
    // Инициализация клиентов
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].sockfd = -1;
    }
    
    setup_server_socket(&server_fd);
    accept_connections(server_fd);
    cleanup_resources(server_fd);
}

// ===================== КЛИЕНТСКАЯ ЧАСТЬ =====================

// Преобразование 64-битных значений
uint64_t htonll(uint64_t value) {
    return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
}

uint64_t ntohll(uint64_t value) {
    return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
}

// Подключение к серверу IPv6
void connect_to_ipv6_server(const char *ipv6_addr, int *sockfd) {
    struct sockaddr_in6 server_addr;
    char addr_str[INET6_ADDRSTRLEN];
    char *zone_ptr;
    unsigned int zone_id = 0;
    
    // Копируем адрес для обработки
    strncpy(addr_str, ipv6_addr, sizeof(addr_str) - 1);
    addr_str[sizeof(addr_str) - 1] = '\0';
    
    // Проверяем наличие идентификатора зоны (%)
    if ((zone_ptr = strchr(addr_str, '%'))) {
        *zone_ptr = '\0';  // Отделяем адрес от идентификатора зоны
        zone_ptr++;        // Переходим к имени интерфейса
        
        // Получаем индекс интерфейса
        if ((zone_id = if_nametoindex(zone_ptr)) == 0) {
            fprintf(stderr, "Ошибка: интерфейс '%s' не найден. Используйте команду 'ip link' для просмотра доступных интерфейсов\n", zone_ptr);
            exit(EXIT_FAILURE);
        }
    }
    
    // Создание IPv6 сокета
    if ((*sockfd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
        perror("Ошибка создания IPv6 сокета");
        exit(EXIT_FAILURE);
    }
    
    // Настройка параметров IPv6
    int hop_limit = 64;
    if (setsockopt(*sockfd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hop_limit, sizeof(hop_limit))) {
        perror("Ошибка настройки Hop Limit");
    }
    
    int v6only = 1;
    if (setsockopt(*sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only))) {
        perror("Ошибка отключения IPv4");
    }
    
    // Настройка адреса сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(PORT);
    server_addr.sin6_scope_id = zone_id;  // Установка ID зоны
    
    // Преобразование IPv6 адреса
    if (inet_pton(AF_INET6, addr_str, &server_addr.sin6_addr) <= 0) {
        if (errno == 0) {
            fprintf(stderr, "Ошибка: '%s' не является валидным IPv6 адресом\n", addr_str);
        } else {
            perror("Ошибка inet_pton");
        }
        close(*sockfd);
        exit(EXIT_FAILURE);
    }
    
    printf("Подключение к IPv6 серверу [%s%%%s]:%d...\n", 
           addr_str, zone_id ? zone_ptr : "<none>", PORT);
    
    if (connect(*sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Ошибка подключения IPv6");
        close(*sockfd);
        exit(EXIT_FAILURE);
    }
    
    printf("Успешное подключение по IPv6\n");
}

// Отправка IPv6 пакета
void send_ipv6_packet(int sockfd, const char *message) {
    struct ipv6_header ip6hdr;
    struct dest_options dest_opt;
    const char *payload = message;
    size_t payload_size = strlen(payload);
    
    // Заполнение IPv6 заголовка
    memset(&ip6hdr, 0, sizeof(ip6hdr));
    ip6hdr.fields.version = 6;
    ip6hdr.fields.traffic_class = 0;
    ip6hdr.fields.flow_label = htonl(12345) >> 12;
    ip6hdr.fields.payload_len = htons(sizeof(dest_opt) + payload_size);
    ip6hdr.fields.next_header = 60;  // Destination Options
    ip6hdr.fields.hop_limit = 64;

    struct sockaddr_in6 my_addr, peer_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in6);

    if (getsockname(sockfd, (struct sockaddr*)&my_addr, &addr_len) == 0) {
        ip6hdr.fields.src_addr = my_addr.sin6_addr;
    } else {
        perror("Ошибка getsockname");
        inet_pton(AF_INET6, "::1", &ip6hdr.fields.src_addr);
    }

    if (getpeername(sockfd, (struct sockaddr*)&peer_addr, &addr_len) == 0) {
        ip6hdr.fields.dst_addr = peer_addr.sin6_addr;
    } else {
        perror("Ошибка getpeername");
        inet_pton(AF_INET6, "::1", &ip6hdr.fields.dst_addr);
    }
    
    // Заполнение опций
    memset(&dest_opt, 0, sizeof(dest_opt));
    dest_opt.next_header = 6;   // TCP
    dest_opt.hdr_ext_len = 1;   // Размер заголовка (1 блок по 8 байт)
    dest_opt.opt_type = 0xC2;   // Тип опции
    dest_opt.opt_len = 8;       // Длина данных опции
    dest_opt.ram_address = htonll(0x123456789ABCDEF0); // Пример адреса
    
    // Формирование пакета
    char packet[sizeof(ip6hdr) + sizeof(dest_opt) + payload_size];
    memcpy(packet, &ip6hdr, sizeof(ip6hdr));
    memcpy(packet + sizeof(ip6hdr), &dest_opt, sizeof(dest_opt));
    memcpy(packet + sizeof(ip6hdr) + sizeof(dest_opt), payload, payload_size);
    
    // Отправка пакета
    if (send(sockfd, packet, sizeof(packet), 0) < 0) {
        perror("Ошибка отправки IPv6 пакета");
    }
}

// Прием сообщений
void* receive_messages(void *sock_ptr) {
    int sockfd = *((int *)sock_ptr);
    char buffer[BUFFER_SIZE];
    
    while (1) {
        ssize_t recv_bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);
        
        if (recv_bytes <= 0) {
            if (recv_bytes < 0) perror("Ошибка чтения IPv6");
            printf("Сервер IPv6 отключен\n");
            close(sockfd);
            exit(0);
        }
        
        // Обработка IPv6 пакета
        if (recv_bytes >= sizeof(struct ipv6_header)) {
            struct ipv6_header *ip6hdr = (struct ipv6_header *)buffer;
            
            if (ip6hdr->fields.version == 6) {
                char src_ip[INET6_ADDRSTRLEN], dst_ip[INET6_ADDRSTRLEN];
                
                inet_ntop(AF_INET6, &ip6hdr->fields.src_addr, src_ip, sizeof(src_ip));
                inet_ntop(AF_INET6, &ip6hdr->fields.dst_addr, dst_ip, sizeof(dst_ip));
                
                printf("\n=== Получен IPv6 пакет ===\n");
                printf("Source: %s\n", src_ip);
                printf("Destination: %s\n", dst_ip);
                printf("Payload length: %u\n", ntohs(ip6hdr->fields.payload_len));
                
                // Обработка опций назначения
                if (ip6hdr->fields.next_header == 60 && 
                    recv_bytes >= sizeof(struct ipv6_header) + sizeof(struct dest_options)) {
                    
                    struct dest_options *dest_opt = (struct dest_options *)(buffer + sizeof(struct ipv6_header));
                    
                    printf("Option type: 0x%02X\n", dest_opt->opt_type);
                    printf("LOCN: 0x%016lX\n", ntohll(dest_opt->ram_address));
                    
                    // Вывод данных
                    char *payload = buffer + sizeof(struct ipv6_header) + sizeof(struct dest_options);
                    size_t payload_size = recv_bytes - sizeof(struct ipv6_header) - sizeof(struct dest_options);
                    
                    if (payload_size > 0) {
                        printf("Payload: %.*s\n", (int)payload_size, payload);
                    }
                }
                printf("> ");
                fflush(stdout);
                continue;
            }
        }
        
        buffer[recv_bytes] = '\0';
        printf("\n[СЕРВЕР]: %s\n> ", buffer);
        fflush(stdout);
    }
    return NULL;
}

// Запуск клиента
void start_client(const char *ipv6_addr) {
    int sockfd;
    pthread_t recv_thread;
    
    connect_to_ipv6_server(ipv6_addr, &sockfd);
    
    if (pthread_create(&recv_thread, NULL, receive_messages, &sockfd)) {
        perror("Ошибка создания потока приема");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    char message[BUFFER_SIZE];
    while (1) {
        printf("> ");
        fflush(stdout);
        
        if (fgets(message, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        
        message[strcspn(message, "\n")] = '\0';
        
        if (strcmp(message, "exit") == 0) {
            break;
        }
        
        send_ipv6_packet(sockfd, message);
    }
    
    close(sockfd);
    pthread_cancel(recv_thread);
    printf("Клиент IPv6 отключен\n");
}

// ===================== ОСНОВНАЯ ФУНКЦИЯ =====================
int main() {
    int mode;
    printf("Выберите режим:\n1. Сервер IPv6\n2. Клиент IPv6\n> ");
    if (scanf("%d", &mode) != 1) {
        printf("Ошибка ввода\n");
        return 1;
    }
    getchar();
    
    if (mode == 1) {
        start_server();
    } else if (mode == 2) {
        char ipv6_addr[INET6_ADDRSTRLEN];
        printf("Введите IPv6 адрес сервера: ");
        if (fgets(ipv6_addr, sizeof(ipv6_addr), stdin) == NULL) {
            printf("Ошибка ввода\n");
            return 1;
        }
        ipv6_addr[strcspn(ipv6_addr, "\n")] = '\0';
        start_client(ipv6_addr);
    } else {
        printf("Некорректный выбор\n");
    }

    return 0;
}
