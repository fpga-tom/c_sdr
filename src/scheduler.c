#include "sdr.h"

static bq_t *fft_bq;
static bq_t *demod_bq;
static bq_t *wb_bq;

static sdr_thread_t *sched_thread;


// common functions

static void *sched(void *arg) {
	while(1) {
		buf_t *b=rtl_sdr.wait();
		if(b) {
			/*
			bq_lock(fft_bq);
			if(!list_empty(&fft_bq->p)) {
				packet_t *p = list_entry(fft_bq->p.next, packet_t, list);
				rtl_sdr.fill(b, p);
				queue(&p->list, fft_bq);
				bq_broadcast(fft_bq);
			}
			bq_unlock(fft_bq);
			*/
			bq_lock(demod_bq);
			if(!list_empty(&demod_bq->p)) {
				packet_t *p = list_entry(demod_bq->p.next, packet_t, list);
				rtl_sdr.fill(b, p);
				queue(&p->list, demod_bq);
				bq_broadcast(demod_bq);
			}
			bq_unlock(demod_bq);
			bq_lock(wb_bq);
			if(!list_empty(&wb_bq->p)) {
				packet_t *p = list_entry(wb_bq->p.next, packet_t, list);
				rtl_sdr.fill(b, p);
				queue(&p->list, wb_bq);
				bq_broadcast(wb_bq);
			}
			bq_unlock(wb_bq);
			rtl_sdr.offer(b);
		}
	}
	pthread_exit(NULL);
}

// public interface
static
int start(bq_t *dq, bq_t *wbq, bq_t *fftq) {
	demod_bq = dq;
	wb_bq = wbq;
	fft_bq = fftq;
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
void join() {
	pthread_join(*sched_thread, NULL);
}


scheduler_t scheduler = {
	.start = start,
	.join = join
};
