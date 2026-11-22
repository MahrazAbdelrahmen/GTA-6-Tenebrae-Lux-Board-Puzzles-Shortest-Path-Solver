/* puzzle.h - rules, BFS solver and puzzle-file parser (no raylib dependency) */
#ifndef PUZZLE_H
#define PUZZLE_H

#include <stdbool.h>

#define GRID          4
#define CELLS         (GRID * GRID)
#define MAX_SOLUTION  64
#define MAX_WARNINGS  16

typedef struct { char c[CELLS]; } Board;           /* 'x' 't' 'v' '-' */
typedef struct { unsigned char from, dir; } Move;  /* dir: 0 up 1 down 2 left 3 right */

typedef struct {
    int    status;        /* 1 solved, 0 unsolvable, -1 search too large */
    int    length;        /* optimal number of moves */
    int    explored;      /* distinct positions discovered */
    int    expanded;      /* positions whose moves were generated */
    int    edges;         /* legal moves generated in total */
    int    maxDepth;      /* deepest BFS layer reached */
    double ms;            /* wall-clock search time */
    Move   moves[MAX_SOLUTION];
} Solution;

extern const int   DR[4], DC[4];
extern const char *DIRNAME[4];

bool     PuzzleInit(void);                 /* allocates solver tables */
void     PuzzleShutdown(void);

int      SlideTarget(const Board *b, int from, int dir);
unsigned Victims(const Board *b, int landing);
bool     DoMove(const Board *b, int from, int dir, Board *slid, Board *out,
                int *landing, unsigned *victims);
bool     BoardIsEmpty(const Board *b);
int      CountPiece(const Board *b, char p);
int      LegalMoveCount(const Board *b);
Solution Solve(const Board *start);

void     BoardToString(const Board *b, char out[CELLS + GRID]);   /* "xxxx/----/t--t/-vv-" */
void     FormatMove(const Board *b, Move m, char *out, int outLen);

/* puzzle files */

typedef struct { char name[48]; Board board; int line; } PuzzleEntry;

typedef struct {
    PuzzleEntry *items;
    int  count, cap;
    char path[260];
    int  warnCount;
    char warn[MAX_WARNINGS][128];
} PuzzleSet;

/* Returns the cell count, or -1 for an invalid symbol. err explains failures. */
int  ParseBoardString(const char *text, Board *out, char *err, int errLen);

int  ParsePuzzleText(const char *text, PuzzleSet *set);   /* returns puzzles added */
int  LoadPuzzleFile(const char *path, PuzzleSet *set);    /* -1 if file can't be read */
bool AppendPuzzleFile(const char *path, const char *name, const Board *b);
bool WriteDefaultPuzzleFile(const char *path);
void PuzzleSetAdd(PuzzleSet *set, const char *name, const Board *b, int line);
void PuzzleSetClear(PuzzleSet *set);

#endif
