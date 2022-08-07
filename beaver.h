#ifndef BEAVER_H
#define BEAVER_H

#ifdef _WIN32
#define BEAVER_ALWAYS_SYNC
#endif

#ifdef _WIN64
#define BEAVER_ALWAYS_SYNC
#endif

#ifndef BEAVER_ALWAYS_SYNC

#ifndef BEAVER_ASYNC
#define BEAVER_ASYNC 0
#endif // BEAVER_ASYNC

#ifndef ASAW_LOCATATION
#define ASAW_LOCATATION "asaw.c"
#endif

#endif // BEAVER_ALWAYS_SYNC

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if BEAVER_ASYNC == 1

#include "asaw.h"
#include <pthread.h>

#endif

// TODO: windows
#include <unistd.h>

#define GREEN "\033[32m"
#define RED "\033[31m"
#define ORANGE "\033[93m"
#define RESET "\033[39m"
#define BLUE "\033[96m"

#ifndef COMPILER

#if defined(__clang__)
#define COMPILER "clang"

#elif defined(__GNUG__)
#define COMPILER "g++"

#elif defined(__GNUC__)
#define COMPILER "gcc"

#elif defined(__MSVC_VER__)
#define COMPILER "msvc"

#elif defined(__TINYC__)
#define COMPILER "tcc"

#elif defined(__MINGW32__)
#define COMPILER "mingw"

#elif defined(__MINGW64__)
#define COMPILER "mingw"
#endif

#endif // COMPILER

#ifndef BEAVER_EXTRA_FLAGS_BUFFER_SIZE
#define BEAVER_EXTRA_FLAGS_BUFFER_SIZE 16
#endif

#ifndef BEAVER_DIRECTORY
#define BEAVER_DIRECTORY "build/"
#endif

typedef struct module_t module_t;
struct module_t {
    char name[32];
    char module[32];
    char src[256];
    char extra_flags[256];
    char special_flags[256];
};

extern module_t modules[];
extern uint32_t modules_len;

// simple set -----------------------------------------------------------------

typedef struct bv_set_t_ bv_set_t_;
struct bv_set_t_ {
    char** set;
    uint32_t size;
    uint32_t used;
};

bv_set_t_* bv_set_create_(uint32_t size)
{
    bv_set_t_* s = malloc(sizeof(*s));
    s->size = size << 2;
    s->set = calloc(s->size, sizeof(*s->set));
    s->used = 0;
    return s;
}

void bv_set_free_(bv_set_t_* s)
{
    if (s == NULL) {
        return;
    }
    free(s->set);
    free(s);
}

uint32_t bv_set_pos_(bv_set_t_* s, char* k)
{
    uint32_t h = 8223;
    {
        char* iter = k;
        for (; *iter; ++iter) {
            h ^= *iter;
        }
    }
    h %= s->size;
    while (s->set[h] != NULL && strcmp(s->set[h], k) != 0) {
        h = (h + 1) % s->size;
    }
    return h;
}

int bv_set_insert_(bv_set_t_* s, char* k)
{
    uint32_t p = bv_set_pos_(s, k);
    s->used += s->set[p] == NULL;
    if (s->used >= s->size) {
        return -1;
    }
    s->set[p] = k;
    return 0;
}

bool bv_set_contains_(bv_set_t_* s, char* k)
{
    uint32_t p = bv_set_pos_(s, k);
    return s->set[p] != NULL;
}

// beaver helper --------------------------------------------------------------

static inline bool bv_should_recomp_(char* file, char* dep)
{
    if (access(file, F_OK) != 0) {
        return 1;
    }

    struct stat file_stat;
    struct stat dep_stat;

    stat(file, &file_stat);
    stat(dep, &dep_stat);

    if (dep_stat.st_mtime >= file_stat.st_mtime) {
        return 1;
    }
    return 0;
}

static inline int bv_bcmd_(
    char** cmd, uint32_t* len, uint32_t* size, char* s, bool space)
{
    if (*cmd == NULL) {
        *size = 1024;
        *len = 0;
        *cmd = malloc(*size);
        if (*cmd == NULL) {
            return -1;
        }
        **cmd = 0;
    }
    uint32_t sl = strlen(s);
    if (*len + sl + space >= *size) {
        *size = (*size << 1) + sl;
        *cmd = realloc(*cmd, *size);
        if (*cmd == NULL) {
            return -1;
        }
    }
    if (space) {
        (*cmd)[*len] = ' ';
        *len += 1;
    }
    memcpy(*cmd + *len, s, sl);
    *len += sl;
    (*cmd)[*len] = 0;
    return 0;
}

static inline int call(char* cmd)
{
    printf(GREEN "[running]" RESET " %s\n", cmd);
    return system(cmd);
}

static inline void call_or_panic(char* cmd)
{
    int r = call(cmd);
    if (r != 0) {
        fprintf(stderr, RED "BEAVER PANIC!" RESET ": '%s'\n", cmd);
        exit(1);
    }
}

static inline void call_or_warn(char* cmd)
{
    int r = call(cmd);
    if (r != 0) {
        fprintf(stderr, ORANGE "BEAVER WARN!" RESET ": '%s'\n", cmd);
    }
}

static inline void bv_check_build_dir_()
{
    if (access(BEAVER_DIRECTORY, F_OK) == 0) {
        return;
    }

    call_or_warn("mkdir -p " BEAVER_DIRECTORY);
}

#define recompile() bv_recompile_beaver_(NULL)

static inline void bv_recompile_beaver_(char** argv)
{
    char* cmd = NULL;
    uint32_t len = 0;
    uint32_t size = 0;

#ifdef BEAVER_ALWAYS_SYNC
    if (argv == NULL) {
        bv_bcmd_(&cmd, &len, &size, COMPILER " -o beaver beaver.c", 0);
    } else {
        bv_bcmd_(&cmd, &len, &size, COMPILER " -o beaver beaver.c &&", 0);
        for (; *argv; argv++) {
            bv_bcmd_(&cmd, &len, &size, *argv, 1);
        }
    }
#else  // beaver async
    if (argv == NULL) {
        bv_bcmd_(&cmd, &len, &size,
            COMPILER " -DBEAVER_ASYNC=1 -o beaver beaver.c " ASAW_LOCATION
                     "-lpthread",
            0);
    } else {
        bv_bcmd_(&cmd, &len, &size,
            COMPILER " -DBEAVER_ASYNC=1 -o beaver beaver.c " ASAW_LOCATION
                     " -lpthread &&",
            0);
        for (; *argv; argv++) {
            bv_bcmd_(&cmd, &len, &size, *argv, 1);
        }
    }
#endif // BEAVER_ALWAYS_SYNC

    call_or_panic(cmd);
    free(cmd);
    exit(0);
}

static inline void auto_update(char** argv)
{
    if (!bv_should_recomp_("beaver", "beaver.c")) {
        return;
    }
    bv_recompile_beaver_(argv);
}

static inline void rm(char* p)
{
    char* cmd = NULL;
    uint32_t len = 0;
    uint32_t size = 0;

    bv_bcmd_(&cmd, &len, &size, "rm", 0);
    bv_bcmd_(&cmd, &len, &size, p, 1);
    call_or_warn(cmd);
    free(cmd);
}

static inline char* bv_file_from_path_(char* p)
{
    char* src = rindex(p, '/');
    if (src == NULL) {
        return p;
    } else {
        return ++src;
    }
}

static bv_set_t_* bv_eflags_ = NULL;
static bv_set_t_* bv_files_ = NULL;
static bv_set_t_* bv_modules_ = NULL;

static void bv_eflags_add_(char* flags)
{
    char* s = flags;
    char* start = NULL;

    while (*s) {
        while (isspace(*s)) {
            s++;
        }
        start = s;
        while (*s && !isspace(*s)) {
            s++;
        }
        if (start != s) {
            *s = 0;
            bv_set_insert_(bv_eflags_, start);
            s++;
        }
    }
}

void* bv_async_call_(void* cmd)
{
    printf(GREEN "[running" BLUE ":async" GREEN "]" RESET " %s\n", (char*)cmd);
    int r = system(cmd);
    if (r != 0) {
        fprintf(stderr, ORANGE "CBUILD WARN!: " RESET "%s\n", (char*)cmd);
    }
    free(cmd);
    return NULL;
}

static inline void bv_compile_module_(char* name, char* flags)
{
    // module already compiled
    if (bv_set_contains_(bv_modules_, name)) {
        return;
    }
    bv_set_insert_(bv_modules_, name);

    char* cmd = NULL;
    uint32_t len = 0;
    uint32_t size = 0;

    module_t* mi = NULL;
    for (mi = modules; mi != modules + modules_len; mi++) {
        if (strcmp(mi->name, name) != 0) {
            continue;
        }

        if (*mi->module != 0) {
            bv_compile_module_(mi->module, flags);
            continue;
        }

        if (bv_set_contains_(bv_files_, mi->src)) {
            continue;
        }

        bv_set_insert_(bv_files_, mi->src);

        bool should_recomp = 0;

        //  TODO: windows
        // check if directory exists
        {
            bv_bcmd_(&cmd, &len, &size, BEAVER_DIRECTORY, 0);
            bv_bcmd_(&cmd, &len, &size, mi->src, 0);

            // beaver directory gurantees one /
            char* d = rindex(cmd, '/');
            *d = 0;

            if (access(cmd, F_OK) != 0) {
                *cmd = 0;
                len = 0;
                bv_bcmd_(&cmd, &len, &size, "mkdir -p " BEAVER_DIRECTORY, 0);
                bv_bcmd_(&cmd, &len, &size, mi->src, 0);
                d = rindex(cmd, '/'); // same as above
                *d = 0;
                call_or_warn(cmd);
                should_recomp = 1;
            }
            *cmd = 0;
            len = 0;
        }

        // check if file was altered
        if (!should_recomp) {
            bv_bcmd_(&cmd, &len, &size, BEAVER_DIRECTORY, 0);
            bv_bcmd_(&cmd, &len, &size, mi->src, 0);
            bv_bcmd_(&cmd, &len, &size, ".o", 0);
            struct stat t = { 0 };
            struct stat s = { 0 };

            int err_0 = stat(cmd, &t);
            int err_1 = stat(mi->src, &s);
            if (err_0 != 0 || err_1 != 0) {
                should_recomp = 1;
            } else if (s.st_mtime >= t.st_mtime) {
                should_recomp = 1;
            }
            *cmd = 0;
            len = 0;
        }

        if (should_recomp) {
            bv_bcmd_(&cmd, &len, &size, COMPILER " -c -o " BEAVER_DIRECTORY, 0);
            bv_bcmd_(&cmd, &len, &size, mi->src, 0);
            bv_bcmd_(&cmd, &len, &size, ".o", 0);
            bv_bcmd_(&cmd, &len, &size, flags, 1);
            bv_bcmd_(&cmd, &len, &size, mi->extra_flags, 1);
            bv_bcmd_(&cmd, &len, &size, mi->src, 1);

#if BEAVER_ASYNC == 1
            async_noawait(bv_async_call_, cmd);
            cmd = NULL;
            len = 0;
            size = 0;
#else
            call_or_panic(cmd);
            *cmd = 0;
            len = 0;
#endif
        }

        bv_eflags_add_(mi->extra_flags);
    }
    free(cmd);
}

static inline void compile(char** program, char* flags)
{

    bv_check_build_dir_();
    bv_eflags_ = bv_set_create_(BEAVER_EXTRA_FLAGS_BUFFER_SIZE);
    bv_files_ = bv_set_create_(modules_len);
    bv_modules_ = bv_set_create_(modules_len);

// compile modules asynchronisly
#if BEAVER_ASYNC == 1
    asaw_init(4);
#endif
    {
        char** pi = NULL;
        for (pi = program; *pi; pi++) {
            bv_compile_module_(*pi, flags);
        }
    }
#if BEAVER_ASYNC == 1
    asaw_free();
#endif

    // compile everything together
    {
        char* cmd = NULL;
        uint32_t len = 0;
        uint32_t size = 0;
        bv_bcmd_(&cmd, &len, &size, COMPILER " -o out", 0);
        bv_bcmd_(&cmd, &len, &size, flags, 1);

        char** mi = NULL;

        // extra flags
        for (mi = bv_eflags_->set; mi != bv_eflags_->set + bv_eflags_->size;
             ++mi) {
            if (*mi == NULL) {
                continue;
            }
            bv_bcmd_(&cmd, &len, &size, *mi, 1);
        }

        // sources
        for (mi = bv_files_->set; mi != bv_files_->set + bv_files_->size;
             ++mi) {
            if (*mi == NULL) {
                continue;
            }
            bv_bcmd_(&cmd, &len, &size, BEAVER_DIRECTORY, 1);
            bv_bcmd_(&cmd, &len, &size, *mi, 0);
            bv_bcmd_(&cmd, &len, &size, ".o", 0);
        }

        call_or_panic(cmd);
        free(cmd);
    }

    bv_set_free_(bv_files_);
    bv_set_free_(bv_eflags_);
    bv_set_free_(bv_modules_);
}

#endif
