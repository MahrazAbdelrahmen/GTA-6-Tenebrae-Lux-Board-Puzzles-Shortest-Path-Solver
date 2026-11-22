/* Slides end at the last empty cell. Captures resolve together on landing. */

#include "puzzle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double NowMs(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
#else
#include <time.h>
static double NowMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
#endif

const int   DR[4] = { -1, 1, 0, 0 };
const int   DC[4] = { 0, 0, -1, 1 };
const char *DIRNAME[4] = { "UP", "DOWN", "LEFT", "RIGHT" };

/* Rules */

int SlideTarget(const Board *b, int from, int dir)
{
    char p = b->c[from];
    if (p != 'x' && p != 't') return -1;
    int r = from / GRID, c = from % GRID;
    for (;;) {
        int nr = r + DR[dir], nc = c + DC[dir];
        if (nr < 0 || nr >= GRID || nc < 0 || nc >= GRID) break;
        if (b->c[nr * GRID + nc] != '-') break;
        r = nr; c = nc;
    }
    int to = r * GRID + c;
    return (to == from) ? -1 : to;
}

unsigned Victims(const Board *b, int landing)
{
    char m = b->c[landing];
    unsigned v = 0;
    int r = landing / GRID, c = landing % GRID;
    for (int d = 0; d < 4; d++) {
        int nr = r + DR[d], nc = c + DC[d];
        if (nr < 0 || nr >= GRID || nc < 0 || nc >= GRID) continue;
        int o = nr * GRID + nc;
        char t = b->c[o];
        if ((m == 'x' && t == 't') || (m == 't' && t == 'x')) { v |= 1u << landing; v |= 1u << o; }
        else if (m == 't' && t == 'v') v |= 1u << o;
    }
    return v;
}

bool DoMove(const Board *b, int from, int dir, Board *slid, Board *out, int *landing, unsigned *victims)
{
    int to = SlideTarget(b, from, dir);
    if (to < 0) return false;
    Board s = *b;
    s.c[to] = s.c[from];
    s.c[from] = '-';
    unsigned v = Victims(&s, to);
    Board o = s;
    for (int i = 0; i < CELLS; i++) if (v & (1u << i)) o.c[i] = '-';
    if (slid) *slid = s;
    if (out) *out = o;
    if (landing) *landing = to;
    if (victims) *victims = v;
    return true;
}

bool BoardIsEmpty(const Board *b)
{
    for (int i = 0; i < CELLS; i++) if (b->c[i] != '-') return false;
    return true;
}

int CountPiece(const Board *b, char p)
{
    int n = 0;
    for (int i = 0; i < CELLS; i++) n += (b->c[i] == p);
    return n;
}

int LegalMoveCount(const Board *b)
{
    int n = 0;
    for (int i = 0; i < CELLS; i++)
        for (int d = 0; d < 4; d++) n += (SlideTarget(b, i, d) >= 0);
    return n;
}

void BoardToString(const Board *b, char out[CELLS + GRID])
{
    int k = 0;
    for (int r = 0; r < GRID; r++) {
        for (int c = 0; c < GRID; c++) out[k++] = b->c[r * GRID + c];
        out[k++] = (r < GRID - 1) ? '/' : '\0';
    }
}

void FormatMove(const Board *b, Move m, char *out, int outLen)
{
    int to = SlideTarget(b, m.from, m.dir);
    snprintf(out, outLen, "%c (%d,%d) %-5s -> (%d,%d)", b->c[m.from],
             m.from / GRID + 1, m.from % GRID + 1, DIRNAME[m.dir],
             to / GRID + 1, to % GRID + 1);
}

/* BFS solver - state packed into 32 bits (2 bits per cell) */

typedef struct { unsigned state; int parent; unsigned char from, dir, depth; } Node;

#define MAX_NODES (1 << 21)
#define HASH_BITS 22
#define HASH_SIZE (1u << HASH_BITS)

static Node *nodes;
static int  *hashTab;            /* node index + 1, 0 = empty slot */

bool PuzzleInit(void)
{
    nodes   = (Node *)malloc(sizeof(Node) * MAX_NODES);
    hashTab = (int *)malloc(sizeof(int) * HASH_SIZE);
    return nodes && hashTab;
}

void PuzzleShutdown(void) { free(nodes); free(hashTab); nodes = NULL; hashTab = NULL; }

static unsigned Pack(const Board *b)
{
    unsigned s = 0;
    for (int i = 0; i < CELLS; i++) {
        unsigned v = b->c[i] == 'x' ? 1 : b->c[i] == 't' ? 2 : b->c[i] == 'v' ? 3 : 0;
        s |= v << (2 * i);
    }
    return s;
}

static Board Unpack(unsigned s)
{
    static const char sym[4] = { '-', 'x', 't', 'v' };
    Board b;
    for (int i = 0; i < CELLS; i++) b.c[i] = sym[(s >> (2 * i)) & 3];
    return b;
}

static int HashFind(unsigned s, bool *found)
{
    unsigned h = (s * 2654435761u) >> (32 - HASH_BITS);
    while (hashTab[h]) {
        if (nodes[hashTab[h] - 1].state == s) { *found = true; return (int)h; }
        h = (h + 1) & (HASH_SIZE - 1);
    }
    *found = false;
    return (int)h;
}

Solution Solve(const Board *start)
{
    Solution sol;
    memset(&sol, 0, sizeof sol);
    double t0 = NowMs();
    memset(hashTab, 0, sizeof(int) * HASH_SIZE);

    int count = 0, head = 0;
    bool f;
    unsigned s0 = Pack(start);
    nodes[count] = (Node){ s0, -1, 0, 0, 0 };
    hashTab[HashFind(s0, &f)] = ++count;
    sol.status = 0;

    while (head < count) {
        int ni = head++;
        if (nodes[ni].depth > sol.maxDepth) sol.maxDepth = nodes[ni].depth;
        if (nodes[ni].state == 0) {                         /* empty board reached */
            int len = nodes[ni].depth;
            if (len > MAX_SOLUTION) { sol.status = -1; break; }
            sol.status = 1;
            sol.length = len;
            for (int k = ni, i = len - 1; i >= 0; i--, k = nodes[k].parent)
                sol.moves[i] = (Move){ nodes[k].from, nodes[k].dir };
            break;
        }
        Board b = Unpack(nodes[ni].state);
        sol.expanded++;
        for (int from = 0; from < CELLS; from++) {
            if (b.c[from] != 'x' && b.c[from] != 't') continue;
            for (int d = 0; d < 4; d++) {
                Board nb;
                if (!DoMove(&b, from, d, NULL, &nb, NULL, NULL)) continue;
                sol.edges++;
                unsigned ns = Pack(&nb);
                int h = HashFind(ns, &f);
                if (f) continue;
                if (count >= MAX_NODES) { sol.status = -1; goto done; }
                nodes[count] = (Node){ ns, ni, (unsigned char)from, (unsigned char)d,
                                       (unsigned char)(nodes[ni].depth + 1) };
                hashTab[h] = ++count;
            }
        }
    }
done:
    sol.explored = count;
    sol.ms = NowMs() - t0;
    return sol;
}

/* Board formats are listed in puzzles.txt. */

static char MapSymbol(char ch)
{
    switch (ch) {
    case 'x': case 'X': case 'b': case 'B':            return 'x';
    case 't': case 'T': case 'p': case 'P': case '^':  return 't';
    case 'v': case 'V': case '=':                      return 'v';
    case '-': case '.': case '_': case '0':            return '-';
    default:                                           return 0;
    }
}

static int IsSep(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '/' || ch == '|' || ch == ',' || ch == '\r' || ch == '\n';
}

int ParseBoardString(const char *text, Board *out, char *err, int errLen)
{
    memset(out->c, '-', CELLS);
    if (err && errLen) err[0] = 0;
    int n = 0, pos = 0;
    for (const char *p = text; *p; p++, pos++) {
        if (IsSep(*p)) continue;
        char m = MapSymbol(*p);
        if (!m) {
            if (err) {
                if (isprint((unsigned char)*p)) snprintf(err, errLen, "unknown symbol '%c' at column %d", *p, pos + 1);
                else snprintf(err, errLen, "unknown symbol at column %d", pos + 1);
            }
            return -1;
        }
        if (n < CELLS) out->c[n] = m;
        n++;
    }
    if (err) {
        if (n > CELLS)      snprintf(err, errLen, "too many cells: %d (a board has %d)", n, CELLS);
        else if (n < CELLS) snprintf(err, errLen, "need %d cells, have %d", CELLS, n);
    }
    return n;
}

static void Warn(PuzzleSet *set, const char *fmt, ...)
{
    if (set->warnCount < MAX_WARNINGS) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(set->warn[set->warnCount], sizeof set->warn[0], fmt, ap);
        va_end(ap);
    }
    set->warnCount++;
}

static void Trim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    int k = 0;
    while (s[k] && isspace((unsigned char)s[k])) k++;
    if (k) memmove(s, s + k, n - k + 1);
}

static void CopyName(char dst[48], const char *src)
{
    snprintf(dst, 48, "%.47s", src);
    Trim(dst);
}

void PuzzleSetAdd(PuzzleSet *set, const char *name, const Board *b, int line)
{
    if (set->count == set->cap) {
        set->cap = set->cap ? set->cap * 2 : 16;
        set->items = (PuzzleEntry *)realloc(set->items, sizeof(PuzzleEntry) * set->cap);
    }
    PuzzleEntry *e = &set->items[set->count++];
    if (name && name[0]) CopyName(e->name, name);
    else snprintf(e->name, sizeof e->name, "Case %d", set->count);
    e->board = *b;
    e->line = line;
}

void PuzzleSetClear(PuzzleSet *set)
{
    free(set->items);
    memset(set, 0, sizeof *set);
}

int ParsePuzzleText(const char *text, PuzzleSet *set)
{
    int added = 0, line = 0, n = 0, startLine = 0;
    bool skipping = false;
    char name[48] = "";
    Board cur;
    memset(cur.c, '-', CELLS);

    const char *p = text;
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        line++;
        char buf[256];
        int len = (int)(e - p);
        if (len > 255) len = 255;
        memcpy(buf, p, len);
        buf[len] = 0;
        p = *e ? e + 1 : e;

        char *cmt = strpbrk(buf, "#;");
        if (cmt) *cmt = 0;
        Trim(buf);

        if (!buf[0]) {                                   /* blank line ends a board */
            if (n > 0) Warn(set, "line %d: board incomplete (%d/%d cells)", startLine, n, CELLS);
            n = 0; skipping = false;
            continue;
        }
        if (buf[0] == '[') {                             /* [Name] header */
            if (n > 0) Warn(set, "line %d: board incomplete (%d/%d cells)", startLine, n, CELLS);
            n = 0; skipping = false;
            char *close = strchr(buf, ']');
            if (!close) { Warn(set, "line %d: missing ']' in header", line); close = buf + strlen(buf); }
            *close = 0;
            CopyName(name, buf + 1);
            continue;
        }
        char *colon = strchr(buf, ':');
        if (colon) {                                     /* Name: board */
            *colon = 0;
            char nm[48], err[96];
            CopyName(nm, buf);
            Board b;
            int k = ParseBoardString(colon + 1, &b, err, sizeof err);
            if (k == CELLS && !err[0]) { PuzzleSetAdd(set, nm, &b, line); added++; }
            else Warn(set, "line %d: '%s': %s", line, nm, err);
            n = 0; name[0] = 0;
            continue;
        }
        if (skipping) continue;

        char row[CELLS];
        int k = 0;
        bool bad = false;
        for (char *q = buf; *q; q++) {
            if (IsSep(*q)) continue;
            char m = MapSymbol(*q);
            if (!m) {
                if (isprint((unsigned char)*q)) Warn(set, "line %d: unknown symbol '%c'", line, *q);
                else Warn(set, "line %d: unknown symbol", line);
                bad = true;
                break;
            }
            if (k < CELLS) row[k] = m;
            k++;
        }
        if (!bad && !(k == GRID || (k == CELLS && n == 0))) {
            Warn(set, "line %d: row has %d cells, expected %d", line, k, GRID);
            bad = true;
        }
        if (bad) { n = 0; skipping = true; name[0] = 0; continue; }

        if (n == 0) startLine = line;
        memcpy(cur.c + n, row, k);
        n += k;
        if (n == CELLS) {
            PuzzleSetAdd(set, name, &cur, startLine);
            added++;
            n = 0; name[0] = 0;
        }
    }
    if (n > 0) Warn(set, "line %d: board incomplete (%d/%d cells)", startLine, n, CELLS);
    return added;
}

int LoadPuzzleFile(const char *path, PuzzleSet *set)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > (1 << 22)) { fclose(f); return -1; }
    char *text = (char *)malloc(size + 1);
    size_t got = fread(text, 1, size, f);
    fclose(f);
    text[got] = 0;
    snprintf(set->path, sizeof set->path, "%s", path);
    int added = ParsePuzzleText(text, set);
    free(text);
    return added;
}

bool AppendPuzzleFile(const char *path, const char *name, const Board *b)
{
    FILE *f = fopen(path, "ab");
    if (!f) return false;
    fprintf(f, "\n[%s]\n", name);
    for (int r = 0; r < GRID; r++) fprintf(f, "%.4s\n", b->c + r * GRID);
    fclose(f);
    return true;
}

static const char *DEFAULT_FILE =
"# Tenebrae Lux puzzles\n"
"#\n"
"# x = block   t = pyramid   v = plate   - = empty\n"
"# Either a [Name] line followed by four rows, or  Name: row/row/row/row\n"
"\n"
"[Opening]\n"
"xxxx\n"
"----\n"
"t--t\n"
"-vv-\n"
"\n"
"# Extra boards, easiest first (all solvable)\n"
"Extra 1: v--v/-xt-/-tx-/v--v\n"
"Extra 2: xvvx/----/----/t--t\n"
"Extra 3: x--x/-t--/--v-/x--t\n"
"Extra 4: -xv-/--x-/-v--/t---\n"
"Extra 5: --vv/---x/--x-/-x-t\n"
"Extra 6: -x-x/---v/vv--/x--t\n"
"Extra 7: t--x/---v/x---/--xv\n"
"Extra 8: t-xv/----/--vx/xv--\n";

bool WriteDefaultPuzzleFile(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs(DEFAULT_FILE, f);
    fclose(f);
    return true;
}
