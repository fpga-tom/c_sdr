#include <stdio.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <complex.h>
#include <liquid/liquid.h>
#include "sdr.h"

#define DEMODQ_SIZE 128

static bq_t demod_bq;
static packet_t packets[DEMODQ_SIZE];
static decim_crcf decim;
static decim_rrrf decim1;
static unsigned int M = 16;
static unsigned int M1 = 2;
static unsigned int h_len = 21;
static liquid_fmtype type = LIQUID_MODEM_FM_DELAY_CONJ;
static float mod_index = 0.2f;
static float fc = 0;
static freqmodem demod;

static pa_simple *stream = NULL;
static sdr_thread_t *play_thread;

static void process(packet_t *p) {
	float complex y[FFTW_SIZE/M];
	float z[FFTW_SIZE/M];
	float z1[FFTW_SIZE/M/M1];
	int error;
	for(int i=0;i<FFTW_SIZE/M;i++) {
		decim_crcf_execute(decim,&p->payload[i*M],&y[i], 0);
		z[i] /= M;
	}
	for(int i=0;i<FFTW_SIZE/M;i++) {
		freqmodem_demodulate(demod, y[i],&z[i]);
	}
	for(int i=0;i<FFTW_SIZE/M/M1;i++) {
		decim_rrrf_execute(decim1,&z[i*M1],&z1[i], 0);
		z1[i] /= M1*50;
	}

	if (pa_simple_write(stream, z1, (size_t) sizeof(z1), &error) < 0) {
		fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
	}
}

static void* play_td(void *arg) {
	unsigned int h_len = 2*M*2+1; 
	unsigned int h_len1 = 2*M1*2+1; 
	float h[h_len];     // transmit filter                                                                    
	float g[h_len];     // receive filter (reverse of h)                                                      
	float h1[h_len1];     // transmit filter                                                                    
	float g1[h_len1];     // receive filter (reverse of h)                                                      
	design_rrc_filter(M,2,0.7f,0.3f,h);   
	design_rrc_filter(M1,2,0.1f,0.3f,h1);   
    unsigned int i;                                                                                           
    for (i=0; i<h_len; i++)                                                                                   
        g[i] = h[h_len-i-1];
    for (i=0; i<h_len1; i++)                                                                                   
        g1[i] = h1[h_len1-i-1];
	decim  = decim_crcf_create(M,g,h_len);
	decim1 = decim_rrrf_create(M1, g1, h_len1);

	demod = freqmodem_create(mod_index, fc, type);

	i = 0;
	while(1) {
		bq_lock(&demod_bq);
		while(list_empty(&demod_bq.q)) {
			bq_wait(&demod_bq);
		}
		packet_t *p = list_entry(demod_bq.q.next, packet_t, list);
		list_del(&p->list);
		bq_unlock(&demod_bq);
		process(p);
		bq_lock(&demod_bq);
		list_add_tail(&p->list, &demod_bq.p);
		bq_broadcast(&demod_bq);
		bq_unlock(&demod_bq);
	}
	pthread_exit(NULL);
}

// public interface

static
int open() {
	int i;
	static const pa_sample_spec ss = {
		 .format = PA_SAMPLE_FLOAT32LE,
		 .rate = 44100,
		 .channels = 1
	};
	bq_init(&demod_bq);
	bq_lock(&demod_bq);
	for(i=0;i<DEMODQ_SIZE;i++) {
		list_add_tail(&packets[i].list, &demod_bq.p);
	}
	bq_unlock(&demod_bq);
	int error;
	if (!(stream = pa_simple_new(NULL, "sdr", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error))) {
		  fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
		  return 0;
	}
	return 1;
}

static
int start() {
	int i;
	if(play_thread != NULL) {
		sdr_log(ERROR, "scheduler already running");
		return 0;
	}
	play_thread = (sdr_thread_t*)malloc(sizeof(sdr_thread_t));
	if(!play_thread) {
		sdr_log(ERROR, "scheduler malloc error occurred");
		return 0;
	}
	if(sdr_thread_create(play_thread, play_td, NULL)) {
		sdr_log(ERROR, "cannot create scheduler thread");
		return 0;
	}
	return 1;
}

dsp_t dsp = {
	.open = open,
	.start = start,
	.demod_bq = &demod_bq
};
