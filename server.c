#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include "game.h"

// This is the server that handles all of the connections and networking for clients

#define MAX_CLIENTS 1024
#define MAX_MESSAGE_LENGTH 256 // use later to limit message size
#define MAX_COMMAND_LENGTH 100

// client that also has a game state
typedef struct client {
    int fd;
    size_t command_length;
    char command[MAX_COMMAND_LENGTH + 1];

    player_t player;
} client_t;

typedef struct {
    int victim_fd;
    char victim_name[MAX_NAME_LENGTH + 1];
    int sunk;
} bomb_hit_t;

bomb_hit_t bomb_hits[MAX_CLIENTS];

//initialize global client array
client_t clients[MAX_CLIENTS] = {0};
int client_count = 0;

static int make_server(int family, int port, int socket_type) {

    int sfd = socket(family, socket_type, 0);

    if (sfd == -1) {
        return -1;
    }

    struct sockaddr_in local = {0};

    local.sin_family = family;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(sfd, (struct sockaddr *)&local, sizeof(local)) == -1) {
        close(sfd);
        return -1;
    }

    if (listen(sfd, MAX_CLIENTS) == -1) {
        close(sfd);
        return -1;
    }

    return sfd;
}

static int name_is_taken(char *name)
{
    for (int i = 0; i < client_count; i++) {
        if (clients[i].player.registered &&
            strcmp(clients[i].player.name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

static client_t *get_client(int fd) {

    for (ssize_t i = 0; i < client_count; i++) {
        if (clients[i].fd == fd) {
            return &clients[i];
        }
    }

    return NULL;
}

// singular message send
static int send_message(client_t *client, const char *message) {

    ssize_t written = write(client->fd, message, strlen(message));

    return written == strlen(message);
}

static void broadcast_all(int epoll_fd, char *message); // so gg compiles

static void gg_client(int epoll_fd, char *name) {

    char gg_message[MAX_NAME_LENGTH + 6];
    snprintf(gg_message, sizeof(gg_message), "GG %s\n", name);
    broadcast_all(epoll_fd, gg_message);

}

static void kick_client(int epoll_fd, client_t *client) {

    int fd = client->fd;
    size_t client_index = (size_t)(client - clients);

    int was_registered = client->player.registered;
    char temp_name[MAX_NAME_LENGTH + 1];

    if (was_registered) {
        strcpy(temp_name, client->player.name);
    }

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);

    client_count--;

    if (client_index < (size_t)client_count) {
        clients[client_index] = clients[client_count];
    }

    if (was_registered) {
        gg_client(epoll_fd, temp_name);
    }
}

// send message to all
static void broadcast_all(int epoll_fd, char *message) { // dont broadcast to non registered clients

    // fd snapshot to prevent the recursive loop
    int recipient_fds[MAX_CLIENTS];
    int recipient_count = 0;

    for (int i = 0; i < client_count; i++) {

        if (clients[i].player.registered) {
            recipient_fds[recipient_count++] = clients[i].fd;
        }
    }

    for (int i = 0; i < recipient_count; i++) {

        client_t *client = get_client(recipient_fds[i]);

        if (!client || !client->player.registered) {
            continue;
        }

        if (!send_message(client, message)) {
            kick_client(epoll_fd, client);
        }
    }
}

static void accept_client(int sfd, int epoll_fd) {

    int new_cfd = accept(sfd, NULL, NULL);

    if (new_cfd == -1) {
        return;
    }

    int flags = fcntl(new_cfd, F_GETFL, 0);

    if (flags == -1 || fcntl(new_cfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        close(new_cfd);
        return;
    }

    if (client_count >= MAX_CLIENTS) {
        perror("client overflow");
        close(new_cfd);

        return;
    }

    struct epoll_event event = {
        .events = EPOLLIN,
        .data.fd = new_cfd
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_cfd, &event) == -1) {

        close(new_cfd);
        return;
    }

    clients[client_count++] = (client_t) {
        .fd = new_cfd,
        .command_length = 0,
        .player = {
            .registered = 0
        }
    };
}

static void eliminate(int epoll_fd, client_t *client) {

    int fd = client->fd;
    char gg_message[MAX_NAME_LENGTH + 6];

    snprintf(gg_message, sizeof(gg_message), "GG %s\n", client->player.name);

    // failed send doesnt matter because client will be disconnected anyway
    send_message(client, gg_message);

    client->player.registered = 0; // avoid double announcement

    broadcast_all(epoll_fd, gg_message);

    client = get_client(fd);

    if (client) {
        kick_client(epoll_fd, client);
    }
}

static void process_command(int epoll_fd, client_t *client, char *command) {

    player_t *player = &client->player;

    if (!player->registered) {
        // only REG is valid

        char name[MAX_NAME_LENGTH + 1];
        int x;
        int y;
        char direction;


        if (!complete_registration(command, name, &x, &y, &direction)) {

            if (!send_message(client, "INVALID\n")) {
                kick_client(epoll_fd, client);
                return;
            }

            return;
        }

        if (name_is_taken(name)) {

            if (!send_message(client, "TAKEN\n")) {
                kick_client(epoll_fd, client);
                return;
            }
            return;
        }

        // everything checks out
        strcpy(player->name, name);
        player->ship_x = x;
        player->ship_y = y;
        player->direction = direction;
        memset(player->hits, 0, sizeof(player->hits));
        player->registered = 1;

        // welcome client
        if (!send_message(client, "WELCOME\n")) {
            kick_client(epoll_fd, client);
            return;
        }

        // tell everyone in the game that the player joined
        char join_message[MAX_NAME_LENGTH + 8];
        snprintf(join_message, sizeof(join_message), "JOIN %s\n", player->name);
        broadcast_all(epoll_fd, join_message);

        return;
    }

    // player is registered now and only BOMB allowed

    int x, y;

    if (!parse_bomb(command, &x, &y)) {

        if (!send_message(client, "INVALID\n")) {
            kick_client(epoll_fd, client);
            return;
        }

        return;
    }

    char attacker_name[MAX_NAME_LENGTH + 1];
    strcpy(attacker_name, player->name);

    int num_ships_hit = 0;

    // loop through all players to see if their ship was hit

    for (int i = 0; i < client_count; i++) {

        client_t *victim = &clients[i];

        if (!victim->player.registered) {
            continue;
        }

        int hit_location = where_is_ship_hit(&victim->player, x, y);

        if (hit_location == -1) {
            continue;
        }

        victim->player.hits[hit_location] = 1;

        bomb_hits[num_ships_hit].victim_fd = victim->fd;

        strcpy(bomb_hits[num_ships_hit].victim_name, victim->player.name);

        bomb_hits[num_ships_hit].sunk = ship_is_sunk(&victim->player);

        num_ships_hit++;
    }

    for (int i = 0; i < num_ships_hit; i++) {

        char hit_message[MAX_MESSAGE_LENGTH];

        snprintf(hit_message, sizeof(hit_message), "HIT %s %d %d %s\n", attacker_name, x, y, bomb_hits[i].victim_name);

        broadcast_all(epoll_fd, hit_message);

        if (bomb_hits[i].sunk) {
            client_t *victim = get_client(bomb_hits[i].victim_fd);

            if (victim && victim->player.registered) {
                eliminate(epoll_fd, victim);
            }
        }
    }

    if (num_ships_hit == 0) {

        char miss_message[MAX_MESSAGE_LENGTH];

        snprintf(miss_message, sizeof(miss_message), "MISS %s %d %d\n", attacker_name, x, y);

        broadcast_all(epoll_fd, miss_message);
    }
}

static void handle_client_input(int epoll_fd, int cfd) {

    client_t *current_client = get_client(cfd);

    if (!current_client) {
        perror("client is null");

        return;
    }

    char msg_buffer[1024];
    ssize_t bytes_read = read(current_client->fd, msg_buffer, sizeof(msg_buffer));

    if (bytes_read == 0) {

        kick_client(epoll_fd, current_client);

        return;
    }

    // since nonblocking
    if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }

        kick_client(epoll_fd, current_client);
        return;
    }

    // validate command length
    for (ssize_t j = 0; j < bytes_read; j++) {

        char ch = msg_buffer[j];

        if (ch == '\n') {

            current_client->command[current_client->command_length] = '\0';

            process_command(epoll_fd, current_client, current_client->command);

            // check that client still exists (if they sank themselves)

            current_client = get_client(cfd);

            if (!current_client) {
                return;
            }

            current_client->command_length = 0;

        } else {

            // check command length is valid

            if (current_client->command_length >= MAX_COMMAND_LENGTH) {

                // kick client
                kick_client(epoll_fd, current_client);
                break;
            }

            // add char to command
            current_client->command[current_client->command_length] = msg_buffer[j];
            current_client->command_length++;
        }

    }
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        perror("not enough args");
        return 1;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    // create a socket for the server
    int sfd = make_server(AF_INET, atoi(argv[1]), SOCK_STREAM);

    if (sfd == -1) {
        return 1;
    }

    //define epoll stuff
    int epoll_fd;
    struct epoll_event ev = {0};
    struct epoll_event events[MAX_CLIENTS + 1];
    int num_ready;

    epoll_fd = epoll_create1(0);

    if (epoll_fd == -1) {
        perror("epoll create failed");

        close(sfd);
        return 1;
    }

    // make the sfd epoll item to then track it
    ev.events = EPOLLIN;
    ev.data.fd = sfd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sfd, &ev) == -1) {
        perror("sfd epoll add failed");

        close(epoll_fd);
        close(sfd);
        return 1;
    }

    for (;;) {

        // get epoll events
        num_ready = epoll_wait(epoll_fd, events, MAX_CLIENTS, -1);

        if (num_ready == -1) {

            if (errno == EINTR) {
                continue;
            }

            break;
        }

        // go through each event
        for (int i = 0; i < num_ready ; i++) {

            // add a client
            if (events[i].data.fd == sfd) {
                accept_client(sfd, epoll_fd);

            } else { // a client said something
                handle_client_input(epoll_fd, events[i].data.fd);
            }
        }
    }

    close(epoll_fd);
    close(sfd);

    return 0;
}
