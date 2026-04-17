#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── TODO: Implement these ───────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str = (type == OBJ_BLOB) ? "blob" :
                           (type == OBJ_TREE) ? "tree" : "commit";

    // Build header: "blob 16\0"
    char header[64];
    int hlen = snprintf(header, sizeof(header), "%s %zu", type_str, len) + 1;

    // Combine header + data
    size_t total = hlen + len;
    uint8_t *full = malloc(total);
    if (!full) return -1;
    memcpy(full, header, hlen);
    memcpy(full + hlen, data, len);

    // Hash the full object
    compute_hash(full, total, id_out);

    // Deduplication check
    if (object_exists(id_out)) {
        free(full);
        return 0;
    }

    // Build shard directory path and create it
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    char dirpath[512];
    snprintf(dirpath, sizeof(dirpath), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(dirpath, 0755);

    // Build final and temp paths
    char path[512], tmppath[512];
    object_path(id_out, path, sizeof(path));
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);

    // Write to temp file, fsync, rename atomically
    int fd = open(tmppath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { free(full); return -1; }
    write(fd, full, total);
    fsync(fd);
    close(fd);
    rename(tmppath, path);

    // fsync the directory
    int dfd = open(dirpath, O_RDONLY);
    if (dfd >= 0) { fsync(dfd); close(dfd); }

    free(full);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    rewind(f);

    uint8_t *buf = malloc(fsize);
    if (!buf) { fclose(f); return -1; }
    fread(buf, 1, fsize, f);
    fclose(f);

    // Integrity check
    ObjectID computed;
    compute_hash(buf, fsize, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buf);
        return -1;
    }

    // Find the \0 separating header from data
    uint8_t *null_pos = memchr(buf, '\0', fsize);
    if (!null_pos) { free(buf); return -1; }

    // Parse type
    if (strncmp((char*)buf, "blob", 4) == 0) *type_out = OBJ_BLOB;
    else if (strncmp((char*)buf, "tree", 4) == 0) *type_out = OBJ_TREE;
    else *type_out = OBJ_COMMIT;

    // Extract data portion
    size_t header_len = null_pos - buf + 1;
    *len_out = fsize - header_len;
    *data_out = malloc(*len_out);
    if (!*data_out) { free(buf); return -1; }
    memcpy(*data_out, null_pos + 1, *len_out);

    free(buf);
    return 0;
}
