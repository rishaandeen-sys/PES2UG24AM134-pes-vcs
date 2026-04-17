#include "pes.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

extern int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
extern void hash_to_hex(const ObjectID *id, char *hex_out);
extern int hex_to_hash(const char *hex, ObjectID *id_out);

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;
    char line[700];
    while (fgets(line, sizeof(line), f)) {
        if (index->count >= MAX_INDEX_ENTRIES) break;
        IndexEntry *e = &index->entries[index->count];
        char hash_hex[HASH_HEX_SIZE + 1];
        unsigned int mode;
        unsigned long long mtime;
        unsigned int sz;
        char path[512];
        if (sscanf(line, "%o %64s %llu %u %511s",
                   &mode, hash_hex, &mtime, &sz, path) == 5) {
            e->mode = mode;
            e->mtime_sec = mtime;
            e->size = sz;
            strncpy(e->path, path, sizeof(e->path)-1);
            hex_to_hash(hash_hex, &e->hash);
            index->count++;
        }
    }
    fclose(f);
    return 0;
}

static int path_cmp(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path, ((IndexEntry*)b)->path);
}

int index_save(const Index *index) {
    Index sorted = *index;
    qsort(sorted.entries, sorted.count, sizeof(IndexEntry), path_cmp);
    char tmppath[256];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", INDEX_FILE);
    FILE *f = fopen(tmppath, "w");
    if (!f) return -1;
    for (int i = 0; i < sorted.count; i++) {
        IndexEntry *e = &sorted.entries[i];
        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&e->hash, hash_hex);
        fprintf(f, "%o %s %llu %u %s\n",
                e->mode, hash_hex,
                (unsigned long long)e->mtime_sec,
                e->size, e->path);
    }
    fclose(f);
    rename(tmppath, INDEX_FILE);
    return 0;
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "error: cannot stat %s\n", path);
        return -1;
    }
    size_t size = st.st_size;
    uint8_t *data = malloc(size + 1);
    if (!data) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) { free(data); return -1; }
    size_t nread = fread(data, 1, size, f);
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, data, nread, &id) != 0) {
        free(data);
        return -1;
    }
    free(data);

    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
    }
    e->mode = 100644;
    e->hash = id;
    e->mtime_sec = st.st_mtime;
    e->size = (uint32_t)size;
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';
    index_save(index);
    return 0;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            memmove(&index->entries[i], &index->entries[i+1],
                    (index->count - i - 1) * sizeof(IndexEntry));
            index->count--;
            return 0;
        }
    }
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    if (index->count == 0)
        printf("  (nothing to show)\n");
    else
        for (int i = 0; i < index->count; i++)
            printf("  staged:     %s\n", index->entries[i].path);
    printf("\nUnstaged changes:\n");
    printf("  (nothing to show)\n");
    printf("\nUntracked files:\n");
    printf("  (nothing to show)\n");
    return 0;
}
