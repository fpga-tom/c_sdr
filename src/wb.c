#include <liquid/liquid.h>
#include <math.h>
#include "sdr.h"


#define WB_SIZE 16

static bq_t wb_bq;
static sdr_thread_t *wb_thread;
static firpfbch_crcf qs;
static packet_t wbs[NUM_CHANNELS];

static void process(packet_t *p) {
}


static void *wb_td(void *arg) {
	int num_channels = NUM_CHANNELS;
	int _m = 2;
	float As    = 60.0f;                                                                                      
	qs = firpfbch_crcf_create_kaiser(LIQUID_SYNTHESIZER,num_channels,_m,As);

	printf("synthesis channelizer: %d\n", num_channels);
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
	for(int i=0;i<NUM_CHANNELS;i++) {
		list_add_tail(&wbs[i].list, &wb_bq.p);
	}
	bq_unlock(&wb_bq);
	return 1;
}

static int start() {
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
