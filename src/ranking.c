#include "ranking.h"
#include "terminal.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_scores(const void *left, const void *right)
{
    const RankingEntry *a = left;
    const RankingEntry *b = right;
    if (a->score != b->score) return a->score > b->score ? -1 : 1;
    return (a->order > b->order) - (a->order < b->order);
}

bool ranking_load(const char *path, Ranking *ranking)
{
    memset(ranking, 0, sizeof(*ranking));
    FILE *file = fopen(path, "rb");
    if (file == NULL) return errno == ENOENT;
    char line[256];
    size_t capacity = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            int ch;
            while ((ch = fgetc(file)) != '\n' && ch != EOF) { }
            ++ranking->skipped;
            continue;
        }
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = '\0';
        char *end;
        errno = 0;
        long score = strtol(line, &end, 10);
        if (errno != 0 || end == line || *end != '\t' || score < 0 || score > INT_MAX
            || strlen(end + 1) >= RANKING_NAME_BYTES || !utf8_name_valid(end + 1)) {
            ++ranking->skipped;
            continue;
        }
        if (ranking->count == capacity) {
            if (capacity > SIZE_MAX / 2 / sizeof(RankingEntry)) { ok = false; break; }
            size_t next = capacity == 0 ? 32 : capacity * 2;
            RankingEntry *entries = realloc(ranking->entries, next * sizeof(*entries));
            if (entries == NULL) { ok = false; break; }
            ranking->entries = entries;
            capacity = next;
        }
        RankingEntry *entry = &ranking->entries[ranking->count];
        strcpy(entry->name, end + 1);
        entry->score = (int)score;
        entry->order = ranking->count++;
    }
    if (ferror(file)) ok = false;
    if (fclose(file) != 0) ok = false;
    if (!ok) { ranking_free(ranking); return false; }
    if (ranking->count > 1)
        qsort(ranking->entries, ranking->count, sizeof(RankingEntry), compare_scores);
    return true;
}

bool ranking_append(const char *path, const char *name, int score)
{
    if (score < 0 || strlen(name) >= RANKING_NAME_BYTES || !utf8_name_valid(name))
        return false;
    FILE *file = fopen(path, "ab+");
    if (file == NULL) return false;
    bool ok = true;
    /* Keep a manually edited last line from swallowing a new result. */
    if (fseek(file, 0, SEEK_END) != 0) ok = false;
    long length = ok ? ftell(file) : -1;
    if (length < 0) ok = false;
    if (ok && length > 0) {
        if (fseek(file, -1, SEEK_END) != 0) ok = false;
        int last = ok ? fgetc(file) : EOF;
        if (last == EOF || fseek(file, 0, SEEK_END) != 0) ok = false;
        if (ok && last != '\n' && fputc('\n', file) == EOF) ok = false;
    }
    if (ok && fprintf(file, "%d\t%s\n", score, name) < 0) ok = false;
    if (fclose(file) != 0) ok = false;
    return ok;
}

void ranking_free(Ranking *ranking)
{
    free(ranking->entries);
    memset(ranking, 0, sizeof(*ranking));
}
