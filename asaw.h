#ifndef FLOW_H
#define FLOW_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef void* (*thread_function_t)(void*);
typedef struct future_t future_t;



void* await(future_t* f);
future_t* async(thread_function_t fn, void* arg);
void asaw_free();
int asaw_init(uint_fast16_t size);
bool async_noawait(thread_function_t fn, void* arg);
bool asaw_is_available();

#endif
