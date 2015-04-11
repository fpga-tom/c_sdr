#include <sys/types.h>
#include <unistd.h>
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
static unsigned int M = (4);
static unsigned int M1 = 2;
static float kf = 0.8f;
static float fc = 0;
static freqdem demod;

static resamp_crcf resamp;
static resamp2_rrrf resamp2, resamp2_1, resamp2_2;
static iirdecim_crcf decim;
static firhilbf hilb;
static firfilt_crcf rrc;

static float *hr;
static unsigned int hr_len;

static pa_simple *stream = NULL;
static sdr_thread_t *play_thread;

static void process(packet_t *p) {
	float complex y[FFTW_SIZE/M];
	unsigned int ny = 0;
	float z[FFTW_SIZE/M];
	float z1[FFTW_SIZE/M/M1];
	float z2[FFTW_SIZE/M/M1/M1];
	float z3[FFTW_SIZE/M/M1/M1/M1];
	float complex h[FFTW_SIZE/M/2];
	float complex hb[FFTW_SIZE/M/2];
	float complex hr[FFTW_SIZE/M/2];
	int error;
	unsigned int nhr;

	for(int i=0;i<FFTW_SIZE/M;i++)
		iirdecim_crcf_execute(decim, &p->payload[M*i], &y[i]);
	freqdem_demodulate_block(demod, y, FFTW_SIZE/M, z);
	firhilbf_decim_execute(hilb, z, h);
	for(int i=0;i<FFTW_SIZE/M/2;i++) {
		firfilt_crcf_push(rrc, h[i]);
		firfilt_crcf_execute(rrc, &hb[i]);
	}
	resamp_crcf_execute_block(resamp, hb, FFTW_SIZE/M/2, hr, &nhr);

	for(int i=0;i<FFTW_SIZE/M/M1;i++)
		resamp2_rrrf_decim_execute(resamp2,&z[2*i], &z1[i]); 
	for(int i=0;i<FFTW_SIZE/M/M1/M1;i++)
		resamp2_rrrf_decim_execute(resamp2_1,&z1[2*i], &z2[i]); 
	for(int i=0;i<FFTW_SIZE/M/M1/M1/M1;i++)
		resamp2_rrrf_decim_execute(resamp2_2,&z2[2*i], &z3[i]); 
	for(int i=0;i<FFTW_SIZE/M/M1/M1;i++)
		z3[i]/=10;


	if (pa_simple_write(stream, z3, sizeof(z3), &error) < 0) {
		fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
	}
}

static void* dsp_td(void *arg) {

	unsigned int m = 5;
    unsigned int m1    = 8;     // resampling filter semi-length (filter delay)                               
    float As          = 60.0f;  // resampling filter stop-band attenuation [dB]                               
	float slsl = 60.0f;
	float beta = 1.0f;
	unsigned int m2 = 3;
	unsigned int k = (SPS/M)/2375;


	unsigned int h_len = 8;
	float r =  1.0f/k;
	float bw = 0.4f;
	float slsl1 = 60.f;
	unsigned int npfb=16;
	printf("dsp thread pid: %d\n", getpid());

	resamp = resamp_crcf_create(r, h_len, bw, slsl1, npfb);
	resamp2 = resamp2_rrrf_create(m1,0,As); 
	resamp2_1 = resamp2_rrrf_create(m1,0,As); 
	resamp2_2 = resamp2_rrrf_create(m1,0,As); 
	decim = iirdecim_crcf_create_default(M, 8);
	hilb = firhilbf_create(m, slsl);
	hr_len = 2*k*m2+1;
	hr = (float*) malloc(sizeof(float)*hr_len);
	if(!hr) {
		sdr_log(ERROR, "cannot allocate rrc");
	}
	liquid_firdes_rnyquist(LIQUID_FIRFILT_RRC, k, m2, beta, 0, hr);
	rrc = firfilt_crcf_create(hr, h_len);
	demod = freqdem_create(kf);


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
		 .rate = AUDIO,
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
	if(sdr_thread_create(play_thread, dsp_td, NULL)) {
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
