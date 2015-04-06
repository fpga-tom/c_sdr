#ifndef __LINUX_AL__
#define __LINUX_AL__

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

enum LOGLEVEL { ERROR,INFO,DEBUG };
#define sdr_log(level, message) printf("%u %s\n",level,message)

static inline void *sdr_malloc(size_t s) {
	return malloc(s);
}

typedef pthread_t sdr_thread_t;
typedef pthread_mutex_t sdr_mutex_t;
typedef pthread_cond_t sdr_cond_t;

static inline int sdr_thread_create(sdr_thread_t *t, void *(*start_routine)(void*), void *arg) {
	return pthread_create(t, NULL, start_routine, arg);
}

static inline void sdr_mutex_init(sdr_mutex_t *mutex) {
	pthread_mutex_init(mutex, NULL);
}

static inline void sdr_mutex_destroy(sdr_mutex_t *mutex) {
	pthread_mutex_destroy(mutex);
}

static inline void sdr_mutex_lock(sdr_mutex_t *mutex) {
	if(pthread_mutex_lock(mutex))
		printf("error locking\n");
}

static inline void sdr_mutex_unlock(sdr_mutex_t *mutex) {
	if(pthread_mutex_unlock(mutex)) {
		printf("error unlocking\n");
		abort();
	}
}

static inline void sdr_cond_init(sdr_cond_t *cond) {
	pthread_cond_init(cond, NULL);
}

static inline void sdr_cond_destroy(sdr_cond_t *cond) {
	pthread_cond_destroy(cond);
}

static inline void sdr_cond_wait(sdr_cond_t *cond, sdr_mutex_t *mutex) {
	if(pthread_cond_wait(cond, mutex))
		printf("error waiting\n");
}

static inline void sdr_cond_broadcast(sdr_cond_t *cond) {
	if(pthread_cond_broadcast(cond))
		printf("error broadcasting");
}

#endif /* __LINUX_AL__ */
