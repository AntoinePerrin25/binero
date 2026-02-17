#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>

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
    {
        printf("Could not open %s : %s\n", path, strerror(errno));
        exit(0);
    }

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
    printf("   ");
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

size_t AdjacentPairRule(Game* game)
{
    // TwoEqualsThree: 00_ -> 001, _00 -> 100  (c==c1 → fill c2)
    // FillTheHole:    0_0 -> 010, 1_1 -> 101   (c==c2 → fill c1)
    char somethingChangedHere = 0;
    for (size_t i = 0; i < game->size; i++)
    {
        for (size_t j = 0; j < game->size; j++)
        {
            Cell* cell = GetCellPtr(game, i, j);
            if (cell->value == ' ' || cell->value == 0) continue;

            Cell* cells[5][2] = {
                {cell, NULL},
                {GetCellPtr(game, i+1, j), GetCellPtr(game, i+2, j)},
                {GetCellPtr(game, i, j+1), GetCellPtr(game, i, j+2)},
                {GetCellPtr(game, i-1, j), GetCellPtr(game, i-2, j)},
                {GetCellPtr(game, i, j-1), GetCellPtr(game, i, j-2)}
            };
            char c = cell->value;
            for (size_t k = 1; k <= 4; k++) {
                Cell* c1 = cells[k][0];
                Cell* c2 = cells[k][1];
                if (!c1 || !c2) continue;
                char opposite = c == '0' ? '1' : '0';
                /* 00_ / _00 */
                if (c == c1->value && !c2->isImmutable && (c2->value == ' ' || c2->value == 0))
                {
                    c2->value = opposite;
                    somethingChangedHere = 1;
                }
                /* 0_0 / 1_1 */
                if (c == c2->value && !c1->isImmutable && (c1->value == ' ' || c1->value == 0))
                {
                    c1->value = opposite;
                    somethingChangedHere = 1;
                }
            }
        }
    }
    return somethingChangedHere;
}

size_t QuotaExhausted(Game* game)
{
    size_t somethingChangedHere = 0;

    // dir 0 = lines, dir 1 = columns
    for (size_t dir = 0; dir < 2; dir++)
    {
        for (size_t i = 0; i < game->size; i++)
        {
            size_t n0 = 0, n1 = 0;
            char* addresses[7] = {0};
            size_t add_idx = 0;
            for (size_t j = 0; j < game->size; j++)
            {
                size_t idx = dir == 0 ? i * game->size + j : j * game->size + i;
                if(game->array[idx].value == '0') n0++;
                if(game->array[idx].value == '1') n1++;
                if(game->array[idx].value == ' ' || game->array[idx].value == 0)
                {
                    if (add_idx != game->size/2)
                    {
                        addresses[add_idx++] = &game->array[idx].value;
                        continue;
                    }
                }
            }
            if(n0 == game->size/2 && n1 != game->size/2)
            {
                for(size_t k = 0; k < add_idx; k++)
                {
                    *addresses[k] = '1';
                    somethingChangedHere = 1;
                }
            }
            if(n1 == game->size/2 && n0 != game->size/2)
            {
                for(size_t k = 0; k < add_idx; k++)
                {
                    *addresses[k] = '0';
                    somethingChangedHere = 1;
                }
            }
        }
    }

    return somethingChangedHere;
}

size_t next(Game* game)
{
    (void) game;
    return 0;
}

typedef size_t (*Rule)(Game*);

void EvidentSolve(Game* game)
{
    Rule rules[] = { AdjacentPairRule, QuotaExhausted, NULL };

    size_t somethingChanged;
    clock_t start = clock();
    do {
        somethingChanged = 0;
        for (size_t i = 0; rules[i] != NULL; ++i) {
            somethingChanged |= rules[i](game);
        }
    } while (somethingChanged);
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC * 1000000; // Convert to microseconds
    printf("Solved in %.0f micro seconds\n", time_spent);
}

size_t checkWin(Game* game)
{
    // Check if all cells are filled
    for (size_t i = 0; i < game->size * game->size; i++)
    {
        if (game->array[i].value == ' ' || game->array[i].value == 0)
            return 0;
    }
    
    // Check rows
    for (size_t i = 0; i < game->size; i++)
    {
        size_t count0 = 0, count1 = 0;
        
        // Count 0s and 1s in row i
        for (size_t j = 0; j < game->size; j++)
        {
            Cell* cell = GetCellPtr(game, i, j);
            if (cell->value == '0') count0++;
            else if (cell->value == '1') count1++;
        }
        
        // Each row must have equal 0s and 1s
        if (count0 != game->size / 2 || count1 != game->size / 2)
            return 0;
        
        // Check for three consecutive identical values in row
        for (size_t j = 0; j < game->size - 2; j++)
        {
            Cell* c1 = GetCellPtr(game, i, j);
            Cell* c2 = GetCellPtr(game, i, j + 1);
            Cell* c3 = GetCellPtr(game, i, j + 2);
            if (c1->value == c2->value && c2->value == c3->value)
                return 0;
        }
    }
    
    // Check columns
    for (size_t j = 0; j < game->size; j++)
    {
        size_t count0 = 0, count1 = 0;
        
        // Count 0s and 1s in column j
        for (size_t i = 0; i < game->size; i++)
        {
            Cell* cell = GetCellPtr(game, i, j);
            if (cell->value == '0') count0++;
            else if (cell->value == '1') count1++;
        }
        
        // Each column must have equal 0s and 1s
        if (count0 != game->size / 2 || count1 != game->size / 2)
            return 0;
        
        // Check for three consecutive identical values in column
        for (size_t i = 0; i < game->size - 2; i++)
        {
            Cell* c1 = GetCellPtr(game, i, j);
            Cell* c2 = GetCellPtr(game, i + 1, j);
            Cell* c3 = GetCellPtr(game, i + 2, j);
            if (c1->value == c2->value && c2->value == c3->value)
                return 0;
        }
    }
    
    // Check all rows are unique
    for (size_t i1 = 0; i1 < game->size; i1++)
    {
        for (size_t i2 = i1 + 1; i2 < game->size; i2++)
        {
            size_t identical = 1;
            for (size_t j = 0; j < game->size; j++)
            {
                Cell* cell1 = GetCellPtr(game, i1, j);
                Cell* cell2 = GetCellPtr(game, i2, j);
                if (cell1->value != cell2->value)
                {
                    identical = 0;
                    break;
                }
            }
            if (identical)
                return 0;
        }
    }
    
    // Check all columns are unique
    for (size_t j1 = 0; j1 < game->size; j1++)
    {
        for (size_t j2 = j1 + 1; j2 < game->size; j2++)
        {
            size_t identical = 1;
            for (size_t i = 0; i < game->size; i++)
            {
                Cell* cell1 = GetCellPtr(game, i, j1);
                Cell* cell2 = GetCellPtr(game, i, j2);
                if (cell1->value != cell2->value)
                {
                    identical = 0;
                    break;
                }
            }
            if (identical)
                return 0;
        }
    }
    
    // All checks passed - game is won!
    return 1;
}

int main(void)
{
    Game game = LoadLevel("lvl1.binero"); // [CB]: InitGame(14);|LoadLevel("lvl1.binero");

    enableRawMode();
    
    while (1) {
        /* move cursor home, clear screen and scrollback to avoid stacking output */
        char clearScreen[] = "\x1b[H\x1b[2J"; //[CB]: "\x1b[H\x1b[2J\x1b[3J";|"\x1b[H\x1b[2J";
        printf("%s", clearScreen);
        PrintGame(&game);
        printf("Flèches: nav | 'a'->'0' | 'e'->'1' | 'r': clear | 'c': commit | 'q': quitter\n");
        
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;
        
        if (c == 'q') break;
        else if (c == 'a') setCellValue(&game, '0');
        else if (c == 'e') setCellValue(&game, '1');
        else if (c == 'r') setCellValue(&game, ' ');
        else if (c == 'c') commitValues(&game);
        // AdjacentPairRule, QuotaExhausted
        else if (c == 38) AdjacentPairRule(&game); // &
        else if (c == 34) QuotaExhausted(&game); // "
        else if (c == 's') EvidentSolve(&game); 
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
        // else {
        //     printf("Touche non reconnue: %d\n", c);
        // }
    }
    
    FreeGame(&game);

    return 0;
}
