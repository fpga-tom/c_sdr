#include <liquid/liquid.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include "sdr.h"


#define WB_SIZE (2*NUM_CHANNELS)

static bq_t wb_bq;
static bq_t *fft_bq;
static sdr_thread_t *wb_thread;
static firpfbch_crcf qs;
static int flags[NUM_CHANNELS];
static packet_t wbs[NUM_CHANNELS];
static packet_t wb_buf[WB_SIZE];
static float complex polybuf[NUM_CHANNELS*FFTW_SIZE];
static uint32_t chan;
static int counter;

static void push() {
	for(int i=0;i<NUM_CHANNELS;i++) {
		bq_lock(fft_bq);
		while(list_empty(&fft_bq->p)) {
			bq_wait(fft_bq);
		}
		if(!list_empty(&fft_bq->p)) {
			packet_t *p = list_entry(fft_bq->p.next, packet_t, list);
			memcpy(p->payload, &polybuf[i*FFTW_SIZE], sizeof(p->payload));
			queue(&p->list, fft_bq);
			bq_broadcast(fft_bq);
		}
		bq_unlock(fft_bq);
	}
}

static void process(packet_t *p) {
	if(!flags[p->chan]) {
		flags[p->chan]=1;

		if(p->chan+1 < NUM_CHANNELS)
			flags[p->chan+1]=0;

		memcpy(wbs[p->chan].payload, p->payload, sizeof(p->payload));
		chan = (p->chan + 1) % NUM_CHANNELS;
		rtl_sdr.chan(chan);
		counter++;
		if(counter == NUM_CHANNELS) {
			float complex v[NUM_CHANNELS];
			for(int i=0;i<FFTW_SIZE;i++) {
				for(int k=0;k<NUM_CHANNELS;k++)
					v[k] = wbs[k].payload[i];
				firpfbch_crcf_synthesizer_execute(qs, v, &polybuf[i*NUM_CHANNELS]);
			}
			flags[0]=0;
			counter = 0;
			push();
		}
	}
}


static void *wb_td(void *arg) {
	int num_channels = NUM_CHANNELS;
	int _m = 4;
	float As = 60.0f;                                                                                      
	qs = firpfbch_crcf_create_kaiser(LIQUID_SYNTHESIZER,num_channels,_m,As);

	printf("synthesis channelizer: %d\n", num_channels);
	pid_t tid = syscall(SYS_gettid);
	printf("channelizer thread pid: %d\n", tid);
	while(1) {
		bq_lock(&wb_bq);
		while(list_empty(&wb_bq.q)) {
			bq_wait(&wb_bq);
		}
		packet_t *p = list_entry(wb_bq.q.next, packet_t, list);
		list_del(&p->list);
		bq_unlock(&wb_bq);
		process(p);
		bq_lock(&wb_bq);
		list_add_tail(&p->list, &wb_bq.p);
		bq_broadcast(&wb_bq);
		bq_unlock(&wb_bq);
	}
	pthread_exit(NULL);
}

// ------------------ public interface

static int open() {
	bq_init(&wb_bq);
	bq_lock(&wb_bq);
	for(int i=0;i<WB_SIZE;i++) {
		list_add_tail(&wb_buf[i].list, &wb_bq.p);
	}
	bq_unlock(&wb_bq);
	return 1;
}

static int start(bq_t *f) {
	fft_bq = f;
	if(wb_thread != NULL) {
		sdr_log(ERROR, "scheduler already running");
		return 0;
	}
	wb_thread = (sdr_thread_t*)malloc(sizeof(sdr_thread_t));
	if(!wb_thread) {
		sdr_log(ERROR, "scheduler malloc error occurred");
		return 0;
	}
	if(sdr_thread_create(wb_thread, wb_td, NULL)) {
		sdr_log(ERROR, "cannot create scheduler thread");
		return 0;
	}
	return 1;
};


wb_t wb = {
	.open = open,
	.start = start,
	.wb_bq = &wb_bq
};
