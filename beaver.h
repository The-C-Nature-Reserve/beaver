#ifndef BEAVER_H
#define BEAVER_H

#ifndef BV_ASYNC_
#define BV_REBUILD_ASYNC_
#endif

#ifdef ALWAYS_SYNC

#undef BV_REBUILD_ASYNC_
#undef BV_ASYNC_

#else

#endif // ALWAYS_SYNC

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef BEAVER_MAX_PROC
#define BEAVER_MAX_PROC 8
#endif

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

// TODO other platforms
#ifndef LINKER
#define LINKER "ld"
#endif // LINKER

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

// simple thread pool ---------------------------------------------------------

#ifdef BV_ASYNC_
#include <pthread.h>

#ifndef NUM_THREADS
#define NUM_THREADS 8
#endif

typedef struct bv_task_t_ bv_task_t_;
struct bv_task_t_ {
    void (*fn)(void*);
    void* arg;
    bv_task_t_* next;
};

typedef struct bv_pool_t_ bv_pool_t_;
struct bv_pool_t_ {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t threads[NUM_THREADS];
    bv_task_t_* stack;
    bool alive;
};

static void* bv_work_(void* a)
{
    bv_pool_t_* p = a;
    bool alive = 1;
    bv_task_t_* t = NULL;
    while (t != NULL || alive) {
        pthread_mutex_lock(&p->mutex);
        while (p->alive && p->stack == NULL) {
            pthread_cond_wait(&p->cond, &p->mutex);
        }
        t = p->stack;
        if (p->stack) {
            p->stack = p->stack->next;
        }
        alive = p->alive;
        pthread_mutex_unlock(&p->mutex);

        if (t) {
            t->fn(t->arg);
            free(t);
        }
    }
    return NULL;
}

bv_pool_t_* bv_pool_create_()
{
    bv_pool_t_* p = malloc(sizeof(*p));
    *p = (bv_pool_t_) {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .alive = 1,
        .stack = NULL,
    };

    pthread_t* t;
    for (t = p->threads; t != p->threads + NUM_THREADS; ++t) {
        pthread_create(t, NULL, bv_work_, p);
    }
    return p;
}

void bv_pool_free_(bv_pool_t_* p)
{
    if (p == NULL) {
        return;
    }

    pthread_mutex_lock(&p->mutex);
    p->alive = 0;
    pthread_mutex_unlock(&p->mutex);
    pthread_cond_broadcast(&p->cond);

    pthread_t* t;
    for (t = p->threads; t != p->threads + NUM_THREADS; ++t) {
        pthread_join(*t, NULL);
    }
    free(p);
}

void bv_pool_add_(bv_pool_t_* p, void (*fn)(void*), void* arg)
{
    bv_task_t_* t = malloc(sizeof(*t));
    t->fn = fn;
    t->arg = arg;

    pthread_mutex_lock(&p->mutex);
    t->next = p->stack;
    p->stack = t;
    pthread_mutex_unlock(&p->mutex);
    pthread_cond_signal(&p->cond);
}

#endif

// simple set -----------------------------------------------------------------

typedef struct bv_set_t_ bv_set_t_;
struct bv_set_t_ {
    char** set;
    uint32_t size;
    uint32_t used;
};

static inline bv_set_t_* bv_set_create_(uint32_t size)
{
    bv_set_t_* s = malloc(sizeof(*s));
    s->size = size << 2;
    s->set = calloc(s->size, sizeof(*s->set));
    s->used = 0;
    return s;
}

static inline void bv_set_free_(bv_set_t_* s)
{
    if (s == NULL) {
        return;
    }
    free(s->set);
    free(s);
}

static inline uint32_t bv_set_pos_(bv_set_t_* s, char* k)
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

static inline int bv_set_insert_(bv_set_t_* s, char* k)
{
    uint32_t p = bv_set_pos_(s, k);
    s->used += s->set[p] == NULL;
    if (s->used >= s->size) {
        return -1;
    }
    s->set[p] = k;
    return 0;
}

static inline bool bv_set_contains_(bv_set_t_* s, char* k)
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

#ifndef ALWAYS_SYNC
    bv_bcmd_(&cmd, &len, &size,
        COMPILER " -lpthread -DBV_ASYNC_ -o beaver beaver.c", 0);
#else
    bv_bcmd_(&cmd, &len, &size, COMPILER " -o beaver beaver.c", 0);
#endif
    if (argv != NULL) {
        bv_bcmd_(&cmd, &len, &size, "&&", 1);
        for (; *argv; argv++) {
            bv_bcmd_(&cmd, &len, &size, *argv, 1);
        }
    }

    call_or_panic(cmd);
    free(cmd);
    exit(0);
}

static inline void auto_update(char** argv)
{

#ifdef BV_REBUILD_ASYNC_
    bv_recompile_beaver_(argv);
#endif

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
        if (*s == '`') {
            do {
                s++;
            } while (*s && *s != '`');
            s++;
        } else {
            while (*s && !isspace(*s)) {
                s++;
            }
        }
        if (start != s) {
            *s = 0;
            bv_set_insert_(bv_eflags_, start);
            s++;
        }
    }
}

#ifdef BV_ASYNC_

static void bv_async_call_(void* cmd)
{
    printf(GREEN "[running" BLUE ":async" GREEN "]" RESET " %s\n", (char*)cmd);
    int r = system(cmd);
    if (r != 0) {
        fprintf(stderr, ORANGE "CBUILD WARN!: " RESET "%s\n", (char*)cmd);
    }
    free(cmd);
}

static bv_pool_t_* bv_pool_ = NULL;

#endif

static inline void bv_compile_file_(module_t* mi, char* flags)
{
    char* cmd = NULL;
    uint32_t len = 0;
    uint32_t size = 0;
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
    }
    if (should_recomp) {
        bv_bcmd_(&cmd, &len, &size, COMPILER " -c -o " BEAVER_DIRECTORY, 0);
        bv_bcmd_(&cmd, &len, &size, mi->src, 0);
        bv_bcmd_(&cmd, &len, &size, ".o", 0);
        bv_bcmd_(&cmd, &len, &size, flags, 1);
        bv_bcmd_(&cmd, &len, &size, mi->extra_flags, 1);
        bv_bcmd_(&cmd, &len, &size, mi->src, 1);

#ifdef BV_ASYNC_
        bv_pool_add_(bv_pool_, (void (*)(void*))bv_async_call_, cmd);
        cmd = NULL;
        len = 0;
        size = 0;
#else
        call_or_panic(cmd);
        *cmd = 0;
        len = 0;
#endif
    }
    free(cmd);
}

static inline void bv_compile_module_(char* name, char* flags)
{
    // module already compiled
    if (bv_set_contains_(bv_modules_, name)) {
        return;
    }
    bv_set_insert_(bv_modules_, name);
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
        bv_compile_file_(mi, flags);
        bv_eflags_add_(mi->extra_flags);
    }
}

static inline void compile(char** program, char* flags)
{
    bv_check_build_dir_();
    bv_eflags_ = bv_set_create_(BEAVER_EXTRA_FLAGS_BUFFER_SIZE);
    bv_files_ = bv_set_create_(modules_len);
    bv_modules_ = bv_set_create_(modules_len);

    // compile modules
    {
#ifdef BV_ASYNC_
        bv_pool_ = bv_pool_create_();
#endif
        char** pi = NULL;
        for (pi = program; *pi; pi++) {
            bv_compile_module_(*pi, flags);
        }
#ifdef BV_ASYNC_
        bv_pool_free_(bv_pool_);
#endif
    }

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

static inline void prepare_all(char* flags)
{
#ifdef BV_ASYNC_
    bv_pool_ = bv_pool_create_();
#endif

    module_t* mi;
    for (mi = modules; mi != modules + modules_len; mi++) {
        if (*mi->src != 0) {
            bv_compile_file_(mi, flags);
        }
    }

#ifdef BV_ASYNC_
    bv_pool_free_(bv_pool_);
#endif
}

static inline void compile_to_object(char** program, char* name, char* flags)
{

    bv_check_build_dir_();
    bv_eflags_ = bv_set_create_(BEAVER_EXTRA_FLAGS_BUFFER_SIZE);
    bv_files_ = bv_set_create_(modules_len);
    bv_modules_ = bv_set_create_(modules_len);

    // compile modules
    {
#ifdef BV_ASYNC_
        bv_pool_ = bv_pool_create_();
#endif
        char** pi = NULL;
        for (pi = program; *pi; pi++) {
            bv_compile_module_(*pi, flags);
        }
#ifdef BV_ASYNC_
        bv_pool_free_(bv_pool_);
#endif
    }

    // link everything together
    {
        char* cmd = NULL;
        uint32_t len = 0;
        uint32_t size = 0;
        bv_bcmd_(&cmd, &len, &size, LINKER " -r -o", 0);
        bv_bcmd_(&cmd, &len, &size, name, 1);
        char** mi = NULL;

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
