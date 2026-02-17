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

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

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

/* Debug: solution reference for solver validation */
static char *g_solution = NULL;
static size_t g_solution_size = 0;
size_t PrintAndDebug = 0; //[CB]: 0;|1;

void LoadSolution(const char *path, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("No solution file: %s\n", path); return; }
    g_solution_size = size;
    g_solution = malloc(size * size);
    for (size_t i = 0; i < size; i++) {
        for (size_t j = 0; j < size; j++) {
            char byte;
            do { byte = fgetc(f); } while (byte == '\n');
            if (byte == EOF) { fclose(f); free(g_solution); g_solution = NULL; return; }
            g_solution[i * size + j] = byte;
        }
    }
    fclose(f);
}

void FreeSolution(void)
{
    free(g_solution);
    g_solution = NULL;
}

/* Check a cell write against the solution. Call from solver rules. */
static inline void debugCheckCell(Game *game, size_t idx, char newVal, const char *ruleName)
{
    if (!PrintAndDebug) return;
    if (!g_solution) return;
    char expected = g_solution[idx];
    if (newVal != expected) {
        size_t row = idx / game->size;
        size_t col = idx % game->size;
        printf("\x1b[31m[BUG] %s: cell (%zu,%zu) set to '%c' but solution expects '%c'\x1b[0m\n",
               ruleName, row, col, newVal, expected);
    }
}

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

/*
Cell* GetCellPtr(Game *game, size_t i, size_t j)
{
    if(LIKELY(i<game->size && j<game->size))
        return &game->array[i * game->size + j];
    return 0;
}
*/

#define GetCellPtr(game, i, j) \
    ((i) < (game)->size && (j) < (game)->size ? &((game)->array[(i) * (game)->size + (j)]) : NULL)

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
            if (UNLIKELY(cell->value == ' ' || cell->value == 0)) continue;

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
                if (UNLIKELY(c == c1->value && !c2->isImmutable && (c2->value == ' ' || c2->value == 0)))
                {
                    debugCheckCell(game, (size_t)(c2 - game->array), opposite, "AdjacentPair(00_)");
                    c2->value = opposite;
                    somethingChangedHere = 1;
                }
                /* 0_0 / 1_1 */
                if (UNLIKELY(c == c2->value && !c1->isImmutable && (c1->value == ' ' || c1->value == 0)))
                {
                    debugCheckCell(game, (size_t)(c1 - game->array), opposite, "AdjacentPair(0_0)");
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
            size_t* indices = alloca(game->size/2 * sizeof(size_t));
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
                        indices[add_idx++] = idx;
                        continue;
                    }
                }
            }
            if(UNLIKELY(n0 == game->size/2 && n1 != game->size/2))
            {
                for(size_t k = 0; k < add_idx; k++)
                {
                    debugCheckCell(game, indices[k], '1', "QuotaExhausted(fill1)");
                    game->array[indices[k]].value = '1';
                    somethingChangedHere = 1;
                }
            }
            if(UNLIKELY(n1 == game->size/2 && n0 != game->size/2))
            {
                for(size_t k = 0; k < add_idx; k++)
                {
                    debugCheckCell(game, indices[k], '0', "QuotaExhausted(fill0)");
                    game->array[indices[k]].value = '0';
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
    } while (LIKELY(somethingChanged));
    clock_t end = clock();
    double time_spent = ((double)(end - start)) / CLOCKS_PER_SEC * 1000000; // Convert to microseconds
    if(PrintAndDebug)
        printf("Solved in %.0f micro seconds\n", time_spent);
}

Game CloneGame(const Game *src)
{
    Game g = { .size = src->size, .array = malloc(src->size * src->size * sizeof(Cell)), .selected = 0 };
    if (!g.array) { perror("malloc"); exit(EXIT_FAILURE); }
    memcpy(g.array, src->array, src->size * src->size * sizeof(Cell));
    return g;
}


#define da_append(xs, x)                                                             \
    do {                                                                             \
        if ((xs)->count >= (xs)->capacity) {                                         \
            if ((xs)->capacity == 0) (xs)->capacity = 256;                           \
            else (xs)->capacity *= 2;                                                \
            (xs)->items = realloc((xs)->items, (xs)->capacity*sizeof(*(xs)->items)); \
        }                                                                            \
                                                                                     \
        (xs)->items[(xs)->count++] = (x);                                            \
    } while (0)

#define da_free(da) free((da)->items)
#define nob_da_foreach(Type, it, da) for (Type *it = (da)->items; it < (da)->items + (da)->count; ++it)

typedef struct {
    int *items;
    size_t count;
    size_t capacity;
} Indexes;


#define NOT_FINISHED 0
#define WIN 1
#define IMPOSSIBLE 2


size_t checkWin(Game* game)
{
    // Check if all cells are filled
    for (size_t i = 0; i < game->size * game->size; i++)
    {
        if (game->array[i].value == ' ' || game->array[i].value == 0)
        {
            if(PrintAndDebug)
                printf("Cell %zu is empty\n", i);
            return NOT_FINISHED;
        }
    }

    for (size_t dir = 0; dir < 2; dir++)
    {
        for (size_t i = 0; i < game->size; i++)
        {
            size_t count0 = 0, count1 = 0;

            for (size_t j = 0; j < game->size; j++)
            {
                size_t idx = dir == 0 ? i * game->size + j : j * game->size + i;
                if (game->array[idx].value == '0') count0++;
                else if (game->array[idx].value == '1') count1++;

                // Check for three consecutive identical values
                if (j >= 2)
                {
                    size_t idx1 = dir == 0 ? i * game->size + (j-2) : (j-2) * game->size + i;
                    size_t idx2 = dir == 0 ? i * game->size + (j-1) : (j-1) * game->size + i;
                    if (game->array[idx1].value == game->array[idx2].value &&
                        game->array[idx2].value == game->array[idx].value)
                    {
                        if(PrintAndDebug)
                            printf("%s %zu has three consecutive '%c'\n",
                                dir == 0 ? "Row" : "Column", i, game->array[idx].value);
                        return IMPOSSIBLE;
                    }
                }
            }

            if (count0 != game->size / 2 || count1 != game->size / 2)
            {
                if(PrintAndDebug)
                    printf("%s %zu does not have equal 0s and 1s\n", dir == 0 ? "Row" : "Column", i);
                return IMPOSSIBLE;
            }
            else if (count0 + count1 != game->size)
            {
                if(PrintAndDebug)
                    printf("%s %zu has empty cells\n", dir == 0 ? "Row" : "Column", i);
                return NOT_FINISHED;
            }

            // Check uniqueness against all subsequent rows/columns
            for (size_t i2 = i + 1; i2 < game->size; i2++)
            {
                size_t identical = 1;
                for (size_t j = 0; j < game->size; j++)
                {
                    size_t idx1 = dir == 0 ? i * game->size + j : j * game->size + i;
                    size_t idx2 = dir == 0 ? i2 * game->size + j : j * game->size + i2;
                    if (game->array[idx1].value != game->array[idx2].value)
                    {
                        identical = 0;
                        break;
                    }
                }
                if (identical)
                {
                    if(PrintAndDebug)
                        printf("%s %zu and %zu are identical\n", dir == 0 ? "Rows" : "Columns", i, i2);
                    return IMPOSSIBLE;
                }
            }
        }
    }
    return WIN;
}


void Solve(Game* game)
{
    // Make a copy to work on
    Game myGame = CloneGame(game);
    EvidentSolve(&myGame);

    // Get Every empty cell idx and make a DA of it
    Indexes emptyCells = {0};
    for (size_t i = 0; i < myGame.size * myGame.size; i++) {
        if (myGame.array[i].value == ' ' || myGame.array[i].value == 0) {
            da_append(&emptyCells, (int)i);
        }
    }


    // If EvidentSolve already solved it, copy back and return
    if (emptyCells.count == 0) {
        if (checkWin(&myGame) == WIN) {
            memcpy(game->array, myGame.array, myGame.size * myGame.size * sizeof(Cell));
        }
        goto cleanup;
    }

    // Try '0' and '1' on the first empty cell and recurse
    else
    {
        size_t idx = (size_t)emptyCells.items[0];
        for (char val = '0'; val <= '1'; val++) {
            Game tryGame = CloneGame(&myGame);
            tryGame.array[idx].value = val;
            Solve(&tryGame);
            if (checkWin(&tryGame) == WIN) {
                memcpy(game->array, tryGame.array, tryGame.size * tryGame.size * sizeof(Cell));
                FreeGame(&tryGame);
                goto cleanup;
            }
            FreeGame(&tryGame);
        }
    }
cleanup:
    free(emptyCells.items);
    FreeGame(&myGame);
}


#include <dirent.h>

void ExportLevel(Game *game)
{
    /* Temporarily disable raw mode to read filename */
    disableRawMode();
    printf("\nNom du niveau (sans extension): ");
    fflush(stdout);

    char name[128] = {0};
    if (fgets(name, sizeof(name), stdin) == NULL || name[0] == '\n') {
        printf("Export annulé.\n");
        enableRawMode();
        return;
    }
    /* Strip trailing newline */
    name[strcspn(name, "\n")] = '\0';

    char path[256];
    snprintf(path, sizeof(path), "levels/%s.binero", name);

    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("Erreur: %s\n", strerror(errno));
        enableRawMode();
        return;
    }
    for (size_t i = 0; i < game->size; i++) {
        for (size_t j = 0; j < game->size; j++) {
            char v = game->array[i * game->size + j].value;
            fputc((v == '0' || v == '1') ? v : ' ', f);
        }
        if (i < game->size - 1) fputc('\n', f);
    }
    fclose(f);
    printf("Exporté vers %s\n", path);
    enableRawMode();
}

Game SelectLevel(void)
{
    /* Scan levels/ directory for .binero files */
    const char *levelsDir = "levels";
    char paths[64][256];
    size_t count = 0;

    DIR *dir = opendir(levelsDir);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL && count < 64) {
            size_t len = strlen(ent->d_name);
            if (len > 7 && strcmp(ent->d_name + len - 7, ".binero") == 0
                && strcmp(ent->d_name + len - 11, ".binero.sol") != 0) {
                snprintf(paths[count], sizeof(paths[count]), "%s/%s", levelsDir, ent->d_name);
                count++;
            }
        }
        closedir(dir);
    }

    /* Sort alphabetically */
    for (size_t i = 0; i < count; i++)
        for (size_t j = i + 1; j < count; j++)
            if (strcmp(paths[i], paths[j]) > 0) {
                char tmp[256];
                memcpy(tmp, paths[i], 256);
                memcpy(paths[i], paths[j], 256);
                memcpy(paths[j], tmp, 256);
            }

    printf("\x1b[H\x1b[2J");
    printf("=== BINERO ===\n\n");
    for (size_t i = 0; i < count; i++)
        printf("  %zu) %s\n", i + 1, paths[i]);
    printf("  0) Grille vide 14x14\n");
    printf("\nChoix: ");
    fflush(stdout);

    /* Read choice (raw mode is not enabled yet) */
    char buf[16] = {0};
    if (fgets(buf, sizeof(buf), stdin) == NULL) buf[0] = '0';
    int choice = atoi(buf);

    if (choice >= 1 && choice <= (int)count) {
        Game game = LoadLevel(paths[choice - 1]);

        /* Try to load matching .sol file */
        char solPath[270];
        snprintf(solPath, sizeof(solPath), "%s.sol", paths[choice - 1]);
        LoadSolution(solPath, game.size);

        return game;
    }

    return InitGame(14);
}

int main(void)
{
    Game game = SelectLevel();

    enableRawMode();

    
    while (1) {
        /* move cursor home, clear screen and scrollback to avoid stacking output */
        char clearScreen[] = "\x1b[H\x1b[2J\n\n"; //[CB]: "\x1b[H\x1b[2J\x1b[3J\n\n";|"\x1b[H\x1b[2J\n\n";
        printf("%s", clearScreen);
        PrintGame(&game);
        printf("Flèches: nav|'a'/'e'->'0'/'1'|'r'emove | 'c'ommit | 'x'port | 'q'uit\n");
        char win = 0;
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;
        
        if (c == 'q') break;
        else if (c == 'a') setCellValue(&game, '0');
        else if (c == 'e') setCellValue(&game, '1');
        else if (c == 'r') setCellValue(&game, ' ');
        else if (c == 'c') commitValues(&game);
        else if (c == '&') AdjacentPairRule(&game); // &
        else if (c == -87) QuotaExhausted(&game); // é
        else if (c == 's') EvidentSolve(&game);
        else if (c == 'S') Solve(&game);
        else if (c == 'x') ExportLevel(&game);
        else if (c == 'w') win = checkWin(&game);
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
        else {
            printf("Touche non reconnue: %d\n", c);
        }
        if (win == WIN) {
            printf("Congratulations! You've won the game!\n");
            printf("Press any key to exit...\n");
            read(STDIN_FILENO, &c, 1);
            break;
        }
    }
    
    FreeSolution();
    FreeGame(&game);

    return 0;
}
