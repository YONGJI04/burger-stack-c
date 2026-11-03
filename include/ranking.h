#ifndef BURGER_RANKING_H
#define BURGER_RANKING_H

#include <stdbool.h>
#include <stddef.h>

#define RANKING_NAME_BYTES 64
#define RANKING_TOP_COUNT 10

typedef struct {
    char name[RANKING_NAME_BYTES];
    int score;
    size_t order;
} RankingEntry;

typedef struct {
    RankingEntry *entries;
    size_t count;
    size_t skipped;
} Ranking;

/* Missing files are an empty ranking. Other I/O failures are reported. */
bool ranking_load(const char *path, Ranking *ranking);
bool ranking_append(const char *path, const char *name, int score);
void ranking_free(Ranking *ranking);

#endif
