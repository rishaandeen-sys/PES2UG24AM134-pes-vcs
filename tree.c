#include "pes.h"
#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

static int entry_cmp(const void *a, const void *b) {
    const TreeEntry *ea = a, *eb = b;
    return strcmp(ea->name, eb->name);
}

int tree_serialize(const Tree *tree, void **out, size_t *len_out) {
    TreeEntry sorted[MAX_TREE_ENTRIES];
    memcpy(sorted, tree->entries, tree->count * sizeof(TreeEntry));
    qsort(sorted, tree->count, sizeof(TreeEntry), entry_cmp);

    size_t total = 0;
    for (int i = 0; i < tree->count; i++) {
        char mode_str[16];
        snprintf(mode_str, sizeof(mode_str), "%o", sorted[i].mode);
        total += strlen(mode_str) + 1 + strlen(sorted[i].name) + 1 + HASH_SIZE;
    }

    uint8_t *buf = malloc(total);
    if (!buf) return -1;
    uint8_t *p = buf;

    for (int i = 0; i < tree->count; i++) {
        char mode_str[16];
        snprintf(mode_str, sizeof(mode_str), "%o", sorted[i].mode);
        size_t mlen = strlen(mode_str);
        size_t nlen = strlen(sorted[i].name);
        memcpy(p, mode_str, mlen); p += mlen;
        *p++ = ' ';
        memcpy(p, sorted[i].name, nlen); p += nlen;
        *p++ = '\0';
        memcpy(p, sorted[i].hash.hash, HASH_SIZE); p += HASH_SIZE;
    }

    *out = buf;
    *len_out = total;
    return 0;
}

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *p = data, *end = (const uint8_t *)data + len;

    while (p < end) {
        const uint8_t *space = memchr(p, ' ', end - p);
        if (!space) break;
        const uint8_t *null = memchr(space + 1, '\0', end - (space + 1));
        if (!null) break;
        if (null + 1 + HASH_SIZE > end) break;
        if (tree_out->count >= MAX_TREE_ENTRIES) break;

        TreeEntry *e = &tree_out->entries[tree_out->count++];
        char mode_str[16];
        size_t mlen = space - p;
        if (mlen >= sizeof(mode_str)) mlen = sizeof(mode_str) - 1;
        memcpy(mode_str, p, mlen);
        mode_str[mlen] = '\0';
        e->mode = (uint32_t)strtol(mode_str, NULL, 8);

        size_t nlen = null - (space + 1);
        if (nlen >= sizeof(e->name)) nlen = sizeof(e->name) - 1;
        memcpy(e->name, space + 1, nlen);
        e->name[nlen] = '\0';

        memcpy(e->hash.hash, null + 1, HASH_SIZE);
        p = null + 1 + HASH_SIZE;
    }
    return 0;
}

int tree_from_index(ObjectID *root_out) {
    Index index;
    index.count = 0;

    FILE *f = fopen(INDEX_FILE, "r");
    if (f) {
        char line[700];
        while (fgets(line, sizeof(line), f)) {
            if (index.count >= MAX_INDEX_ENTRIES) break;
            IndexEntry *e = &index.entries[index.count];
            char hash_hex[HASH_HEX_SIZE + 1];
            if (sscanf(line, "%o %64s %llu %u %511s",
                       &e->mode, hash_hex,
                       (unsigned long long*)&e->mtime_sec,
                       &e->size, e->path) == 5) {
                hex_to_hash(hash_hex, &e->hash);
                index.count++;
            }
        }
        fclose(f);
    }

    Tree root;
    root.count = 0;
    for (int i = 0; i < index.count; i++) {
        if (root.count >= MAX_TREE_ENTRIES) break;
        TreeEntry *e = &root.entries[root.count++];
        e->mode = index.entries[i].mode;
        strncpy(e->name, index.entries[i].path, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
        e->hash = index.entries[i].hash;
    }

    void *data;
    size_t len;
    if (tree_serialize(&root, &data, &len) != 0) return -1;
    int ret = object_write(OBJ_TREE, data, len, root_out);
    free(data);
    return ret;
}
