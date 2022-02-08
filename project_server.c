#include "project.h"

//Global Variables /////////////////////////////////////////////////////////////
Uint32 Event_ServerCommand;

int socket_fd;

struct board_info board;

int total_clients = 0;
int active_clients = 0;
struct client* client_vector;

pthread_rwlock_t* line_lock;
pthread_rwlock_t client_vector_mutex;
pthread_mutex_t fruit_mutex, total_clients_mutex;

////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[]){

  SDL_Event event;
  int done = 0;
  int total_bricks, max_clients;

  pthread_t accept_thr, score_thr;

  total_bricks = read_file(&board);

  initialize_mutexes();

  max_clients = server_setup(total_bricks);

  pthread_create(&accept_thr, NULL, accept_thread, &max_clients);

  pthread_create(&score_thr, NULL, score_thread, NULL);

  create_board_window(board.length, board.width);

  paint_objects(board);

  Event_ServerCommand = SDL_RegisterEvents(1);


  while (!done){
    while (SDL_PollEvent(&event)){
      if(event.type == SDL_QUIT) {
          done = SDL_TRUE;
          printf("Ending the game\n");
          close(socket_fd);
      }

      if(event.type == Event_ServerCommand){
        struct move* data_ptr;
        data_ptr = event.user.data1;
        paint_tile((*data_ptr).place1);
        paint_tile((*data_ptr).place2);
        free(event.user.data1);
      }

    }
  }

	close_board_windows();

  pthread_cancel(accept_thr);
  pthread_cancel(score_thr);
  server_data_cleanup();

  return 0;
}



///////////////////////////////////////////////////////////////////////////////
int server_setup(int bricks){

  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(socket_fd == 0)
    error_handle("Failed to socket\n");

  struct sockaddr_in local_addr;

  memset(&local_addr, 0, sizeof(struct sockaddr_in));
  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = INADDR_ANY;
  local_addr.sin_port = htons(3000);

  int ret_val = bind(socket_fd, (struct sockaddr*)&local_addr, sizeof(struct sockaddr_in));
  if(ret_val < 0)
    error_handle("Fail to bind\n");

  int max_clients = (board.width*board.length - bricks +1) / 4;

  ret_val = listen(socket_fd, max_clients);
  if(ret_val < 0)
    error_handle("Error in listen\n");

  printf("Server setup successful\n");

  return max_clients;
}

///////////////////////////////////////////////////////////////////////////////
void* accept_thread(void* arg){

  int max_clients = *((int*)arg);

  int client_fd;
  struct sockaddr_in other_addr;
  socklen_t len = sizeof(struct sockaddr_in);
  pthread_t thread_id;
  struct client my_client;

  printf("Waiting for client to connect\n");

  while(1){

    client_fd = accept(socket_fd, (struct sockaddr*)&other_addr, &len);
    if(client_fd < 0)
      error_handle("Error in accept\n");
    else
      printf("Accepted a new client ");

    if(active_clients >= max_clients){
      printf("but the game is full - client kicked. Try again later\n");
      close(client_fd);
      continue;
    }


    my_client = new_client(client_fd);

    if(my_client.client_fd < 0)
      continue;

    printf("- Player id: %d\n", my_client.player_id);

    deliver_or_trim_fruit();

    pthread_create(&thread_id, NULL, work_thread, &my_client);

  }

  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
struct client new_client(int client_fd){
  //Reads color, assigns the pacman and monster of the client, send to the client

  int free_x, free_y;
  struct client new_client;
  struct color client_color;
  struct tile pacman_tile, monster_tile;
  struct move msg;
  int empty_spot = 0;


  //Reads the color of the client
  int bytes = read(client_fd, &client_color, sizeof(client_color));
  if(bytes != sizeof(struct color)){
    printf("Error reading the client's color - kicking client\n");
    close(client_fd);
    new_client.client_fd = -1;
    return new_client;
  }

  new_client.client_color = client_color;
  new_client.player_id = client_fd;
  new_client.client_fd = client_fd;
  new_client.score = 0;


  //Sends the dimensions of the board
  int length = board.length;
  int width = board.width;


  send(client_fd, &length, sizeof(int), 0);

  send(client_fd, &width, sizeof(int), 0);

  //Finds two empty spots for the clients' characters
  find_free_tile(&free_x, &free_y);
  pacman_tile.type = PACMAN;
  pacman_tile.tile_color = client_color;
  pacman_tile.x = free_x;
  pacman_tile.y = free_y;
  pacman_tile.owner = new_client.player_id;
  pacman_tile.super_bites = 0;

  new_client.x_pacman = free_x;
  new_client.y_pacman = free_y;


  pthread_rwlock_wrlock(&line_lock[pacman_tile.y]);
  board.board_matrix[pacman_tile.y][pacman_tile.x] = pacman_tile;
  pthread_rwlock_unlock(&line_lock[pacman_tile.y]);

  find_free_tile(&free_x, &free_y);
  monster_tile.type = MONSTER;
  monster_tile.tile_color = client_color;
  monster_tile.x = free_x;
  monster_tile.y = free_y;
  monster_tile.owner = new_client.player_id;
  monster_tile.super_bites = 0;

  new_client.x_monster = free_x;
  new_client.y_monster = free_y;


  pthread_rwlock_wrlock(&line_lock[monster_tile.y]);
  board.board_matrix[monster_tile.y][monster_tile.x] = monster_tile;
  pthread_rwlock_unlock(&line_lock[monster_tile.y]);


  //Send the information of the board to the client
  for(int i=0; i<board.width; i++){
    pthread_rwlock_rdlock(&line_lock[i]);
    for(int j=0; j<board.length; j++){
      send(client_fd, &board.board_matrix[i][j], sizeof(struct tile), 0);
    }
    pthread_rwlock_unlock(&line_lock[i]);
  }

  //Send the new client its information
  send(client_fd, &new_client, sizeof(struct client), 0);


  //Updates the client vector

  //Checks if there is a free spot on the client vector
  pthread_rwlock_wrlock(&client_vector_mutex);
  for(int i=0; i<total_clients; i++){
    if(client_vector[i].client_fd < 0){
      client_vector[i] = new_client;
      active_clients++;
      //pthread_rwlock_unlock(&client_vector_mutex);
      empty_spot = 1;

      //return new_client;        //TODO este return não impede de enviar o que está à frente?
    }
  }

  if(empty_spot == 0){
    //If not, increases the vector
    total_clients++;
    active_clients++;

    client_vector = (struct client*) realloc(client_vector, sizeof(struct client)*total_clients );
    client_vector[total_clients -1] = new_client;
  }

  pthread_rwlock_unlock(&client_vector_mutex);


  //Envia as novas personagens para os outros clientes
  msg.type = REGULAR_MESSAGE;
  msg.place1 = pacman_tile;
  msg.place2 = monster_tile;

  update_server_window(msg);

  pthread_rwlock_rdlock(&client_vector_mutex);
  for(int i=0; i < total_clients; i++){
    if(client_vector[i].client_fd > 0 && client_vector[i].client_fd != client_fd){
      send(client_vector[i].client_fd, &msg, sizeof(struct move), 0);
    }
  }
  pthread_rwlock_unlock(&client_vector_mutex);

  return new_client;
}

////////////////////////////////////////////////////////////////////////////////
void client_disconnect(int client_fd){

  close(client_fd);

  //Actualizar o vetor dos clientes

  pthread_rwlock_wrlock(&client_vector_mutex);
  active_clients--;

  for(int i=0; i < total_clients; i++){
    if(client_vector[i].client_fd == client_fd){
      client_vector[i].client_fd = -1;
      client_vector[i].player_id = -1;
    }
  }
  pthread_rwlock_unlock(&client_vector_mutex);

  //Actualizar o board
  struct move tiles_to_remove;

  tiles_to_remove.type = REGULAR_MESSAGE;

  int characters_to_remove = 2;
  int stop = 0;

  for(int i=0; i<board.width && stop == 0; i++){

    for(int j=0; j<board.length && stop == 0; j++){
      pthread_rwlock_wrlock(&line_lock[i]);

      if(board.board_matrix[i][j].owner == client_fd){

        if(board.board_matrix[i][j].type == PACMAN){
          board.board_matrix[i][j].type = EMPTY;
          board.board_matrix[i][j].owner = -1;

          tiles_to_remove.place1 = board.board_matrix[i][j];
          characters_to_remove--;
        }

        else if(board.board_matrix[i][j].type == MONSTER){
          board.board_matrix[i][j].type = EMPTY;
          board.board_matrix[i][j].owner = -1;

          tiles_to_remove.place2 = board.board_matrix[i][j];
          characters_to_remove--;
        }
      }

      pthread_rwlock_unlock(&line_lock[i]);

      if(characters_to_remove == 0)
        stop = 1;
    }
  }


  send_to_all_clients(tiles_to_remove);
  update_server_window(tiles_to_remove);
  deliver_or_trim_fruit();

}

////////////////////////////////////////////////////////////////////////////////
void* work_thread(void* arg){
  struct client my_client = *((struct client*) arg);
  int client_fd = my_client.client_fd;

  int ret_val;

  struct client_intent intent;
  struct move server_command;
  int valid_movement;
  server_command.type = REGULAR_MESSAGE;


  //Para contar o max move speed
  struct timespec start_pacman, start_monster, stop_pacman, stop_monster;
  double interval;
  int count_pacman = 0;
  int count_monster = 0;


  //Para o inactivity jump
  pthread_t inactivity_jump_pacman;
  pthread_t inactivity_jump_monster;

  int active_pacman = 0;
  int active_monster = 0;

  struct inactivity_arg inactive_pac_arg;
  inactive_pac_arg.active = &active_pacman;
  inactive_pac_arg.character = PACMAN;
  inactive_pac_arg.client_fd = client_fd;

  struct inactivity_arg inactive_mons_arg;
  inactive_mons_arg.active = &active_monster;
  inactive_mons_arg.character = MONSTER;
  inactive_mons_arg.client_fd = client_fd;

  pthread_create(&inactivity_jump_pacman, NULL, inactivity_jump_thread, &inactive_pac_arg);
  pthread_create(&inactivity_jump_monster, NULL, inactivity_jump_thread, &inactive_mons_arg);

  while(1){

    //Lê as intenções do cliente
    ret_val = read(client_fd, &intent, sizeof(struct client_intent));

    //Client disconnect
    if(ret_val <= 0){
      printf("Client %d disconnected\n", my_client.player_id);
      client_disconnect(client_fd);

      active_pacman = -1;
      active_monster = -1;

      sleep(3);   //Isto é para a as threads do inactivity jump acabarem
      return NULL;
    }

    //Max movement speed restriction do Pacman
    if(intent.character == PACMAN){

      active_pacman = 1;

      if(count_pacman == 0)
        clock_gettime(CLOCK_REALTIME, &start_pacman);

      count_pacman++;

      if(count_pacman > 1){
        clock_gettime(CLOCK_REALTIME, &stop_pacman);
        interval = (stop_pacman.tv_sec - start_pacman.tv_sec)+(stop_pacman.tv_nsec - start_pacman.tv_nsec)*1e-9;

        if(interval < 0.5)
          continue;

        else{
          count_pacman = 1;
          clock_gettime(CLOCK_REALTIME, &start_pacman);
        }
      }
    }

    //Max movement speed restriction do Monstro
    else if(intent.character == MONSTER){

      active_monster = 1;

      if(count_monster == 0)
        clock_gettime(CLOCK_REALTIME, &start_monster);

      count_monster++;

      if(count_monster > 1) {
        clock_gettime(CLOCK_REALTIME, &stop_monster);


        interval = (stop_monster.tv_sec - start_monster.tv_sec)+(stop_monster.tv_nsec - start_monster.tv_nsec)*1e-9;

        if(interval < 0.5){
          continue;
        }
        else{
          count_monster = 1;
          clock_gettime(CLOCK_REALTIME, &start_monster);
        }
      }
    }

    //Determina o efeito da ação pretendida do cliente
    valid_movement = resolve(intent, &server_command);

    if(valid_movement){
      send_to_all_clients(server_command);
      update_server_window(server_command);
    }

  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
int resolve(struct client_intent intent, struct move* effect){

  int valid;

  int checked = 0;

  int current_x = intent.current_x;
  int current_y = intent.current_y;
  int wanted_x = intent.wanted_x;
  int wanted_y = intent.wanted_y;

  struct tile matrix_current, matrix_target;

  pthread_rwlock_rdlock(&line_lock[current_y]);
  matrix_current = board.board_matrix[current_y][current_x];
  pthread_rwlock_unlock(&line_lock[current_y]);

  //If the target is outside the board
  if(wanted_x < 0 || wanted_x >= board.length || wanted_y < 0|| wanted_y >= board.width){

    //Hitting left wall
    if(wanted_x < 0){
      //Is the bounce location inside the board?
      if(current_x + 1 < board.length){
        wanted_x = current_x + 1;
      }
      else{
        return 0;  //Bounce location outside the board
      }
    }

    //Hitting right wall
    else if(wanted_x >= board.length){
      //Is the bounce location inside the board?
      if(current_x - 1 >= 0){
        wanted_x = current_x - 1;
      }
      else{
        return 0;
      }
    }

    //Hitting top wall
    else if(wanted_y < 0){
      //Is the bounce location inside the board?
      if(current_y + 1 < board.width){
        wanted_y = current_y + 1;
      }
      else{
        return 0;
      }
    }

    //Hitting bottom wall
    else if(wanted_y >= board.width){
      //Is the bounce location inside the board
      if(current_y - 1 >= 0){
        wanted_y = current_y - 1;
      }
      else{
        return 0;
      }
    }

    pthread_rwlock_rdlock(&line_lock[wanted_y]);
    matrix_target = board.board_matrix[wanted_y][wanted_x];
    pthread_rwlock_unlock(&line_lock[wanted_y]);

    if(matrix_target.type != EMPTY)
      return 0;

    checked = 1;
  }

  //If the target is inside the board
  if(checked == 0){
    pthread_rwlock_rdlock(&line_lock[wanted_y]);
    matrix_target = board.board_matrix[wanted_y][wanted_x];
    pthread_rwlock_unlock(&line_lock[wanted_y]);
  }

  //Target is an empty tile -> swap
  if( matrix_target.type == EMPTY){
    swap_tiles(&matrix_current, &matrix_target);
    valid = 1;
  }

  //I'm a pacman and target is a monster
  else if( matrix_current.type == PACMAN && matrix_target.type == MONSTER){

    //Characters are from the same player -> swap
    if(matrix_current.owner == matrix_target.owner){
        swap_tiles(&matrix_current, &matrix_target);
        valid = 1;
      }

    //Normal Pacman jumping into opposing Monster -> Pacman gets eaten
    else if( matrix_current.super_bites == 0){
        struct tile character = matrix_current;

        matrix_current.type = EMPTY;
        matrix_current.owner = -1;

        increase_score(matrix_target.owner);

        respawn_character(character);

        valid = 1;
      }

    //SuperPacman jumping into opposing Monster -> Monster gets eaten
    else{
      struct tile character = matrix_target;

      matrix_target.type = EMPTY;
      matrix_target.owner = -1;

      matrix_current.super_bites--;

      increase_score(matrix_current.owner);

      swap_tiles(&matrix_current, &matrix_target);

      respawn_character(character);

      valid = 1;
    }
  }

  //I'm a pacman and target is a pacman
  else if( matrix_current.type == PACMAN && matrix_target.type == PACMAN){
    swap_tiles(&matrix_current, &matrix_target);
    valid = 1;
  }

  //I'm a monster and target is a pacman
  else if( matrix_current.type == MONSTER && matrix_target.type == PACMAN){

    //Characters are from the same player -> swap
    if(matrix_current.owner == matrix_target.owner){
      swap_tiles(&matrix_current, &matrix_target);
      valid = 1;
    }

    //Monster jumping into opposing pacman -> Monster eats pacman
    else if(matrix_target.super_bites == 0){

      struct tile character = matrix_target;

      matrix_target.type = EMPTY;
      matrix_target.owner = -1;

      increase_score(matrix_current.owner);

      swap_tiles(&matrix_current, &matrix_target);

      respawn_character(character);

      valid = 1;
    }

    //Monster jumping into Super Pacman -> Monster gets eaten
    else{
      struct tile character = matrix_current;

      matrix_current.type = EMPTY;
      matrix_current.owner = -1;

      matrix_target.super_bites--;

      increase_score(matrix_target.owner);

      respawn_character(character);

      valid = 1;
    }


  }

  //I'm a monster and target is a monster
  else if(matrix_current.type == MONSTER && matrix_target.type == MONSTER){
    swap_tiles(&matrix_current, &matrix_target);
    valid = 1;
  }

  //Target is a fruit
  else if( matrix_target.type == CHERRY || matrix_target.type == LEMON){

    matrix_target.type = EMPTY;

    if(matrix_current.type == PACMAN)
      matrix_current.super_bites = 2;

    increase_score(matrix_current.owner);

    swap_tiles(&matrix_current, &matrix_target);
    valid = 1;

    pthread_t fruit_thread_id;
    pthread_create(&fruit_thread_id, NULL, new_fruit_thread, NULL);

  }

  //Target is a brick
  else if( matrix_target.type == BRICK){

    //Hitting brick from the right
    if(current_x > wanted_x){

      //Is the bounce location inside the board?
      if(current_x + 1 < board.length){
        pthread_rwlock_rdlock(&line_lock[wanted_y]);
        if(board.board_matrix[wanted_y][current_x+1].type == EMPTY){  //Yes, and it's free
          wanted_x = current_x + 1;
          valid = 1;
        }
        else{
          wanted_x = current_x;   //Yes, but it's occupied
          valid = 0;
        }
        pthread_rwlock_unlock(&line_lock[wanted_y]);
      }
      else{
        wanted_x = current_x;
        valid = 0;  //Bounce location outside of matrix
      }
    }

    //Hitting brick from the left
    else if(current_x < wanted_x){

        //Is the bounce location inside the board?
        if(current_x - 1 >= 0){
          pthread_rwlock_rdlock(&line_lock[wanted_y]);
          if(board.board_matrix[wanted_y][current_x-1].type == EMPTY){  //Yes, and it's free
            wanted_x = current_x - 1;
            valid = 1;
          }
          else{
            wanted_x = current_x; //Yes, but it's occupied
            valid = 0;
          }
          pthread_rwlock_unlock(&line_lock[wanted_y]);
        }
        else{
          wanted_x = current_x;
          valid = 0;    //Bounce location outside of matrix
        }
      }

    //Hitting brick from below
    else if(current_y > wanted_y ){

      //Is the bounce location inside the board and is the place free?
      if(current_y + 1 < board.width){
        pthread_rwlock_rdlock(&line_lock[current_y + 1]);
        if(board.board_matrix[current_y + 1][wanted_x].type == EMPTY){  //Yes, and it's free
          wanted_y = current_y + 1;
          valid = 1;
          }
        else{
          wanted_y = current_y;
          valid = 0;  //Yes, but it's occupied
        }
        pthread_rwlock_unlock(&line_lock[current_y + 1]);
      }
      else{
        wanted_y = current_y;
        valid = 0;  //Bounce location outside of matrix
      }
    }

    //Hitting brick from above
    else if(current_y < wanted_y ){

      if(current_y - 1 >= 0){
        pthread_rwlock_rdlock(&line_lock[current_y - 1]);
        if(board.board_matrix[current_y - 1][wanted_x].type == EMPTY){  //Yes, and it's free
          wanted_y = current_y - 1;
          valid = 1;
          }
        else{
          wanted_y = current_y; //Yes, but it's occupied
          valid = 0;
        }
        pthread_rwlock_unlock(&line_lock[current_y - 1]);
      }
      else{
        wanted_y = current_y;
        valid = 0;    //Bounce location outside of matrix
      }
    }

    if(valid){
      pthread_rwlock_rdlock(&line_lock[wanted_y]);
      matrix_target = board.board_matrix[wanted_y][wanted_x];
      pthread_rwlock_unlock(&line_lock[wanted_y]);
      swap_tiles(&matrix_current, &matrix_target);
    }
  }

  //¯\_(ツ)_/¯
  else{
    valid = 0;
    return valid;
  }

  if(valid){
    //This will be sent to the client
    effect->place1 = matrix_current;
    effect->place2 = matrix_target;

    //This is to update the matrix
    pthread_rwlock_wrlock(&line_lock[current_y]);
    board.board_matrix[current_y][current_x] = matrix_current;
    pthread_rwlock_unlock(&line_lock[current_y]);

    pthread_rwlock_wrlock(&line_lock[wanted_y]);
    board.board_matrix[wanted_y][wanted_x] = matrix_target;
    pthread_rwlock_unlock(&line_lock[wanted_y]);
  }

  return valid;
}

////////////////////////////////////////////////////////////////////////////////
void* inactivity_jump_thread(void* arg){
  struct inactivity_arg in_arg = *((struct inactivity_arg*) arg);

  int* active = in_arg.active;
  int client_fd = in_arg.client_fd;
  int character = in_arg.character;

  struct tile current;
  struct move msg;
  msg.type = REGULAR_MESSAGE;

  int free_x, free_y, old_x, old_y;
  int counter = 0;
  int stop;

  while(1){
    sleep(1);

    //Client disconnected
    if(*active == -1)
      return NULL;

    counter++;

    //If the character becomes active resets the counter
    if(*active == 1){
      *active = 0;
      counter = 0;
      continue;
    }


    //If the counter reaches 30 seconds
    if(counter == 30){
      *active = 0;
      counter = 0;

      //Searches for the tile with the character from this player and makes it jump
      stop = 0;
      for(int i=0; i<board.width && stop == 0; i++){

        pthread_rwlock_wrlock(&line_lock[i]);

        for(int j=0; j<board.length && stop == 0; j++){
          if(board.board_matrix[i][j].owner == client_fd && board.board_matrix[i][j].type == character){

            current = board.board_matrix[i][j];

            old_x = j;
            old_y = i;

            board.board_matrix[i][j].type = EMPTY;
            board.board_matrix[i][j].owner = -1;

            stop = 1;
          }
        }
        pthread_rwlock_unlock(&line_lock[i]);
      }

      find_free_tile(&free_x, &free_y);

      current.x = free_x;
      current.y = free_y;

      pthread_rwlock_wrlock(&line_lock[free_y]);
      board.board_matrix[free_y][free_x] = current;
      msg.place1 = board.board_matrix[free_y][free_x];
      pthread_rwlock_unlock(&line_lock[free_y]);

      pthread_rwlock_rdlock(&line_lock[old_y]);
      msg.place2 = board.board_matrix[old_y][old_x];
      pthread_rwlock_unlock(&line_lock[old_y]);

      send_to_all_clients(msg);
      update_server_window(msg);
    }
  }

  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
void* score_thread(void* arg){

  struct move msg;
  msg.type = SCORE_MESSAGE;

  while(1){

    sleep(60);

    if(active_clients == 0)
      continue;

    printf("Sending score\n");

    if(active_clients == 1)
      set_lone_client_score();


    pthread_rwlock_rdlock(&client_vector_mutex);

    for(int i=0; i<total_clients; i++){
      if(client_vector[i].client_fd > 0){

        //Determina o id e o score de cada cliente
        msg.place1.owner = client_vector[i].player_id;
        msg.place2.owner = client_vector[i].score;


        //Isto é para o client saber que vem a primeira mensagem
        if(i == 0)
          msg.place1.type = FIRST;
        else
          msg.place1.type = -FIRST;


        for(int j=0; j<total_clients; j++){   //Aqui não se mete a send_to_all_clients porque essa faria lock outra vez
          //Envia para todos os clientes
          if(client_vector[j].client_fd > 0){
            send(client_vector[j].client_fd, &msg, sizeof(struct move), 0);
          }
        }

      }
    }
    pthread_rwlock_unlock(&client_vector_mutex);

  }
}

////////////////////////////////////////////////////////////////////////////////
void increase_score(int player_id){

  pthread_rwlock_wrlock(&client_vector_mutex);

  for(int i=0; i<total_clients; i++){
    if(client_vector[i].player_id  == player_id){
      client_vector[i].score++;
        pthread_rwlock_unlock(&client_vector_mutex);
      return;
    }
  }

  pthread_rwlock_unlock(&client_vector_mutex);

}

////////////////////////////////////////////////////////////////////////////////
void swap_tiles(struct tile* place1, struct tile* place2){
  struct tile aux;

  aux.type = place2->type;
  aux.tile_color = place2->tile_color;
  aux.owner = place2->owner;
  aux.super_bites = place2->super_bites;

  place2->type = place1->type;
  place2->tile_color = place1->tile_color;
  place2->owner = place1->owner;
  place2->super_bites = place1->super_bites;

  place1->type = aux.type;
  place1->tile_color = aux.tile_color;
  place1->owner = aux.owner;
  place1->super_bites = aux.super_bites;

}

////////////////////////////////////////////////////////////////////////////////
void deliver_or_trim_fruit(){
  //Entra um novo cliente -> Põe frutas
  //Sai um cliente -> Retira frutas

  int current_fruit, fruit_needed;

  struct move message;
  message.type = REGULAR_MESSAGE;

  current_fruit = board.max_fruit;

  pthread_mutex_lock(&fruit_mutex);
  board.max_fruit = ( active_clients - 1 ) * 2;
  pthread_mutex_unlock(&fruit_mutex);

  if(board.max_fruit < 0)
    board.max_fruit = 0;

  fruit_needed = board.max_fruit - current_fruit;


  //Incrementa a fruta porque entra um novo cliente
  if(fruit_needed > 0){

    int free_x, free_y;
    int fruit_type;

    for(int i=0; i<fruit_needed; i++){
      find_free_tile(&free_x, &free_y);

      fruit_type = rand() % 2;


      pthread_rwlock_wrlock(&line_lock[free_y]);
      if(fruit_type == 0)
        board.board_matrix[free_y][free_x].type = CHERRY;

      else
        board.board_matrix[free_y][free_x].type = LEMON;


      message.place1 = board.board_matrix[free_y][free_x];
      pthread_rwlock_unlock(&line_lock[free_y]);


      message.place2 = message.place1;

      send_to_all_clients(message);
      update_server_window(message);

    }
  }

  //Decrementa a fruta porque saiu um cliente
  else{
    int fruit_to_trim = -fruit_needed;

    for(int i=0; i<board.width && fruit_to_trim > 0; i++){
      pthread_rwlock_wrlock(&line_lock[i]);

      for(int j=0; j<board.length && fruit_to_trim > 0; j++){

        if(board.board_matrix[i][j].type == CHERRY || board.board_matrix[i][j].type == LEMON){

          board.board_matrix[i][j].type = EMPTY;
          board.board_matrix[i][j].owner = -1;

          message.place1 = board.board_matrix[i][j];
          message.place2 = message.place1;

          send_to_all_clients(message);
          update_server_window(message);
          fruit_to_trim--;
        }
      }

      pthread_rwlock_unlock(&line_lock[i]);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
void* new_fruit_thread(void* arg){

  int fruit_type;
  int free_x, free_y;
  struct move msg;

  msg.type = REGULAR_MESSAGE;

  find_free_tile(&free_x, &free_y);

  fruit_type = rand() % 2;

  pthread_rwlock_wrlock(&line_lock[free_y]);
  if(fruit_type == 0)
    board.board_matrix[free_y][free_x].type = CHERRY;

  else
    board.board_matrix[free_y][free_x].type = LEMON;

  board.board_matrix[free_y][free_x].x = free_x;
  board.board_matrix[free_y][free_x].y = free_y;

  msg.place1 = board.board_matrix[free_y][free_x];
  pthread_rwlock_unlock(&line_lock[free_y]);

  msg.place2 = msg.place1;

  sleep(2);

  send_to_all_clients(msg);
  update_server_window(msg);

  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
void respawn_character(struct tile character){

  struct move msg;
  msg.type = REGULAR_MESSAGE;

  int free_x, free_y;
  find_free_tile(&free_x, &free_y);

  character.x = free_x;
  character.y = free_y;

  pthread_rwlock_wrlock(&line_lock[free_y]);
  board.board_matrix[free_y][free_x] = character;

  msg.place1 = board.board_matrix[free_y][free_x];
  pthread_rwlock_unlock(&line_lock[free_y]);

  msg.place2 = msg.place1;

  send_to_all_clients(msg);
  update_server_window(msg);
}

////////////////////////////////////////////////////////////////////////////////
void find_free_tile(int* free_x, int* free_y){
  //Talvez gerar um número ao calhas desde 0 até (length*wide)-(bricks+fruits+clients*2) e depois contar até aí

  int i, j;
  int is_free = 0;

  do{
      i = rand() % board.width;
      j = rand() % board.length;

      pthread_rwlock_rdlock(&line_lock[i]);

      if(board.board_matrix[i][j].type == EMPTY)
        is_free = 1;

      pthread_rwlock_unlock(&line_lock[i]);

  }while (is_free != 1);

      *free_x = j;
      *free_y = i;
}

////////////////////////////////////////////////////////////////////////////////
int read_file(struct board_info* board){
  FILE* fp;
  char buffer[100];

  fp = fopen("board.txt", "r");
  if(fp == NULL)
    error_handle("Error opening file\n");

  int length = 0;
  int width = 0;
  int number_of_bricks = 0;

  //Uma primeira passagem com fgets para ler as dimensões do ecrã
  if(fgets(buffer, 100, fp) != NULL)
    if( sscanf(buffer, "%d %d\n", &length, &width) != 2)
      error_handle("Error reading board size from the text file\n");

  //Cria uma matriz com as dimensões da janela
  struct tile** board_matrix = (struct tile**) malloc(sizeof(struct tile) * width);
  for(int i=0; i<width; i++)
    board_matrix[i] = (struct tile*) malloc(sizeof(struct tile) * length);

  //Inicializa as casas da matriz
  for(int i=0; i<width; i++){
    for(int j=0; j<length; j++){
      board_matrix[i][j].x = j;
      board_matrix[i][j].y = i;
      board_matrix[i][j].type = EMPTY;
      board_matrix[i][j].owner = -1;
      board_matrix[i][j].super_bites = 0;
    }
  }

  //Mais fgets para ler o resto do ficheiro e completar a matriz
  for(int i=0; fgets(buffer, 100, fp) != NULL; i++){
      for(int j=0; buffer[j] != '\n' && buffer[j] != '\0'; j++){     //Blindar isto melhor
        if(buffer[j] == 'B'){
          board_matrix[i][j].type = BRICK;
          number_of_bricks++;
        }
      }
  }


  //Assign everything
  board->length = length;
  board->width = width;
  board->board_matrix = board_matrix;
  board->max_fruit = 0;

  fclose(fp);

  return number_of_bricks;

}

////////////////////////////////////////////////////////////////////////////////
void initialize_mutexes(void){

  pthread_mutex_init(&total_clients_mutex, NULL);
  pthread_mutex_init(&fruit_mutex, NULL);
  pthread_rwlock_init(&client_vector_mutex, NULL);

  line_lock = (pthread_rwlock_t*) malloc(sizeof(pthread_rwlock_t)* board.width);

  for(int i=0; i<board.width; i++){
    pthread_rwlock_init(&line_lock[i], NULL);
  }

}

////////////////////////////////////////////////////////////////////////////////
void send_to_all_clients(struct move msg){

  pthread_rwlock_rdlock(&client_vector_mutex);

  for(int i=0; i < total_clients; i++){
    if(client_vector[i].client_fd > 0)
      send(client_vector[i].client_fd, &msg, sizeof(struct move), 0);
    }

  pthread_rwlock_unlock(&client_vector_mutex);

}

////////////////////////////////////////////////////////////////////////////////
void paint_objects(struct board_info board){
  //Percorre a matrix board e pinta todos os objetos
  for(int i=0; i < board.width; i++){
    for(int j=0; j < board.length; j++){

      if(board.board_matrix[i][j].type == BRICK)
    	  paint_brick(j, i);

      else if(board.board_matrix[i][j].type == PACMAN)
     	  paint_pacman(j, i, board.board_matrix[i][j].tile_color.r, board.board_matrix[i][j].tile_color.g, board.board_matrix[i][j].tile_color.b);


      else if(board.board_matrix[i][j].type == SUPER_PACMAN)
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
void set_lone_client_score(void){
  //Põe o score do único client a zero
  pthread_rwlock_wrlock(&client_vector_mutex);
  for(int i=0; i<total_clients; i++){
    if(client_vector[i].client_fd > 0){
      client_vector[i].score = 0;
      return;
    }
  }
  pthread_rwlock_unlock(&client_vector_mutex);

}

////////////////////////////////////////////////////////////////////////////////
void update_server_window(struct move msg){
  struct move* event_data;
  SDL_Event new_event;
  if(msg.type == REGULAR_MESSAGE){
        event_data = (struct move*) malloc(sizeof(struct move));
        *event_data = msg;
        SDL_zero(new_event);
        new_event.type = Event_ServerCommand;
        new_event.user.data1 = event_data;
        SDL_PushEvent(&new_event);
  }
}

////////////////////////////////////////////////////////////////////////////////
void server_data_cleanup(){

  free(client_vector);

  for(int i=0; i<board.width; i++){
    free(board.board_matrix[i]);
  }

  free(board.board_matrix);
  free(line_lock);

}

////////////////////////////////////////////////////////////////////////////////
void error_handle(char error_message[]){
  perror(error_message);
  exit(-1);
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
void print_board(){
  //Imprime o board; só para debugging
  for(int i=0; i<board.width; i++){
    for(int j=0; j<board.length; j++){
      if(board.board_matrix[i][j].type == EMPTY)
        printf("---- | ");

      else if(board.board_matrix[i][j].type == BRICK)
        printf("BBBB | ");

      else if(board.board_matrix[i][j].type == CHERRY)
        printf(" chry| ");

      else if(board.board_matrix[i][j].type == LEMON)
        printf(" lemn| ");

      else if(board.board_matrix[i][j].type == MONSTER)
        printf("M %d | ", board.board_matrix[i][j].owner);

      else if(board.board_matrix[i][j].type == PACMAN)
        printf("P %d | ", board.board_matrix[i][j].owner);

    }
    printf("\n");
  }
}
