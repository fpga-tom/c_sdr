#include "sdr.h"

static bq_t fft_bq;
static bq_t *demod_bq;

#define FFTQ_SIZE 4

static packet_t packets[FFTQ_SIZE];
static sdr_thread_t *sched_thread;


// common functions

static void *sched(void *arg) {
	while(1) {
		buf_t *b=rtl_sdr.wait();
		if(b) {
			bq_lock(&fft_bq);
			if(!list_empty(&fft_bq.p)) {
				packet_t *p = list_entry(fft_bq.p.next, packet_t, list);
				rtl_sdr.fill(b, p);
				queue(&p->list, &fft_bq);
				bq_broadcast(&fft_bq);
			}
			bq_unlock(&fft_bq);
			bq_lock(demod_bq);
			if(!list_empty(&demod_bq->p)) {
				packet_t *p = list_entry(demod_bq->p.next, packet_t, list);
				rtl_sdr.fill(b, p);
				queue(&p->list, demod_bq);
				bq_broadcast(demod_bq);
			}
			bq_unlock(demod_bq);
			rtl_sdr.offer(b);
		}
	}
	pthread_exit(NULL);
}

// public interface
static
int start(bq_t *dq) {
	int i;
	demod_bq = dq;
	bq_init(&fft_bq);
	for(i=0;i<FFTQ_SIZE;i++) {
		list_add_tail(&packets[i].list, &fft_bq.p);
	}
	if(sched_thread != NULL) {
		sdr_log(ERROR, "scheduler already running");
		return 0;
	}
	sched_thread = (sdr_thread_t*)malloc(sizeof(sdr_thread_t));
	if(!sched_thread) {
		sdr_log(ERROR, "scheduler malloc error occurred");
		return 0;
	}
	if(sdr_thread_create(sched_thread, sched, NULL)) {
		sdr_log(ERROR, "cannot create scheduler thread");
		return 0;
	}
	return 1;
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

static
void join() {
	pthread_join(*sched_thread, NULL);
}


scheduler_t scheduler = {
	.start = start,
	.wait = wait,
	.offer = offer,
	.join = join
};
