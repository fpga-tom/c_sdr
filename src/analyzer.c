#include <unistd.h>
#include <sys/time.h>
#include <complex.h>
#include <fftw3.h>
#include "sdr.h"

#define FRAME_RATE 30
#define MAX_FRAME_RATE 30
#define LOW_THRESHOLD 0.65
#define HIGH_THRESHOLD 0.85

static sdr_thread_t *ana_thread;
static fftw_complex *in, *out;
static fftw_plan plan;
static float mag[FFTW_SIZE];
static bq_t fft_bq;

#define FFTQ_SIZE 32

static packet_t packets[FFTQ_SIZE];

// utility functions

static long c_time() {
	struct timeval t;
	gettimeofday(&t, NULL);
	return t.tv_sec*1000000 + t.tv_usec;
}

static void process(packet_t *p) {
	int i;
	for(i=0;i<FFTW_SIZE && i<BUF_SIZE;i++) {
		in[i] = p->payload[i];
	}
	fftw_execute(plan);
	for(i=0;i<FFTW_SIZE/2;i++) {
		mag[i+FFTW_SIZE/2] = crealf(out[i])*crealf(out[i]) + cimagf(out[i])*cimagf(out[i]);
	}
	for(i=FFTW_SIZE/2;i<FFTW_SIZE;i++) {
		mag[i-FFTW_SIZE/2] = crealf(out[i])*crealf(out[i]) + cimagf(out[i])*cimagf(out[i]);
	}
}

static
packet_t *wait() {
	bq_lock(&fft_bq);
	while(list_empty(&fft_bq.q)) {
		bq_wait(&fft_bq);
	}
	packet_t *p = list_entry(fft_bq.q.next, packet_t, list);
	list_del(&p->list);
	bq_unlock(&fft_bq);
	return p;
}

static
void offer(packet_t *p) {
	bq_lock(&fft_bq);
	list_add_tail(&p->list, &fft_bq.p);
	bq_broadcast(&fft_bq);
	bq_unlock(&fft_bq);
}

// analyzer thread
//
static void *analyzer_td(void *arg) {
	int fps = FRAME_RATE;
	float load = 0.f;
	long sleep_time = 0;
	while(1) {
		long timestamp = c_time();
		packet_t *p = wait();
		process(p);
		offer(p);
		surface.draw(mag, fps, load, sleep_time);
		sleep_time = 1000000.0f/fps - (c_time() - timestamp);
		if(sleep_time > 0) {
			load = (c_time() - timestamp) / (1000000.0f/fps);
			if(load < LOW_THRESHOLD && fps < MAX_FRAME_RATE)
				fps++;
			if(load > HIGH_THRESHOLD && fps > 1)
				fps--;
			usleep(sleep_time);
		} else {
			if(fps > 1) {
				fps--;
				load = 1;
			}
		}

	}
	pthread_exit(NULL);
}

// public interface

static
int open() {
	bq_init(&fft_bq);
	bq_lock(&fft_bq);
	for(int i=0;i<FFTQ_SIZE;i++) {
		list_add_tail(&packets[i].list, &fft_bq.p);
	}
	bq_unlock(&fft_bq);
	return 1;
}

static
int start() {

	in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*FFTW_SIZE);
	if(!in) {
		sdr_log(ERROR, "fftw in alloc error occurred");
		return 0;
	}
	out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*FFTW_SIZE);
	if(!out) {
		sdr_log(ERROR, "fftw out alloc error occurred");
		return 0;
	}
	plan = fftw_plan_dft_1d(FFTW_SIZE, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

	if(ana_thread != NULL) {
		sdr_log(ERROR, "analyzer thread already running");
		return 0;
	}
	ana_thread = (sdr_thread_t*)malloc(sizeof(sdr_thread_t));
	if(!ana_thread) {
		sdr_log(ERROR, "error allocating analyzer thread occurred");
		return 0;
	}
	if(sdr_thread_create(ana_thread, analyzer_td, NULL)) {
		sdr_log(ERROR, "error creating analyzer thread");
		return 0;
	}
	return 1;
}

analyzer_t analyzer = {
	.open = open,
	.start = start,
	.fft_bq = &fft_bq
};
