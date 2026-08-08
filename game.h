#ifndef GAME_H
#define GAME_H

#define MAX_NAME_LENGTH 20
#define SHIP_LENGTH 5

typedef struct player {
    int registered;
    char name[MAX_NAME_LENGTH + 1];

    int ship_x;
    int ship_y;
    char direction;

    int hits[SHIP_LENGTH]; // 1 if hit, 0 if not hit
} player_t;

int complete_registration(char *command, char name[MAX_NAME_LENGTH + 1], int *x, int *y, char *direction);

int ship_is_sunk(const player_t *player);

int where_is_ship_hit(const player_t *player, int x, int y);

int parse_bomb(const char *command, int *x, int *y);

#endif