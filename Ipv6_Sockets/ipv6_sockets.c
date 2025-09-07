#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netdb.h>
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
struct ipv6_header
{
    union
    {
        struct
        {
#if __BYTE_ORDER == __LITTLE_ENDIAN
            uint32_t traffic_class : 8;
            uint32_t flow_label : 20;
            uint32_t version : 4;
#else
            uint32_t version : 4;
            uint32_t traffic_class : 8;
            uint32_t flow_label : 20;
#endif
            uint16_t payload_len;
            uint8_t next_header;
            uint8_t hop_limit;
            struct in6_addr src_addr;
            struct in6_addr dst_addr;
        } fields;
        uint8_t raw[40];
    };
};

// Структура для опций назначения
struct dest_options
{
    uint8_t next_header;
    uint8_t hdr_ext_len;
    uint8_t opt_type;
    uint8_t opt_len;
    uint64_t ram_address;
    uint8_t padding[6];
};

// Информация о клиенте
typedef struct
{
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
void *handle_client(void *client_data);
void setup_server_socket(int *server_fd);
void accept_connections(int server_fd);
void cleanup_resources(int server_fd);
void *receive_messages(void *sock_ptr);
void connect_to_ipv6_server(const char *ipv6_addr, int *sockfd);
void send_ipv6_packet(int sockfd, const char *message);
void print_ipv6_header(const struct ipv6_header *hdr);
void print_dest_options(const struct dest_options *opts);
uint64_t htonll(uint64_t value);
uint64_t ntohll(uint64_t value);

// ===================== СЕРВЕРНАЯ ЧАСТЬ =====================

// Настройка IPv6 сокета
void setup_server_socket(int *server_fd)
{
    struct sockaddr_in6 server_addr;

    // socket: Создает конечную точку для связи и возвращает файловый дескриптор.
    // AF_INET6: Семейство адресов для IPv6.
    // SOCK_STREAM: Тип сокета для потоковой передачи данных (TCP).
    // 0: Протокол по умолчанию для данного типа сокета (TCP).
    if ((*server_fd = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
    {
        perror("Ошибка создания IPv6 сокета");
        exit(EXIT_FAILURE);
    }

    // setsockopt: Устанавливает опции для сокета.
    // SOL_SOCKET: Уровень сокета, на котором будет установлена опция.
    // SO_REUSEADDR: Позволяет повторно использовать локальный адрес, что полезно для быстрого перезапуска сервера.
    int opt = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("Ошибка SO_REUSEADDR");
    }

    // IPV6_V6ONLY: Опция для сокета IPv6, которая определяет, будет ли сокет принимать только IPv6-соединения
    // или также и IPv4-соединения (в режиме совместимости). 1 - только IPv6.
    int v6only = 1;
    if (setsockopt(*server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)))
    {
        perror("Ошибка IPV6_V6ONLY");
    }

    // IPV6_UNICAST_HOPS: Устанавливает максимальное количество переходов (hop limit) для исходящих unicast-пакетов.
    int hop_limit = 64;
    if (setsockopt(*server_fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hop_limit, sizeof(hop_limit)))
    {
        perror("Ошибка IPV6_UNICAST_HOPS");
    }

    // memset: Заполняет блок памяти указанным значением (в данном случае нулями).
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    // htons (host to network short): Преобразует 16-битное число из порядка байтов хоста в сетевой порядок байтов.
    server_addr.sin6_port = htons(PORT);
    // in6addr_any: Специальный IPv6-адрес (::), который означает, что сервер будет принимать подключения на всех доступных сетевых интерфейсах.
    server_addr.sin6_addr = in6addr_any;

    // bind: Привязывает сокет к указанному адресу и порту.
    // (struct sockaddr*)&server_addr: Приведение типа от конкретной структуры адреса IPv6 (sockaddr_in6)
    // к обобщенной структуре адреса (sockaddr), как того требует функция bind.
    if (bind(*server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Ошибка привязки IPv6 сокета");
        close(*server_fd);
        exit(EXIT_FAILURE);
    }

    // listen: Переводит сокет в режим прослушивания входящих подключений.
    // MAX_CLIENTS: Максимальная длина очереди ожидающих подключений.
    if (listen(*server_fd, MAX_CLIENTS) < 0)
    {
        perror("Ошибка прослушивания");
        close(*server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Сервер IPv6 запущен на порту %d\n", PORT);
    printf("Ожидание IPv6 подключений...\n");
}

// Прием новых подключений
void accept_connections(int server_fd)
{
    struct sockaddr_in6 client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (server_active)
    {
        // accept: Извлекает первое соединение из очереди ожидающих соединений, создает новый сокет для этого соединения
        // и возвращает его файловый дескриптор. Блокирует выполнение до появления нового соединения.
        // (struct sockaddr*)&client_addr: Приведение типа для передачи указателя на структуру,
        // в которую будет записана информация об адресе подключившегося клиента.
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_fd < 0)
        {
            if (server_active)
                perror("Ошибка accept");
            continue;
        }

        // pthread_mutex_lock: Блокирует мьютекс для защиты разделяемых данных (списка клиентов) от одновременного доступа из разных потоков.
        pthread_mutex_lock(&clients_mutex);

        if (active_clients >= MAX_CLIENTS)
        {
            printf("Достигнут лимит IPv6 клиентов\n");
            close(client_fd);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }

        // Поиск свободного слота
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].sockfd == -1)
            {
                slot = i;
                break;
            }
        }

        if (slot == -1)
        {
            printf("Нет свободных слотов для IPv6 клиента\n");
            close(client_fd);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }

        // Сохранение информации о клиенте
        clients[slot].sockfd = client_fd;
        clients[slot].addr = client_addr;
        active_clients++;

        // pthread_create: Создает новый поток для обработки подключенного клиента.
        // &clients[slot].thread_id: Указатель для хранения идентификатора нового потока.
        // NULL: Атрибуты потока по умолчанию.
        // handle_client: Функция, которую будет выполнять новый поток.
        // &clients[slot]: Аргумент, передаваемый в функцию потока.
        if (pthread_create(&clients[slot].thread_id, NULL, handle_client, &clients[slot]))
        {
            perror("Ошибка создания потока IPv6 клиента");
            close(client_fd);
            clients[slot].sockfd = -1;
            active_clients--;
        }

        // pthread_mutex_unlock: Разблокирует мьютекс после завершения работы с разделяемыми данными.
        pthread_mutex_unlock(&clients_mutex);
    }
}

// Вывод IPv6 заголовка
void print_ipv6_header(const struct ipv6_header *hdr)
{
    char src_ip[INET6_ADDRSTRLEN], dst_ip[INET6_ADDRSTRLEN];

    // inet_ntop (network to presentation): Преобразует числовой IPv6-адрес из бинарного формата в текстовую строку.
    inet_ntop(AF_INET6, &hdr->fields.src_addr, src_ip, sizeof(src_ip));
    inet_ntop(AF_INET6, &hdr->fields.dst_addr, dst_ip, sizeof(dst_ip));

    printf("\n=== IPv6 Header ===\n");
    printf("Version: %u\n", hdr->fields.version);
    printf("Traffic class: %u\n", hdr->fields.traffic_class);
    printf("Flow label: %u\n", hdr->fields.flow_label);
    // ntohs (network to host short): Преобразует 16-битное число из сетевого порядка байтов в порядок байтов хоста.
    printf("Payload length: %u\n", ntohs(hdr->fields.payload_len));
    printf("Next header: %u\n", hdr->fields.next_header);
    printf("Hop limit: %u\n", hdr->fields.hop_limit);
    printf("Source ip: %s\n", src_ip);
    printf("Destination ip: %s\n", dst_ip);
}

// Вывод опций назначения
void print_dest_options(const struct dest_options *opts)
{
    printf("\n=== Destination options header ===\n");
    printf("Next header: %u\n", opts->next_header);
    printf("Extension length: %u\n", opts->hdr_ext_len);
    printf("Option type: 0x%02X\n", opts->opt_type);
    printf("Option length: %u\n", opts->opt_len);
    // ntohll (network to host long long): Пользовательская функция для преобразования 64-битного числа из сетевого порядка в хостовый.
    printf("LOCN: 0x%016lX\n", ntohll(opts->ram_address));
}

// Обработчик клиента
void *handle_client(void *client_data)
{
    // (client_t *)client_data: Приведение типа аргумента. Потоковая функция принимает указатель общего вида (void*),
    // который здесь приводится обратно к исходному типу (указателю на client_t) для доступа к данным клиента.
    client_t *client = (client_t *)client_data;
    int sockfd = client->sockfd;
    char client_ip[INET6_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];

    inet_ntop(AF_INET6, &client->addr.sin6_addr, client_ip, sizeof(client_ip));
    printf("IPv6 клиент подключен: %s\n", client_ip);

    while (server_active)
    {
        // recv: Получает данные из сокета. Блокирует выполнение до получения данных.
        // Возвращает количество полученных байт, 0 при закрытии соединения клиентом, -1 при ошибке.
        ssize_t recv_bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);

        printf("\n[СЕРВЕР] Получен сырой пакет (%ld байт):\n---\n", recv_bytes);
        for (int i = 0; i < recv_bytes; i++)
        {
            printf("%02x ", (unsigned char)buffer[i]);
            if ((i + 1) % 16 == 0)
                printf("\n");
        }
        printf("\n---\n");

        if (recv_bytes <= 0)
        {
            if (recv_bytes < 0)
                perror("Ошибка чтения IPv6");
            break;
        }

        // Проверка на IPv6 пакет
        if (recv_bytes >= sizeof(struct ipv6_header))
        {
            // (struct ipv6_header *)buffer: Приведение типа. Указатель на начало буфера (char*) преобразуется
            // в указатель на структуру ipv6_header. Это позволяет интерпретировать
            // начальные байты полученных данных как заголовок IPv6 и обращаться к его полям.
            struct ipv6_header *ip6hdr = (struct ipv6_header *)buffer;

            if (ip6hdr->fields.version == 6)
            {
                print_ipv6_header(ip6hdr);

                // Проверка на опции назначения
                if (ip6hdr->fields.next_header == 60 &&
                    recv_bytes >= sizeof(struct ipv6_header) + sizeof(struct dest_options))
                {

                    // (struct dest_options *)(buffer + sizeof(struct ipv6_header)): Приведение типа со смещением.
                    // Указатель смещается на размер заголовка IPv6, чтобы указывать на начало следующего
                    // заголовка (в данном случае, опций назначения), и приводится к соответствующему типу.
                    struct dest_options *dest_opt = (struct dest_options *)(buffer + sizeof(struct ipv6_header));
                    print_dest_options(dest_opt);

                    // Вывод данных
                    char *payload = buffer + sizeof(struct ipv6_header) + sizeof(struct dest_options);
                    size_t payload_size = recv_bytes - sizeof(struct ipv6_header) - sizeof(struct dest_options);

                    if (payload_size > 0)
                    {
                        printf("Payload: %.*s\n", (int)payload_size, payload);
                    }
                }
            }
        }

        // Эхо-ответ был удален, сервер не отправляет ответ.
    }

    printf("IPv6 клиент отключен: %s\n", client_ip);
    // close: Закрывает файловый дескриптор сокета, освобождая системные ресурсы.
    close(sockfd);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].sockfd == sockfd)
        {
            clients[i].sockfd = -1;
            active_clients--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    return NULL;
}

// Очистка ресурсов
void cleanup_resources(int server_fd)
{
    close(server_fd);

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].sockfd != -1)
        {
            close(clients[i].sockfd);
            // pthread_join: Ожидает завершения указанного потока.
            // Это гарантирует, что все потоки клиентов завершат свою работу корректно перед остановкой сервера.
            pthread_join(clients[i].thread_id, NULL);
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    printf("Сервер IPv6 остановлен\n");
}

// Запуск сервера
void start_server()
{
    int server_fd;

    // Инициализация клиентов
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].sockfd = -1;
    }

    setup_server_socket(&server_fd);
    accept_connections(server_fd);
    cleanup_resources(server_fd);
}

// ===================== КЛИЕНТСКАЯ ЧАСТЬ =====================

// Преобразование 64-битных значений из хостового в сетевой порядок байт
uint64_t htonll(uint64_t value)
{
    // htonl (host to network long): Преобразует 32-битное число из порядка байтов хоста в сетевой.
    return ((uint64_t)htonl(value & 0xFFFFFFFF) << 32) | htonl(value >> 32);
}

// Преобразование 64-битных значений из сетевого в хостовый порядок байт
uint64_t ntohll(uint64_t value)
{
    // ntohl (network to host long): Преобразует 32-битное число из сетевого порядка байтов в хостовый.
    return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
}

// Подключение к серверу IPv6
void connect_to_ipv6_server(const char *ipv6_addr, int *sockfd)
{
    struct sockaddr_in6 server_addr;
    char addr_str[INET6_ADDRSTRLEN];
    char *zone_ptr;
    unsigned int zone_id = 0;

    // strncpy: Копирует строку, чтобы безопасно работать с ней, не изменяя исходный аргумент.
    strncpy(addr_str, ipv6_addr, sizeof(addr_str) - 1);
    addr_str[sizeof(addr_str) - 1] = '\0';

    // strchr: Ищет первое вхождение символа '%' в строке, который отделяет IPv6-адрес от идентификатора зоны (имени интерфейса).
    if ((zone_ptr = strchr(addr_str, '%')))
    {
        *zone_ptr = '\0'; // Заменяет '%' на null-терминатор, чтобы отделить адрес.
        zone_ptr++;       // Указатель теперь указывает на имя интерфейса.

        // if_nametoindex: Преобразует имя сетевого интерфейса (например, "eth0") в его системный индекс.
        // Этот индекс необходим для указания, через какой интерфейс отправлять пакеты с link-local адресами.
        if ((zone_id = if_nametoindex(zone_ptr)) == 0)
        {
            fprintf(stderr, "Ошибка: интерфейс '%s' не найден. Используйте команду 'ip link' для просмотра доступных интерфейсов\n", zone_ptr);
            exit(EXIT_FAILURE);
        }
    }

    if ((*sockfd = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
    {
        perror("Ошибка создания IPv6 сокета");
        exit(EXIT_FAILURE);
    }

    int hop_limit = 64;
    if (setsockopt(*sockfd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hop_limit, sizeof(hop_limit)))
    {
        perror("Ошибка настройки Hop Limit");
    }

    int v6only = 1;
    if (setsockopt(*sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)))
    {
        perror("Ошибка отключения IPv4");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(PORT);
    server_addr.sin6_scope_id = zone_id; // Установка ID зоны (индекса интерфейса) для link-local адресов.

    // inet_pton (presentation to network): Преобразует текстовую строку с IPv6-адресом в бинарный формат.
    if (inet_pton(AF_INET6, addr_str, &server_addr.sin6_addr) <= 0)
    {
        if (errno == 0)
        {
            fprintf(stderr, "Ошибка: '%s' не является валидным IPv6 адресом\n", addr_str);
        }
        else
        {
            perror("Ошибка inet_pton");
        }
        close(*sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Подключение к IPv6 серверу [%s%%%s]:%d...\n",
           addr_str, zone_id ? zone_ptr : "<none>", PORT);

    // connect: Устанавливает соединение с сервером по указанному адресу.
    // (struct sockaddr*)&server_addr: Приведение типа к обобщенной структуре адреса, как требует функция connect.
    if (connect(*sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Ошибка подключения IPv6");
        close(*sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Успешное подключение по IPv6\n");
}

// Отправка IPv6 пакета
void send_ipv6_packet(int sockfd, const char *message)
{
    struct ipv6_header ip6hdr;
    struct dest_options dest_opt;
    const char *payload = message;
    // strlen: Вычисляет длину строки сообщения.
    size_t payload_size = strlen(payload);

    memset(&ip6hdr, 0, sizeof(ip6hdr));
    ip6hdr.fields.version = 6;
    ip6hdr.fields.traffic_class = 0;
    // htonl (host to network long): Преобразует 32-битное число из порядка байтов хоста в сетевой.
    ip6hdr.fields.flow_label = htonl(12345) >> 12;
    ip6hdr.fields.payload_len = htons(sizeof(dest_opt) + payload_size);
    ip6hdr.fields.next_header = 60; // Destination Options
    ip6hdr.fields.hop_limit = 64;

    struct sockaddr_in6 my_addr, peer_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in6);

    // getsockname: Получает локальный адрес, к которому привязан сокет.
    // (struct sockaddr*)&my_addr: Приведение типа для передачи в функцию.
    if (getsockname(sockfd, (struct sockaddr *)&my_addr, &addr_len) == 0)
    {
        ip6hdr.fields.src_addr = my_addr.sin6_addr;
    }
    else
    {
        perror("Ошибка getsockname");
        inet_pton(AF_INET6, "::1", &ip6hdr.fields.src_addr);
    }

    // getpeername: Получает адрес удаленного узла, к которому подключен сокет.
    // (struct sockaddr*)&peer_addr: Приведение типа для передачи в функцию.
    if (getpeername(sockfd, (struct sockaddr *)&peer_addr, &addr_len) == 0)
    {
        ip6hdr.fields.dst_addr = peer_addr.sin6_addr;
    }
    else
    {
        perror("Ошибка getpeername");
        inet_pton(AF_INET6, "::1", &ip6hdr.fields.dst_addr);
    }

    memset(&dest_opt, 0, sizeof(dest_opt));
    dest_opt.next_header = 6;                          // TCP
    dest_opt.hdr_ext_len = 1;                          // Размер заголовка (1 блок по 8 байт)
    dest_opt.opt_type = 0xC2;                          // Тип опции
    dest_opt.opt_len = 8;                              // Длина данных опции
    dest_opt.ram_address = htonll(0x123456789ABCDEF0); // Пример адреса

    // Формирование пакета
    char packet[sizeof(ip6hdr) + sizeof(dest_opt) + payload_size];
    // memcpy: Копирует данные из одной области памяти в другую.
    memcpy(packet, &ip6hdr, sizeof(ip6hdr));
    memcpy(packet + sizeof(ip6hdr), &dest_opt, sizeof(dest_opt));
    memcpy(packet + sizeof(ip6hdr) + sizeof(dest_opt), payload, payload_size);

    if (send(sockfd, packet, sizeof(packet), 0) < 0)
    {
        perror("Ошибка отправки IPv6 пакета");
    }
}

// Прием сообщений
void *receive_messages(void *sock_ptr)
{
    // *((int *)sock_ptr): Разыменование указателя. Аргумент sock_ptr имеет тип void*.
    // Сначала он приводится к типу (int *), а затем оператор (*) получает значение (файловый дескриптор),
    // на которое этот указатель ссылается.
    int sockfd = *((int *)sock_ptr);
    char buffer[BUFFER_SIZE];

    while (1)
    {
        ssize_t recv_bytes = recv(sockfd, buffer, BUFFER_SIZE, 0);

        if (recv_bytes > 0)
        {
            printf("\n[СЕРВЕР] Получен сырой пакет (%ld байт):\n---\n", recv_bytes);
            for (int i = 0; i < recv_bytes; i++)
            {
                printf("%02x ", (unsigned char)buffer[i]);
                if ((i + 1) % 16 == 0)
                    printf("\n");
            }
            printf("\n---\n");
        }

        if (recv_bytes <= 0)
        {
            if (recv_bytes < 0)
            perror("Ошибка чтения IPv6");
            printf("Сервер IPv6 отключен\n");
            close(sockfd);
            // exit: Немедленно завершает программу.
            exit(0);
        }

        // Обработка IPv6 пакета
        if (recv_bytes >= sizeof(struct ipv6_header))
        {
            // (struct ipv6_header *)buffer: Приведение типа для интерпретации данных из буфера как заголовка IPv6.
            struct ipv6_header *ip6hdr = (struct ipv6_header *)buffer;

            if (ip6hdr->fields.version == 6)
            {
                char src_ip[INET6_ADDRSTRLEN], dst_ip[INET6_ADDRSTRLEN];

                inet_ntop(AF_INET6, &ip6hdr->fields.src_addr, src_ip, sizeof(src_ip));
                inet_ntop(AF_INET6, &ip6hdr->fields.dst_addr, dst_ip, sizeof(dst_ip));

                printf("\n=== Получен IPv6 пакет ===\n");
                printf("Source: %s\n", src_ip);
                printf("Destination: %s\n", dst_ip);
                printf("Payload length: %u\n", ntohs(ip6hdr->fields.payload_len));

                // Обработка опций назначения
                if (ip6hdr->fields.next_header == 60 &&
                    recv_bytes >= sizeof(struct ipv6_header) + sizeof(struct dest_options))
                {

                    // (struct dest_options *): Приведение типа указателя, смещенного на размер заголовка IPv6,
                    // для доступа к данным опций назначения.
                    struct dest_options *dest_opt = (struct dest_options *)(buffer + sizeof(struct ipv6_header));

                    printf("Option type: 0x%02X\n", dest_opt->opt_type);
                    printf("LOCN: 0x%016lX\n", ntohll(dest_opt->ram_address));

                    // Вывод данных
                    char *payload = buffer + sizeof(struct ipv6_header) + sizeof(struct dest_options);
                    size_t payload_size = recv_bytes - sizeof(struct ipv6_header) - sizeof(struct dest_options);

                    if (payload_size > 0)
                    {
                        printf("Payload: %.*s\n", (int)payload_size, payload);
                    }
                }
                printf("> ");
                // fflush: Принудительно сбрасывает буфер вывода. stdout - стандартный поток вывода.
                // Это гарантирует, что приглашение "> " будет немедленно отображено в консоли.
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
void start_client(const char *ipv6_addr)
{
    int sockfd;
    pthread_t recv_thread;

    connect_to_ipv6_server(ipv6_addr, &sockfd);

    if (pthread_create(&recv_thread, NULL, receive_messages, &sockfd))
    {
        perror("Ошибка создания потока приема");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char message[BUFFER_SIZE];
    while (1)
    {
        printf("> ");
        fflush(stdout);

        // fgets: Читает строку из указанного потока (stdin - стандартный ввод) и сохраняет ее в буфер.
        // Читает до символа новой строки или до заполнения буфера.
        if (fgets(message, BUFFER_SIZE, stdin) == NULL)
        {
            break;
        }

        // strcspn: Находит позицию первого символа из строки "\n" в строке message.
        // Используется для удаления символа новой строки, который добавляет fgets.
        message[strcspn(message, "\n")] = '\0';

        // strcmp: Сравнивает две строки. Возвращает 0, если строки равны.
        if (strcmp(message, "exit") == 0)
        {
            break;
        }

        send_ipv6_packet(sockfd, message);
    }

    close(sockfd);
    // pthread_cancel: Отправляет запрос на отмену указанному потоку.
    pthread_cancel(recv_thread);
    printf("Клиент IPv6 отключен\n");
}

// ===================== ОСНОВНАЯ ФУНКЦИЯ =====================
void get_link_local_ipv6() {
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    printf("Link-local IPv6 addresses:\n");

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET6) {  // Проверяем IPv6
            s = getnameinfo(ifa->ifa_addr,
                           sizeof(struct sockaddr_in6),
                           host, NI_MAXHOST,
                           NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                continue;
            }

            // Выводим ТОЛЬКО link-local (fe80::...)
            if (strstr(host, "fe80:") != NULL) {
                printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, host);
            }
        }
    }

    freeifaddrs(ifaddr);
}

int main()
{
    int mode;
    printf("Выберите режим:\n1. Сервер IPv6\n2. Клиент IPv6\n> ");
    // scanf: Читает форматированный ввод из стандартного потока ввода.
    // "%d": Ожидает целое десятичное число.
    // Возвращает количество успешно считанных элементов.
    if (scanf("%d", &mode) != 1)
    {
        printf("Ошибка ввода\n");
        return 1;
    }
    // getchar: Считывает один символ из стандартного потока ввода.
    // Используется здесь для "поглощения" символа новой строки, оставшегося в буфере после scanf.
    getchar();

    if (mode == 1)
    {
        get_link_local_ipv6();
        start_server();
    }
    else if (mode == 2)
    {
        char ipv6_addr[INET6_ADDRSTRLEN];
        printf("Введите IPv6 адрес сервера: ");
        if (fgets(ipv6_addr, sizeof(ipv6_addr), stdin) == NULL)
        {
            printf("Ошибка ввода\n");
            return 1;
        }
        ipv6_addr[strcspn(ipv6_addr, "\n")] = '\0';
        start_client(ipv6_addr);
    }
    else
    {
        printf("Некорректный выбор\n");
    }

    return 0;
}
