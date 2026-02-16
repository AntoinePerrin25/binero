#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>

#define RED   "\x1b[31m"
#define GREEN "\x1b[32m"
#define BG_WHITE "\x1b[47m"
#define RESET "\x1b[0m"

typedef struct __attribute__((packed)) Cell_s {
    char value;
    char isMutable;
} Cell;

typedef struct __attribute__((packed)) Game_s {
    size_t size;
    Cell* array;
    size_t selected;
} Game;

Game InitGame(size_t size)
{
    Game game = {
        .size = size,
        .array = calloc(size * size, sizeof(Cell)),
        .selected = 0}; 
    if (!game.array) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    return game;
}

void FreeGame(Game *game)
{
    if (!game) return;
    free(game->array);
    game->array = NULL;
    game->size = 0;
}

Game LoadLevel(const char *path)
{
    Game game = InitGame(14);
    FILE* file = fopen(path, "rb");
    if (!file)
        printf("Could not open %s : %s", path, strerror(errno));

    for(size_t i = 0; i < game.size; i++){
        for(size_t j = 0; j < game.size; j++){
            char byte;
            do {
                byte = fgetc(file);
            } while (byte == '\n');
            
            if (byte == EOF) {
                printf("Erreur: fichier trop court\n");
                fclose(file);
                return game;
            }
            if (byte == '0' || byte == '1') {
                game.array[i * game.size + j].isMutable = 0;
            }
            else if (byte == ' ') {
                game.array[i * game.size + j].isMutable = 1;
            }
            game.array[i * game.size + j].value = byte;
        }
    }
fclose(file);

    return game;
}

Cell* GetCellPtr(Game *game, size_t i, size_t j)
{
    return &game->array[i * game->size + j];
}

void PrintGame(Game* game)
{
    /* header */
    printf("\n\n    ");
    for (size_t j = 0; j < game->size; j++) {
        printf(" %c", 'a' + (char)j);
    }
    printf("\n");

    for (size_t i = 0; i < game->size; i++) {
        printf("%2zu:|", i + 1);
        for (size_t j = 0; j < game->size; j++) {
            Cell* cell = GetCellPtr(game, i, j);
            size_t isSelected = (i * game->size + j) == game->selected;
            char ch = cell->value ? cell->value : ' ';

            printf("%s%s%c%s|", 
                isSelected ? BG_WHITE : "",
                cell->isMutable ? RED : "",
                ch,
                RESET);
        }
        printf("\n");
    }
    fflush(stdout);
}

/* Mode terminal raw (capture touches sans Enter) */
static struct termios orig_termios;

void disableRawMode(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void enableRawMode(void)
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    //raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }
}

void moveSelection(Game* game, int dx, int dy)
{
    int row = (int)(game->selected / game->size);
    int col = (int)(game->selected % game->size);
    
    if (dx < 0 && col > 0) col--;
    else if (dx > 0 && col < (int)game->size - 1) col++;
    if (dy < 0 && row > 0) row--;
    else if (dy > 0 && row < (int)game->size - 1) row++;
    
    game->selected = (size_t)(row * (int)game->size + col);
}

void setCellValue(Game* game, char value)
{
    Cell* cell = &game->array[game->selected];
    if (!cell->isMutable) return; /* ignore if not mutable */
    cell->value = value;
}



int main(void)
{
    Game game = LoadLevel("lvl1.binero"); // [CB]: InitGame(14);|LoadLevel("lvl1.binero");

    enableRawMode();
    
    while (1) {
        printf("\x1b[2J\x1b[H"); /* clear screen */
        /* Debug status */
        size_t sel = game.selected;
        printf("Selected: idx=%zu row=%zu col=%zu\n", sel, sel / game.size, sel % game.size);
        PrintGame(&game);
        printf("\nFlèches: nav | 'a'->'0' | 'b'->'1' | 'c': clear | 'q': quitter\n");
        
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;
        
        if (c == 'q') break;
        else if (c == 'c') setCellValue(&game, 0);
        else if (c == 'a') setCellValue(&game, '0');
        else if (c == 'b') setCellValue(&game, '1');
        else if (c == '\x1b') { /* escape sequence */
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
            
            if (seq[0] == '[') {
                if (seq[1] == 'A') moveSelection(&game, 0, -1); /* up */
                else if (seq[1] == 'B') moveSelection(&game, 0, 1); /* down */
                else if (seq[1] == 'C') moveSelection(&game, 1, 0); /* right */
                else if (seq[1] == 'D') moveSelection(&game, -1, 0); /* left */
            }
        }
    }
    
    FreeGame(&game);

    return 0;
}