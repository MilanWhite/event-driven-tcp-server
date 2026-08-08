#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "game.h"

int valid_name(char *name) {
    size_t length = strlen(name);

    if (length == 0 || length > MAX_NAME_LENGTH) {
        return 0;
    }

    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)name[i];

        // name must be full alphanum including dashes
        if (!isalnum(ch) && ch != '-') {
            return 0;
        }
    }

    return 1;
}

int valid_ship(int x, int y, char direction){
    if (direction == '-') {
        return x >= 2 && x <= 7 && y >= 0 && y <= 9;
    }

    if (direction == '|') {
        return x >= 0 && x <= 9 && y >= 2 && y <= 7;
    }

    return 0;
}

int complete_registration(char *command, char name[MAX_NAME_LENGTH + 1], int *x, int *y, char *direction) {

    char extra_input;

    if (strncmp(command, "REG", 3) != 0 || !isspace((unsigned char)command[3])) {
        return 0;
    } // double check the actual command and verify space after it

    // parse an extra char to check that there werent more than 4 fields
    int registration_fields = sscanf(command, "REG %20s %d %d %c %c", name, x, y, direction, &extra_input);

    if (registration_fields != 4) {
        return 0;
    }

    return valid_name(name) && valid_ship(*x, *y, *direction);
}

int parse_bomb(const char *command, int *x, int *y) {
    char extra;

    if (strncmp(command, "BOMB", 4) != 0 || !isspace((unsigned char)command[4])) {
        return 0;
    } // double check the actual command and verify space after it

    return sscanf(command, "BOMB %d %d %c", x, y, &extra) == 2; // check to make sure there arent any extra args
}

int ship_is_sunk(const player_t *player) {
    for (int i = 0; i < SHIP_LENGTH; i++) {
        if (player->hits[i] == 0) {
            return 0;
        }
    }

    return 1;
}

int where_is_ship_hit(const player_t *player, int x, int y) {

    if (player->direction == '-') {
        if (y != player->ship_y) {
            return -1; // needs to be same level
        }

        int left_of_ship = player->ship_x - 2;

        if (x < left_of_ship || x > left_of_ship + 4) {
            return -1;
        }

        return x - left_of_ship;
    }

    if (player->direction == '|') {
        if (x != player->ship_x) {
            return -1; // same
        }

        int top_of_ship = player->ship_y - 2;

        if (y < top_of_ship || y > top_of_ship + 4) {
            return -1;
        }

        return y - top_of_ship;
    }

    return -1;
}