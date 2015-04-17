#ifndef __SDR_H__
#define __SDR_H__

#include <stdio.h>
#include <stdint.h>
#include <complex.h>
#include "list.h"
#include "linux_al.h"

#define BUF_SIZE (2048)
#define FFTW_SIZE (BUF_SIZE/2)
//#define SPS (1411200)
#define SPS (228000*4)
#define NUM_CHANNELS 10
#define BW (NUM_CHANNELS*SPS)
#define FM 150000
//#define AUDIO 44100
#define AUDIO (28500)


typedef struct {
	// q is for queue
	// p is for pool
	// m is for mutex
	// c is for cond
	struct list_head q;
	struct list_head p;
	sdr_mutex_t m;
	sdr_cond_t c;
} bq_t;

typedef struct {
	struct list_head avl; // availability list (free, used)
	uint8_t data[BUF_SIZE];
	uint64_t size;
} buf_t;

static inline void bq_init(bq_t *bq) {
	INIT_LIST_HEAD(&bq->q);
	INIT_LIST_HEAD(&bq->p);
	sdr_mutex_init(&bq->m);
	sdr_cond_init(&bq->c);
}

static inline void bq_lock(bq_t *bq) {
	sdr_mutex_lock(&bq->m);
}

static inline void bq_unlock(bq_t *bq) {
	sdr_mutex_unlock(&bq->m);
}

static inline void bq_wait(bq_t *bq) {
	sdr_cond_wait(&bq->c, &bq->m);
}

static inline void bq_broadcast(bq_t *bq) {
	sdr_cond_broadcast(&bq->c);
}

static inline void queue(struct list_head *l, bq_t *q) {
	list_move_tail(l, &q->q);
}

static inline void pool(struct list_head *l, bq_t *p) {
	list_move_tail(l, &p->p);
}


typedef struct {
	struct list_head list;
	float complex payload[FFTW_SIZE];
	size_t size;
	uint32_t freq;
} packet_t;

typedef struct {
	int (*open)(void *(*callback)(void*));
	int (*start)();
	int (*tune)(uint32_t);
	uint32_t (*freq)();
	int (*sps)(uint32_t);
	int (*agc)(int);
	void (*packet)(packet_t*);
	buf_t* (*poll)();
	buf_t* (*wait)();
	void (*offer)(buf_t*);
	void (*fill)(buf_t *, packet_t *);
} rtl_sdr_t;

typedef struct {
	int (*start)(bq_t*, bq_t*);
	packet_t *(*wait)();
	void (*offer)(packet_t*);
	void (*join)();
} scheduler_t;

typedef struct {
	int (*start)();
} analyzer_t;

typedef struct {
	void (*draw)(float* mag, float fps, float load, long sleep_time);
	void (*open)(int *, char***);
	void (*start)();
	bq_t *scatter_bq;
} surface_t;

typedef struct {
	int (*open)();
	int (*start)(bq_t *scatter_bq);
	bq_t *demod_bq;
} dsp_t;

typedef struct {
	int (*open)();
	int (*start)();
	bq_t *wb_bq;
} wb_t;

extern rtl_sdr_t rtl_sdr;
extern scheduler_t scheduler;
extern analyzer_t analyzer;
extern surface_t surface;
extern dsp_t dsp;
extern wb_t wb;

#endif /* __SDR_H__ */
