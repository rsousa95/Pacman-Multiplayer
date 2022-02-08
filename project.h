#include <SDL2/SDL.h>
#include "UI_library.h"

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>

#define EMPTY 0
#define PACMAN 1
#define SUPER_PACMAN 2
#define MONSTER 3
#define CHERRY 4
#define LEMON 5
#define BRICK 6

#define REGULAR_MESSAGE 1
#define SCORE_MESSAGE 2

#define FIRST 1

//  /sbin/ifconfig
// IP address: 192.168.1.73
// Port: 3000

//STRUCTS//////////////////////////////////////////////////////////////////////
struct color{
  int r, g, b;
};

struct tile{
  int type;
  struct color tile_color;
  int owner;
  int super_bites;
  int x, y;
};

struct move{
  int type;
  struct tile place1, place2;
};


struct client{
  int client_fd;
  int player_id;
  struct color client_color;
  int x_pacman, y_pacman;
  int x_monster, y_monster;
  int score;
};

struct client_intent{
  int character;
  int current_x, current_y;
  int wanted_x, wanted_y;
};


struct board_info{
  int length, width;
  struct tile** board_matrix;
  int max_fruit;
};

struct inactivity_arg{
  int* active;
  int character;
  int client_fd;
};


//SERVER PROTOTYPES////////////////////////////////////////////////////////////
void error_handle(char error_message[]);
int read_file(struct board_info* board);
void place_bricks(struct board_info board);
void* accept_thread(void* arg);
void* work_thread(void* arg);
int server_setup(int bricks);
void find_free_tile(int* free_x, int* free_y);
struct client new_client(int client_fd);
int resolve(struct client_intent intent, struct move* effect);
void swap_tiles(struct tile* place1, struct tile* place2);
void print_board(void);
void client_disconnect(int client_fd);
void deliver_or_trim_fruit(void);
void* new_fruit_thread(void* arg);
void respawn_character(struct tile character);
void increase_score(int player_id);
void* score_thread(void* arg);
void send_to_all_clients(struct move msg);
void set_lone_client_score(void);
void* inactivity_jump_thread(void* arg);
void initialize_mutexes(void);
void server_data_cleanup();
void update_server_window(struct move msg);

//CLIENT PROTOTYPES/////////////////////////////////////////////////////////////
void client_setup(char* argv[]);
void error_handle(char error_message[]);
struct color get_color(int argc, char* argv[]);
void initial_connection(struct color my_color, struct board_info* board, struct client* me);
void paint_objects(struct board_info board);
void send_intention(int character, int current_x, int current_y, int wanted_x, int wanted_y);
void* communication_thread(void* arg);
void paint_tile(struct tile t);
void obey_server(struct move server_command, int player_id, int* x_pacman, int* y_pacman, int* x_monster, int* y_monster);
void disconnect(void);
void client_data_cleanup();
