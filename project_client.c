#include "project.h"

int socket_fd;
struct board_info board;
Uint32 Event_ServerCommand;

int main(int argc, char* argv[]){
  if(argc != 6)
    error_handle("Usage: ./client [IP Address] [Port number] [color red] [color green] [color blue]\n");  //192.168.1.73

  //Variables
  SDL_Event event;
  int done = 0;
  struct color my_color;
  pthread_t comm_thr;
  struct client me;

  //Functions

  my_color = get_color(argc, argv);

  client_setup(argv);

  initial_connection(my_color, &board, &me);

  Event_ServerCommand = SDL_RegisterEvents(1);

  create_board_window(board.length, board.width);

  paint_objects(board);

  pthread_create(&comm_thr, NULL, communication_thread, &me.player_id);


  while (!done){
    while (SDL_PollEvent(&event)){
      if(event.type == SDL_QUIT) {
          done = SDL_TRUE;
          printf("Closing!!!\n");
          close(socket_fd);
      }

      else if(event.type == SDL_MOUSEMOTION){
				int x_new, y_new;

				get_board_place(event.motion.x, event.motion.y, &x_new, &y_new);

        if(x_new >= -1 && x_new <= board.length && y_new >= -1 && y_new <= board.width){
          if((x_new != me.x_pacman) || (y_new != me.y_pacman)){

            //move up
            if(x_new == me.x_pacman && y_new == me.y_pacman -1)
              send_intention(PACMAN, me.x_pacman, me.y_pacman, x_new, y_new);

            //move down
            else if(x_new == me.x_pacman && y_new == me.y_pacman +1)
              send_intention(PACMAN, me.x_pacman, me.y_pacman, x_new, y_new);

            //move left
            else if(x_new == me.x_pacman -1 && y_new == me.y_pacman)
              send_intention(PACMAN, me.x_pacman, me.y_pacman, x_new, y_new);

            //move left
            else if(x_new == me.x_pacman +1 && y_new == me.y_pacman)
              send_intention(PACMAN, me.x_pacman, me.y_pacman, x_new, y_new);
          }
        }
			}

      else if(event.type == SDL_KEYDOWN){
        if(event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_w ){
          send_intention(MONSTER, me.x_monster, me.y_monster, me.x_monster, me.y_monster-1);
        }

        else if(event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_a ){
          send_intention(MONSTER, me.x_monster, me.y_monster, me.x_monster-1, me.y_monster);
        }

        else if(event.key.keysym.sym == SDLK_DOWN || event.key.keysym.sym == SDLK_s ){
          send_intention(MONSTER, me.x_monster, me.y_monster, me.x_monster, me.y_monster+1);
        }

        else if(event.key.keysym.sym == SDLK_RIGHT || event.key.keysym.sym == SDLK_d ){
          send_intention(MONSTER, me.x_monster, me.y_monster, me.x_monster+1, me.y_monster);
        }
      }

      else if(event.type == Event_ServerCommand){
        struct move* data_ptr;
        data_ptr = event.user.data1;

        obey_server(*data_ptr, me.player_id, &me.x_pacman, &me.y_pacman, &me.x_monster, &me.y_monster);


        free(event.user.data1);
      }

    }
  }

	close_board_windows();
  client_data_cleanup();
  return 0;
}

///////////////////////////////////////////////////////////////////////////////
void obey_server(struct move server_command, int player_id, int* x_pacman, int* y_pacman, int* x_monster, int* y_monster){

  if(server_command.place2.type == PACMAN){
    if(server_command.place2.owner == player_id){
      *x_pacman = server_command.place2.x;
      *y_pacman = server_command.place2.y;
    }
  }
  else if(server_command.place2.type == MONSTER){
    if(server_command.place2.owner == player_id){
      *x_monster = server_command.place2.x;
      *y_monster = server_command.place2.y;
    }
  }
  if(server_command.place1.type == PACMAN){
    if(server_command.place1.owner == player_id){
      *x_pacman = server_command.place1.x;
      *y_pacman = server_command.place1.y;
    }
  }
  else if(server_command.place1.type == MONSTER){
    if(server_command.place1.owner == player_id){
      *x_monster = server_command.place1.x;
      *y_monster = server_command.place1.y;
    }
  }

  paint_tile(server_command.place1);
  paint_tile(server_command.place2);
}

////////////////////////////////////////////////////////////////////////////////
void send_intention(int character, int current_x, int current_y, int wanted_x, int wanted_y){

  struct client_intent intention;
  intention.character = character;
  intention.current_x = current_x;
  intention.current_y = current_y;
  intention.wanted_x = wanted_x;
  intention.wanted_y = wanted_y;

  send(socket_fd, &intention, sizeof(struct client_intent), 0);
}

////////////////////////////////////////////////////////////////////////////////
void client_setup(char* argv[]){
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(socket_fd == 0)
    error_handle("Failed to socket\n");

  struct sockaddr_in other_addr;

  other_addr.sin_family = AF_INET;
  other_addr.sin_port = htons(atoi(argv[2]));
  inet_aton(argv[1], &other_addr.sin_addr);

  if(connect(socket_fd, (const struct sockaddr *) &other_addr, sizeof(other_addr)) == -1 )
    error_handle("Failed to connect\n");

  printf("Client setup successful\n");
}

////////////////////////////////////////////////////////////////////////////////
void* communication_thread(void* arg){

  int my_id = *((int*)arg);

  int ret_val;
  struct move change;
  struct move* event_data;
  SDL_Event new_event;



  while(1){

    ret_val = read(socket_fd, &change, sizeof(struct move));

    if(ret_val <= 0){
      disconnect();
      return NULL;
    }

    if(change.type == REGULAR_MESSAGE){

      event_data = (struct move*) malloc(sizeof(struct move));
      *event_data = change;
      SDL_zero(new_event);
      new_event.type = Event_ServerCommand;
      new_event.user.data1 = event_data;
      SDL_PushEvent(&new_event);
    }

    else if(change.type == SCORE_MESSAGE){

      int player_id = change.place1.owner;
      int score = change.place2.owner;

      if(change.place1.type == FIRST)
        printf("\n---------------SCORE---------------\n");

      if(player_id == my_id)
        printf("(ME)Player %d: %d points\n", player_id, score);

      else
        printf("Player %d: %d points\n", player_id, score);
    }

  }

  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
void initial_connection(struct color my_color, struct board_info* board, struct client* me){
  //Handles the initial connection; sends the color and receives the board
  int ret_val;

  //Sends the color
  send(socket_fd, &my_color, sizeof(struct color), 0);

  //Receives the size of the window
  int length, width;
  ret_val = read(socket_fd, &length, sizeof(int));
  if(ret_val <= 0){
    close(socket_fd);
    error_handle("Unable to read board info from the server\n");
  }


  ret_val = read(socket_fd, &width, sizeof(int));
  if(ret_val <= 0){
    close(socket_fd);
    error_handle("Unable to read board info from the server\n");
  }

  board->length = length;
  board->width = width;

  //Creates the board
  struct tile** board_matrix = (struct tile**) malloc(sizeof(struct tile) * width);
  for(int i=0; i < width; i++)
    board_matrix[i] = (struct tile*) malloc(sizeof(struct tile) * length);

  //Receives board information from the server
  for(int i=0; i<width; i++){
    for(int j=0; j<length; j++){
      read(socket_fd, &board_matrix[i][j], sizeof(struct tile));
    }
  }

  board->board_matrix = board_matrix;

  //Gets information about oneself
  read(socket_fd, me, sizeof(struct client));

}

///////////////////////////////////////////////////////////////////////////////
void error_handle(char error_message[]){
  perror(error_message);
  exit(-1);
}

///////////////////////////////////////////////////////////////////////////////
struct color get_color(int argc, char* argv[]){
  struct color my_color;

  if(sscanf(argv[3], "%d", &my_color.r) != 1)
    error_handle("Failure to read color red");

  if(sscanf(argv[4], "%d", &my_color.g) != 1)
    error_handle("Failure to read color green");

  if(sscanf(argv[5], "%d", &my_color.b) != 1)
    error_handle("Failure to read color blue");


  return my_color;
}

///////////////////////////////////////////////////////////////////////////////
void paint_objects(struct board_info board){
  //Percorre a matrix board e pinta todos os objetos
  for(int i=0; i < board.width; i++){
    for(int j=0; j < board.length; j++){

      if(board.board_matrix[i][j].type == BRICK)
    	  paint_brick(j, i);

      else if(board.board_matrix[i][j].type == PACMAN)
     	  paint_pacman(j, i, board.board_matrix[i][j].tile_color.r, board.board_matrix[i][j].tile_color.g, board.board_matrix[i][j].tile_color.b);


      else if(board.board_matrix[i][j].type == SUPER_PACMAN)  //TODO meter o SUPER_PACMAN
        paint_pacman(j, i, board.board_matrix[i][j].tile_color.r, board.board_matrix[i][j].tile_color.g, board.board_matrix[i][j].tile_color.b);

      else if(board.board_matrix[i][j].type == MONSTER)
        paint_monster(j, i, board.board_matrix[i][j].tile_color.r, board.board_matrix[i][j].tile_color.g,  board.board_matrix[i][j].tile_color.b);

      else if(board.board_matrix[i][j].type == CHERRY)
        paint_cherry(j, i);

      else if(board.board_matrix[i][j].type == LEMON)
        paint_lemon(j, i);

    }
  }
}

////////////////////////////////////////////////////////////////////////////////
void paint_tile(struct tile t){
  switch (t.type) {
    case EMPTY:
      clear_place(t.x, t.y);
      break;

    case PACMAN:
      if(t.super_bites == 0)
        paint_pacman(t.x, t.y, t.tile_color.r, t.tile_color.g, t.tile_color.b);

      else
        paint_powerpacman(t.x, t.y, t.tile_color.r, t.tile_color.g, t.tile_color.b);
      break;

    case MONSTER:
      paint_monster(t.x, t.y, t.tile_color.r, t.tile_color.g, t.tile_color.b);
      break;

    case CHERRY:
      paint_cherry(t.x, t.y);
      break;

    case LEMON:
      paint_lemon(t.x, t.y);
      break;

    case BRICK:
      paint_brick(t.x, t.y);
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////
void disconnect(void){

  SDL_Event end_event;

  printf("Server disconnected\n");

  close(socket_fd);

  SDL_zero(end_event);
  end_event.type = SDL_QUIT;
  SDL_PushEvent(&end_event);

}

////////////////////////////////////////////////////////////////////////////////
void client_data_cleanup(){

  for(int i=0; i<board.width; i++){
    free(board.board_matrix[i]);
  }

  free(board.board_matrix);
}
