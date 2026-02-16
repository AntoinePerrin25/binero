#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>

#define RED   "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BG_WHITE "\x1b[47m"
#define RESET "\x1b[0m"

typedef struct __attribute__((packed)) Cell_s {
    char value;
    size_t isImmutable:1;    
    size_t isCommited:1;
    size_t reserved:6; /* padding to 1 byte */    
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
            size_t idx = i * game.size + j;
            if (byte == '0' || byte == '1') {
                game.array[idx].isImmutable = 1;
            }
            else if (byte == ' ') {
                game.array[idx].isImmutable = 0;
            }
            game.array[idx].value = byte;
        }
    }
fclose(file);

    return game;
}

Cell* GetCellPtr(Game *game, size_t i, size_t j)
{
    if(i<game->size && j<game->size)
        return &game->array[i * game->size + j];
    return 0;
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
                cell->isCommited ? YELLOW : (cell->isImmutable ? "" : RED),
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
    if (cell->isImmutable || cell->isCommited) return; /* ignore if not mutable or already committed */
    cell->value = value;
}

void commitValues(Game* game)
{
    for (size_t i = 0; i < game->size * game->size; i++) {
        if (!game->array[i].isImmutable && game->array[i].value != ' ' && game->array[i].value != 0) {
            game->array[i].isCommited = 1;
        }
    }
}

size_t Rule1(Game* game)
{
    char somethingChangedHere = 0;
    for (size_t i = 0; i < game->size; i++)
    {
        for (size_t j = 0; j < game->size; j++)
        {
            Cell* cell = GetCellPtr(game, i, j);
            if (cell->isImmutable) continue; /* skip immutable cells */
            if (cell->value == ' ' || cell->value == 0) continue; /* skip empty cells */

            char pairs[4][2][2] = {
                {{1, 0}, {2, 0}},
                {{-1, 0}, {-2, 0}},
                {{0, 1}, {0, 2}},
                {{0, -1}, {0, -2}}
            };

            Cell* cells[5][2] = {
                {cell, NULL},
                {GetCellPtr(game, i+1, j), GetCellPtr(game, i+2, j)},
                {GetCellPtr(game, i, j+1), GetCellPtr(game, i, j+2)},
                {GetCellPtr(game, i-1, j), GetCellPtr(game, i-2, j)},
                {GetCellPtr(game, i, j-1), GetCellPtr(game, i, j-2)}
            };
            char c = cells[0][0]->value;
            if (c != ' ' && c != 0)
            {
                for (size_t k = 0; k < 4; k++) {
                    Cell* c1 = cells[k][0];
                    Cell* c2 = cells[k][1];
                    if (c2->value != ' ' && c2->value != 0)
                        continue;
                    if (c == c1->value)
                    {
                        c2->value = c == '0' ? '1' : '0';
                        somethingChangedHere = 1;
                    }
                }
            }
        }
    }
    return 0;
}

size_t Rule2(Game* game)
{
    printf("Rule 2\n");
    (void*) game;
    return 0;
}

typedef size_t (*Rule)(Game*);

void solve(Game* game)
{
    Rule rules[] = { Rule1, Rule2, NULL };

    size_t somethingChanged;
    do {
        somethingChanged = 0;
        for (size_t i = 0; rules[i] != NULL; ++i) {
            somethingChanged |= rules[i](game);
        }
    } while (somethingChanged);
}

int main(void)
{
    Game game = LoadLevel("lvl1.binero"); // [CB]: InitGame(14);|LoadLevel("lvl1.binero");

    enableRawMode();
    
    while (1) {
        printf("\x1b[2J\x1b[H"); /* clear screen */
        PrintGame(&game);
        printf("\nFlèches: nav | 'a'->'0' | 'e'->'1' | 'r': clear | 'c': commit | 'q': quitter\n");
        
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;
        
        if (c == 'q') break;
        else if (c == 'a') setCellValue(&game, '0');
        else if (c == 'e') setCellValue(&game, '1');
        else if (c == 'c') commitValues(&game); 
        else if (c == 'r') setCellValue(&game, ' ');
        else if (c == 's') solve(&game); 
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