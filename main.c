#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>
#include <pthread.h>

#if !defined(__GNU_LIBRARY__) || defined(_SEM_SEMUN_UNDEFINED)
union semun {
  int val;             // value for SETVAL
  struct semid_ds *buf; // buffer for IPC_STAT, IPC_SET
  unsigned short *array; // array for GETALL, SETALL
  struct seminfo *__buf; // buffer for IPC_INFO
};
#endif

#define pln printf("\n");
#define BOARD_SIZE 8
#define EMPTY 0
#define PLAYER_ONE 1
#define PLAYER_TWO 2

struct SharedMemory {
  int shared_board[BOARD_SIZE][BOARD_SIZE];
  int halt_flag;
  int id_sem;
};

enum
{
  SEM_USER_1 = 0, // Indicates it’s the first person’s turn.
  SEM_USER_2 = 0  // Indicates it’s the second person’s turn.
};

// Declarations for constants...
const int NUMBER_OF_PLAYERS = 2;
const int dx[] = {1, 1, 1, 0, 0, -1, -1, -1};
const int dy[] = {-1, 0, 1, 1, -1, -1, 0, 1};

// Declaration for player's cells counter
players_cells_number[3] = {0, 4, 4};
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Declarations for wrapper functions...
int AllocateSharedMemory();
struct SharedMemory* MapSharedMemory(int id);
void DeleteSharedMemory(int **addr, int n);
int CreateSemaphoreSet(int n);
void DeleteSemaphoreSet(int id);
void print_board(int shared_board[BOARD_SIZE][BOARD_SIZE]);
int exist_empty_space(int shared_board[BOARD_SIZE][BOARD_SIZE]);
int is_valid(int x, int y);
void assign_player(int row, int col, int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id);
int is_adjacent(int row, int col, int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id);
int is_a_valid_move(int row, int col, int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id);
int player_turn_move(int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id, char* move_buf);
void WaitSemaphore(int id, int sem_num);
void SignalSemaphore(int id, int sem_num, int op);
void check_halt_and_empty_space(struct SharedMemory* shared_memory);
void player_annoucement(int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id);
int random_play(int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id) ;
void handle_error(const char *message);
void player_movement(int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id);
void player(struct SharedMemory* shared_memory, int player_id);


int AllocateSharedMemory() {
  return shmget(IPC_PRIVATE, sizeof(struct SharedMemory), IPC_CREAT | SHM_R | SHM_W);
}

struct SharedMemory* MapSharedMemory(int id) {
  struct SharedMemory* addr;
  addr = (struct SharedMemory*)shmat(id, NULL, 0);

  // Initialize halt_flag
  addr->halt_flag = 0;

  // Initialize the matrix
  for (int i = 0; i < BOARD_SIZE; i++)
    for (int j = 0; j < BOARD_SIZE; j++)
      addr->shared_board[i][j] = 0;

  for (int i = 0; i < BOARD_SIZE / 2; i++)
    addr->shared_board[i][i] = 1;

  for (int i = BOARD_SIZE / 2; i < BOARD_SIZE; i++)
    addr->shared_board[i][i] = 2;

  return addr;
}

int CreateSemaphoreSet(int n) {
  union semun arg;
  int id;
  short vals[2] = {1, 0};

  assert(n > 0);          /* You need at least one! */

  id = semget(IPC_PRIVATE, n, SHM_R | SHM_W);
  arg.array = vals;
  semctl(id, 0, SETALL, arg);
  return id;
}

void DeleteSemaphoreSet(int id) {
  if (semctl(id, 0, IPC_RMID, NULL) == -1) {
    perror("Error releasing semaphore!");
    exit(EXIT_FAILURE);
  }
}

int exist_empty_space(int shared_board[BOARD_SIZE][BOARD_SIZE])
{
  for (int i = 0; i < BOARD_SIZE; i++)
    for (int j = 0; j < BOARD_SIZE; j++)
      if (shared_board[i][j] == 0)
        return 1;

  return 0;
}

void print_board(int shared_board[BOARD_SIZE][BOARD_SIZE]) {
  printf("   ");
  for (size_t i = 1; i <= BOARD_SIZE; i++) {
    printf("%-2zu ", i);
  }
  pln;

  for (size_t i = 0; i < BOARD_SIZE; i++) {
    printf("%-2zu ", i + 1);
    for (size_t j = 0; j < BOARD_SIZE; j++) {
      printf("%d  ", shared_board[i][j]);
    }
    pln;
  }
}

int is_valid(int x, int y) { return x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE; }

void assign_player(int row, int col, int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id) {
  shared_board[row][col] = player_id;
  players_cells_number[player_id]+=1;
}

int is_adjacent(int row, int col, int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id) {
  for (int k = 0; k < 8; k++) {
    int x = row + dx[k];
    int y = col + dy[k];
    if (is_valid(x, y) && shared_board[x][y] == player_id)
      return 1;
  }
  return 0;
}

int is_a_valid_move(int row, int col, int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id) {
  if(is_valid(row, col)) {
    if(shared_board[row][col] == EMPTY) {
      if(is_adjacent(row, col, shared_board, player_id)) {
        return 1;
      } else {
        puts("Move it's not player's adjacent cell\n");
        return 0;
      }
    }
    else {
      puts("Move already assigned by a player\n");
    }
  }
  else {
    puts("Move out of board\n");
  }

  return 0;
}

int player_turn_move(int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id, char* move_buf) {
  int row = move_buf[0] - '0';
  int col = move_buf[2] - '0';

  if (is_a_valid_move((row-1), (col-1), shared_board, player_id)) {
    assign_player((row-1), (col-1), shared_board, player_id);
    return 1;
  } else {
    puts("Try Again\n");
    return 0;
  }
}

// Function to wait for the semaphore value
void WaitSemaphore(int id, int sem_num) {
  struct sembuf sb;
  sb.sem_num = sem_num;
  sb.sem_op = -1;
  sb.sem_flg = 0;
  semop(id, &sb, 1);
}

// Function to signal (release) the semaphore
void SignalSemaphore(int id, int sem_num, int op) {
  struct sembuf sb;
  sb.sem_num = sem_num;
  sb.sem_op = op;
  sb.sem_flg = 0;
  semop(id, &sb, 1);
}

// Function to halt both processes
void check_halt_and_empty_space(struct SharedMemory* shared_memory) {
  if (shared_memory->halt_flag) {
    exit(0);
  }

  if (!exist_empty_space(shared_memory->shared_board)) {
    shared_memory->halt_flag = 1;
    exit(0);
  }
}

void player_annoucement(int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id) {
  printf("Player %d's turn\n", player_id);
  puts("Choose your move:\n");
  puts("1 - Write row and col, respectively:\n");
  puts("2 - Random assignment:\n");
  puts("Current board:\n");
  print_board(shared_board);
}

int random_play(int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id) 
{
  int i, j;
  for (;;) {
    i = rand() % BOARD_SIZE;
    j = rand() % BOARD_SIZE;

    if (shared_board[i][j] == player_id) {
      break;
    }
  }
  
  for (int k = 0; k < 8; k++) {
    int value = rand() % 8;
  
    int x = i + dx[value];
    int y = j + dy[value];
  
    if (is_valid(x, y) && shared_board[x][y] == EMPTY) {
      assign_player(x, y, shared_board, player_id);
      return 1;
    }
  }

  return 0;
}

void handle_error(const char *message) {
  perror(message);
  exit(EXIT_FAILURE);
}

void player_movement(int shared_board[BOARD_SIZE][BOARD_SIZE], int player_id) {
  char move_buf[BUFSIZ];
  char choice[BUFSIZ];

  player_annoucement(shared_board, player_id);
  if(!fgets(choice, BUFSIZ, stdin)) {
    handle_error("Error reading from stdin");
  }

  choice[strcspn(choice, "\n")] = '\0';

  if((strcmp(choice, "1")) == 0) {
    puts("Write row and col, respectively\n");
    puts("For example I want row equal one and col equal two, so I write '1 2'\n");
    if(!fgets(move_buf, BUFSIZ, stdin)) {
      handle_error("Error reading from stdin");
    }
    while(!player_turn_move(shared_board, player_id, move_buf)) {
      if(!fgets(move_buf, BUFSIZ, stdin)) {
        handle_error("Error reading from stdin");
      }
    }
  } else if(strcmp(choice, "2") == 0) {
    if(!random_play(shared_board, player_id)) {
      puts("You don't have a valid move\n");
    }
  }

  printf("Player move was assigned to: %s\n", move_buf);
  print_board(shared_board);
}

void player(struct SharedMemory* shared_memory, int player_id) {
  while (1) {
    if (player_id == 1) {
      WaitSemaphore(shared_memory->id_sem, 0);
      pthread_mutex_lock(&mutex);

      player_movement(shared_memory->shared_board, player_id);
    
      pthread_mutex_unlock(&mutex);
      SignalSemaphore(shared_memory->id_sem, 1, 1);
      check_halt_and_empty_space(shared_memory);
    } else {
      WaitSemaphore(shared_memory->id_sem, 1);
      pthread_mutex_lock(&mutex);

      player_movement(shared_memory->shared_board, player_id);

      pthread_mutex_unlock(&mutex);
      SignalSemaphore(shared_memory->id_sem, 0, 1);
      check_halt_and_empty_space(shared_memory);
    }
  }
  exit(0);
}

int main(int argc, char *argv[]) {  
  int id_sh_mem = AllocateSharedMemory();
  if (id_sh_mem == -1) {
    perror("Error in shmget");
    exit(EXIT_FAILURE);
  }

  struct SharedMemory* shared_memory = MapSharedMemory(id_sh_mem);

  // Initialize semaphore
  shared_memory->id_sem = CreateSemaphoreSet(2);
  if (shared_memory->id_sem == -1) {
    perror("Error in shmget or sem_init");
    exit(EXIT_FAILURE);
  }
  
  // Create players processes
  pid_t firstPlayerPid = fork();

  if (firstPlayerPid == 0) {
    // process (firstPlayer)
    player(shared_memory, PLAYER_ONE);
    exit(0);
  } else {
    pid_t secondPlayerPid = fork();

    if (secondPlayerPid == 0) {
      // process (secondPlayer)
      player(shared_memory, PLAYER_TWO);
      exit(0);
    } else {
      // Parent process (main) waits for both processes to finish
      waitpid(firstPlayerPid, NULL, 0);
      waitpid(secondPlayerPid, NULL, 0);
    }
  }

  if(players_cells_number[PLAYER_ONE] > players_cells_number[PLAYER_TWO]) {
    printf("PLAYER ONE IS THE WINNER\n");
  } else if(players_cells_number[PLAYER_TWO] > players_cells_number[PLAYER_ONE]) {
    printf("PLAYER TWO IS THE WINNER\n");
  } else {
    printf("Tie between the players\n");
  }

  // Clean up resources
  DeleteSemaphoreSet(shared_memory->id_sem);
  shmctl(id_sh_mem, IPC_RMID, NULL);
  shmdt(shared_memory);
  pthread_mutex_destroy(&mutex);

  return 0;
}
