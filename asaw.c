#include "asaw.h"

typedef enum {
    FUTURE_AWAITED,
    FUTURE_DONE,
    FUTURE_NOT_AWAITED,
} future_state_t;

struct future_t {
    future_state_t state;

    void* arg;
    thread_function_t function;

    future_t* next;
};

typedef struct msg_t_ msg_t_;
struct msg_t_ {
    size_t count;
    bool death;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

typedef struct slave_t_ slave_t_;
typedef struct flow_t flow_t;
typedef struct fq_t_ fq_t_;
struct fq_t_ {
    pthread_mutex_t mutex;
    future_t* head;
    future_t* tail;
};

struct slave_t_ {
    uint_fast16_t i;
    pthread_t thread;
    msg_t_ msg;
};

struct flow_t {
    slave_t_* slaves;
    fq_t_* queues;
    uint8_t size;
};

static inline uint64_t rand_()
{
    static uint8_t __thread d = 0;
    return ++d;
}

static flow_t* aas;

// msg_t_
//--------------------------------------------------------------------------------------------------------------------------------

#define MSG_INITIALIZER \
    (msg_t_) { .count = 0, .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .death = 0 }

static inline bool msg_is_dead_wait_(msg_t_* msg)
{
    bool is_dead;
    pthread_mutex_lock(&msg->mutex);
    if (msg->count == 0 && !msg->death) {
        pthread_cond_wait(&msg->cond, &msg->mutex);
    }
    msg->count--;
    is_dead = msg->count == 0 && msg->death;
    pthread_mutex_unlock(&msg->mutex);
    return is_dead;
}

static inline void msg_send_(msg_t_* msg)
{
    pthread_mutex_lock(&msg->mutex);
    msg->count++;
    pthread_mutex_unlock(&msg->mutex);
    pthread_cond_signal(&msg->cond);
}

static inline void msg_send_death_(msg_t_* msg)
{
    pthread_mutex_lock(&msg->mutex);
    msg->count++;
    msg->death = 1;
    pthread_mutex_unlock(&msg->mutex);
    pthread_cond_signal(&msg->cond);
}

// future_t
//--------------------------------------------------------------------------------------------------------------------------------

#define FUTURE_NOAWAIT 0
#define FUTURE_AWAIT 1

static void inline future_exec_(future_t* f)
{
    f->arg = f->function(f->arg);
    switch (f->state) {
    case FUTURE_DONE:
        __builtin_unreachable(); // done tasks can't be handled;
        return;
    case FUTURE_AWAITED:
        f->state = FUTURE_DONE;
        return;
    case FUTURE_NOT_AWAITED:
        free(f);
        return;
    }
}

static inline void future_init_(future_t* f, future_state_t state, thread_function_t fn, void* arg)
{
    *f = (future_t) {
        .arg = arg,
        .function = fn,
        .next = NULL,
        .state = state,
    };
}

static inline future_t* future_create_(future_state_t state, thread_function_t fn, void* arg)
{
    future_t* f = malloc(sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    future_init_(f, state, fn, arg);
    return f;
}

// fq_t_
//--------------------------------------------------------------------------------------------------------------------------------

static inline future_t* fq_pop_(fq_t_* q)
{
    future_t* f = NULL;
    if (q->head != NULL) {
        f = q->head;
        q->head = q->head->next;
        f->next = NULL;
    }
    return f;
}

static inline void fq_push_(fq_t_* q, future_t* f)
{
    f->next = NULL;
    if (q->head != NULL) {
        q->tail = q->tail->next = f;
    } else {
        q->head = q->tail = f;
    }
}

static inline bool fq_try_pop_(fq_t_* q, future_t** fp)
{
    if (pthread_mutex_trylock(&q->mutex) == 0) {
        *fp = fq_pop_(q);
        pthread_mutex_unlock(&q->mutex);
        return *fp != NULL;
    }
    return 0;
}

static inline bool fq_try_push_(fq_t_* q, future_t* f)
{
    if (pthread_mutex_trylock(&q->mutex) == 0) {
        fq_push_(q, f);
        pthread_mutex_unlock(&q->mutex);
        return 1;
    }
    return 0;
}

static inline future_t* fq_force_pop_(fq_t_* q)
{
    future_t* f;
    pthread_mutex_lock(&q->mutex);
    f = fq_pop_(q);
    pthread_mutex_unlock(&q->mutex);
    return f;
}
static inline void fq_force_push_(fq_t_* q, future_t* f)
{
    pthread_mutex_lock(&q->mutex);
    fq_push_(q, f);
    pthread_mutex_unlock(&q->mutex);
}

// slave_t_ flow_t
//--------------------------------------------------------------------------------------------------------------------------------

static inline future_t* flow_pop_(unsigned int i)
{
    future_t* f;
    unsigned int j;
    for (j = i; j < aas->size * 4 + i; j++) {
        if (fq_try_pop_(&aas->queues[j % aas->size], &f)) {
            return f;
        }
    }
    return fq_force_pop_(&aas->queues[i]);
}

static inline void* slave_work_(slave_t_* me)
{
    future_t* f;
    bool alive = 1;

    while (alive) {
        alive = !msg_is_dead_wait_(&me->msg);
        f = flow_pop_(me->i);
        if (f != NULL) {
            future_exec_(f);
        }
    }
    return NULL;
}

int asaw_init(uint_fast16_t size)
{
    aas = malloc(sizeof(*aas));
    if (aas == NULL) {
        return -1;
    }
    slave_t_* slaves = malloc(size * sizeof(*slaves));
    fq_t_* queues = malloc(size * sizeof(*queues));

    if (slaves == NULL || queues == NULL) {
        free(aas);
        free(slaves);
        free(queues);
        return -1;
    }

    *aas = (flow_t) {
        .slaves = slaves,
        .queues = queues,
        .size = size
    };
    uint_fast16_t i;
    for (i = 0; i < size; i++) {
        queues[i] = (fq_t_) {
            .mutex = PTHREAD_MUTEX_INITIALIZER,
            .head = NULL,
            .tail = NULL,
        };
    }

    bool err = 0;
    for (i = 0; i < size; i++) {
        slaves[i] = (slave_t_) {
            .thread = 0,
            .msg = MSG_INITIALIZER,
            .i = i,
        };
        if (pthread_create(&slaves[i].thread, NULL, (thread_function_t)slave_work_, &slaves[i]) != 0) {
            err = 1;
        }
    }
    if (err) {
        free(aas->slaves);
        free(aas->queues);
        free(aas);
        return -1;
    }
    return 0;
}

static inline void kill_slaves_(slave_t_* slaves, uint_fast16_t size)
{
    slave_t_* slave;
    for (slave = slaves; slave != slaves + size; slave++) {
        msg_send_death_(&slave->msg);
    }
    for (slave = slaves; slave != slaves + size; slave++) {
        pthread_join(slave->thread, NULL);
    }
}

void asaw_free()
{
    if (aas != NULL) {
        kill_slaves_(aas->slaves, aas->size);
        free(aas->queues);
        free(aas->slaves);
        free(aas);
    }
}

static inline void flow_push_(future_t* f)
{
    uint_fast16_t i = rand_() % aas->size;
    uint_fast16_t j;
    for (j = i; j < aas->size * 4 + i; j++) {
        if (fq_try_push_(&aas->queues[j % aas->size], f)) {
            msg_send_(&aas->slaves[j % aas->size].msg);
            return;
        }
    }
    fq_force_push_(&aas->queues[i % aas->size], f);
    msg_send_(&aas->slaves[i % aas->size].msg);
}

future_t* async(thread_function_t fn, void* arg)
{
    future_t* f = future_create_(FUTURE_AWAITED, fn, arg);
    if (f == NULL) {
        return NULL;
    }
    flow_push_(f);
    return f;
}

bool async_noawait(thread_function_t fn, void* arg)
{
    future_t* f = future_create_(FUTURE_NOT_AWAITED, fn, arg);
    if (f == NULL) {
        return 0;
    }
    flow_push_(f);
    return 1;
}

void* await(future_t* f)
{
    future_t* m;
    while (f->state != FUTURE_DONE) {
        m = flow_pop_(rand_() % aas->size);
        if (m != NULL) {
            future_exec_(m);
        }
    }
    void* r = f->arg;
    free(f);
    return r;
}

bool asaw_is_available()
{
    return aas != NULL;
}
