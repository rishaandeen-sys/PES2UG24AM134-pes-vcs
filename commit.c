#include "pes.h"
#include "commit.h"
#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
extern int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);
extern void hash_to_hex(const ObjectID *id, char *hex_out);
extern int hex_to_hash(const char *hex, ObjectID *id_out);
extern int tree_from_index(ObjectID *root_out);

int head_read(ObjectID *id_out) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\n")] = '\0';
    if (strncmp(line, "ref: ", 5) == 0) {
        char refpath[256];
        snprintf(refpath, sizeof(refpath), ".pes/%s", line + 5);
        FILE *rf = fopen(refpath, "r");
        if (!rf) return -1;
        char hash[HASH_HEX_SIZE + 1];
        if (!fgets(hash, sizeof(hash), rf)) { fclose(rf); return -1; }
        fclose(rf);
        hash[strcspn(hash, "\n")] = '\0';
        return hex_to_hash(hash, id_out);
    }
    return hex_to_hash(line, id_out);
}

int head_update(const ObjectID *id) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\n")] = '\0';
    char refpath[256];
    if (strncmp(line, "ref: ", 5) == 0)
        snprintf(refpath, sizeof(refpath), ".pes/%s", line + 5);
    else
        snprintf(refpath, sizeof(refpath), "%s", HEAD_FILE);
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    char tmppath[300];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", refpath);
    FILE *rf = fopen(tmppath, "w");
    if (!rf) return -1;
    fprintf(rf, "%s\n", hex);
    fclose(rf);
    rename(tmppath, refpath);
    return 0;
}

int commit_create(const char *message, ObjectID *id_out) {
    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) return -1;

    char tree_hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&tree_id, tree_hex);

    ObjectID parent_id;
    int has_parent = (head_read(&parent_id) == 0);
    char parent_hex[HASH_HEX_SIZE + 1];
    if (has_parent) hash_to_hex(&parent_id, parent_hex);

    time_t now = time(NULL);
    const char *author = pes_author();

    char buf[4096];
    int len;
    if (has_parent)
        len = snprintf(buf, sizeof(buf),
            "tree %s\nparent %s\nauthor %s %llu\ncommitter %s %llu\n\n%s\n",
            tree_hex, parent_hex,
            author, (unsigned long long)now,
            author, (unsigned long long)now,
            message);
    else
        len = snprintf(buf, sizeof(buf),
            "tree %s\nauthor %s %llu\ncommitter %s %llu\n\n%s\n",
            tree_hex,
            author, (unsigned long long)now,
            author, (unsigned long long)now,
            message);

    if (object_write(OBJ_COMMIT, buf, len, id_out) != 0) return -1;
    return head_update(id_out);
}

int commit_parse(const void *data, size_t len, Commit *commit_out) {
    const char *p = data;
    const char *end = p + len;
    memset(commit_out, 0, sizeof(*commit_out));
    while (p < end) {
        const char *nl = memchr(p, '\n', end - p);
        if (!nl) break;
        if (nl == p) { p++; break; }
        char line[512];
        size_t llen = nl - p;
        if (llen >= sizeof(line)) llen = sizeof(line) - 1;
        memcpy(line, p, llen);
        line[llen] = '\0';
        if (strncmp(line, "tree ", 5) == 0)
            hex_to_hash(line + 5, &commit_out->tree);
        else if (strncmp(line, "parent ", 7) == 0) {
            hex_to_hash(line + 7, &commit_out->parent);
            commit_out->has_parent = 1;
        } else if (strncmp(line, "author ", 7) == 0) {
            strncpy(commit_out->author, line + 7, sizeof(commit_out->author) - 1);
            char *last_space = strrchr(commit_out->author, ' ');
            if (last_space) {
                commit_out->timestamp = (uint64_t)strtoull(last_space + 1, NULL, 10);
                *last_space = '\0';
            }
        }
        p = nl + 1;
    }
    if (p < end) {
        size_t msglen = end - p;
        if (msglen >= sizeof(commit_out->message)) msglen = sizeof(commit_out->message) - 1;
        memcpy(commit_out->message, p, msglen);
        commit_out->message[msglen] = '\0';
        char *nl = strchr(commit_out->message, '\n');
        if (nl) *nl = '\0';
    }
    return 0;
}

int commit_walk(void (*callback)(const ObjectID*, const Commit*, void*), void *ctx) {
    ObjectID id;
    if (head_read(&id) != 0) return -1;
    while (1) {
        ObjectType type;
        void *data;
        size_t len;
        if (object_read(&id, &type, &data, &len) != 0) break;
        Commit commit;
        commit_parse(data, len, &commit);
        free(data);
        callback(&id, &commit, ctx);
        if (!commit.has_parent) break;
        id = commit.parent;
    }
    return 0;
}
