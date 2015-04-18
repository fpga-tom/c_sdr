#include <fftw3.h>
#include <math.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <complex.h>
#include <fftw3.h>
#include <liquid/liquid.h>
#include "sdr.h"

#define DEMODQ_SIZE 128


static bq_t demod_bq;
static bq_t *scatter_bq;
static packet_t packets[DEMODQ_SIZE];
static unsigned int M = (4);
static unsigned int M1 = 2;
static float kf = 15./75.f;
//static float fc = 0;
static freqdem demod;
static int idx;
static int idx1;
static agc_rrrf agc;

static resamp_rrrf resamp;
static resamp2_rrrf resamp2, resamp2_1, resamp2_2;
static iirdecim_crcf decim;
static firhilbf hilb;
static firfilt_rrrf rrc;
static iirfilt_crcf lowpass;
static iirfilt_rrrf lowpass1;
static nco_crcf nco;
static symsync_rrrf symsync;

static float *hr;
static unsigned int hr_len;

static float *B,*A;
static unsigned int B_len;
static iirfilt_rrrf band;
static int last;

static pa_simple *stream = NULL;
static sdr_thread_t *play_thread;

static fftw_plan plan;
static double *in;
static fftw_complex *out;
#define FFTW_SIZE1 1024

static void process(packet_t *p) {
	float complex o[FFTW_SIZE];
	float complex y[FFTW_SIZE/M];
	float z[FFTW_SIZE/M];
	float z1[FFTW_SIZE/M/M1];
	float z2[FFTW_SIZE/M/M1/M1];
	float z3[FFTW_SIZE/M/M1/M1/M1];
	int error;

	for(int i=0;i<FFTW_SIZE;i++) {
		iirfilt_crcf_execute(lowpass, p->payload[i], &o[i]);
	}

	for(int i=0;i<FFTW_SIZE/M;i++)
		iirdecim_crcf_execute(decim, &o[M*i]/*&p->payload[M*i]*/, &y[i]);
	freqdem_demodulate_block(demod, y, FFTW_SIZE/M, z);

	for(int i=0;i<FFTW_SIZE/M/M1;i++)
		resamp2_rrrf_decim_execute(resamp2,&z[2*i], &z1[i]); 
	for(int i=0;i<FFTW_SIZE/M/M1/M1;i++)
		resamp2_rrrf_decim_execute(resamp2_1,&z1[2*i], &z2[i]); 
	for(int i=0;i<FFTW_SIZE/M/M1/M1/M1;i++)
		resamp2_rrrf_decim_execute(resamp2_2,&z2[2*i], &z3[i]); 
	for(int i=0;i<FFTW_SIZE/M/M1/M1/M1;i++) {
		z3[i]/=8;
	}


#if 0
	if (pa_simple_write(stream, z3, sizeof(z3), &error) < 0) {
		fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
	}
#endif
}

static void* dsp_td(void *arg) {

	unsigned int m = 5;
    unsigned int m1    = 8;     // resampling filter semi-length (filter delay)                               
    float As          = 60.0f;  // resampling filter stop-band attenuation [dB]                               
	float slsl = 60.0f;
	float beta = 1.0f;
	unsigned int m2 = 12;
	float k = (SPS/M)/(2375);
	printf("%f\n", k);


	unsigned int h_len = 64;
	float r =  1.0f/k;
	float bw = 0.4f;
	float slsl1 = 60.f;
	unsigned int npfb=64;
	pid_t tid = syscall(SYS_gettid);
	printf("dsp thread pid: %d\n", tid);

	resamp = resamp_rrrf_create(r, h_len, bw, slsl1, npfb);
	resamp2 = resamp2_rrrf_create(m1,0,As); 
	resamp2_1 = resamp2_rrrf_create(m1,0,As); 
	resamp2_2 = resamp2_rrrf_create(m1,0,As); 
	decim = iirdecim_crcf_create_default(M, 64);
	hilb = firhilbf_create(m, slsl);
	hr_len = 2*k*m2+1;
	hr = (float*) malloc(sizeof(float)*hr_len);
	if(!hr) {
		sdr_log(ERROR, "cannot allocate rrc");
	}
	liquid_firdes_rnyquist(LIQUID_FIRFILT_RRC, k, m2, beta, 0, hr);
	rrc = firfilt_rrrf_create(hr, hr_len);
	agc = agc_rrrf_create();
	nco = nco_crcf_create(LIQUID_VCO);
	float freq = 57000.0f/((SPS/M));
	printf("freq %f\n", freq);
	nco_crcf_set_frequency(nco,freq);
	demod = freqdem_create(kf);


	int num_filters = 64;
	symsync = symsync_rrrf_create_rnyquist(LIQUID_FIRFILT_RRC, k, m2, beta, num_filters); 

	unsigned int order = 5;
	float fc = (57000.0f-2400.0f)/(SPS/M);
	float f0 = 57000.0f/(SPS/M);
	float Ap = 1.0f;
	printf("%f %f\n", f0, fc);

	unsigned int N = 2*order;
	unsigned int r1 = N % 2;
	unsigned int L = (N-r1)/2;

	B_len = 3*(L+r1);
	B = (float*)malloc(sizeof(float)*B_len);
	if(!B) 
		sdr_log(ERROR, "iir B alloc error occured");
	A = (float*)malloc(sizeof(float)*B_len);
	if(!A) 
		sdr_log(ERROR, "iir A alloc error occured");

	band = iirfilt_rrrf_create_prototype(LIQUID_IIRDES_ELLIP,
                  LIQUID_IIRDES_BANDPASS,
                  LIQUID_IIRDES_SOS,
                  order,
                  fc, f0, Ap, As);

//	lowpass = iirfilt_crcf_create_lowpass(16, 75000.0/SPS);

	lowpass = iirfilt_crcf_create_prototype(LIQUID_IIRDES_ELLIP,
                  LIQUID_IIRDES_LOWPASS,
                  LIQUID_IIRDES_SOS,
                  order,
                  75000.0f/SPS, 0, Ap, As);

	lowpass1 = iirfilt_rrrf_create_prototype(LIQUID_IIRDES_ELLIP,
                  LIQUID_IIRDES_LOWPASS,
                  LIQUID_IIRDES_SOS,
                  order,
                  2400.0f/SPS, 0, Ap, As);


	in = (double*)fftw_malloc(sizeof(double)*FFTW_SIZE1);
	if(!in) {
		sdr_log(ERROR, "fftw in alloc error occurred");
		return 0;
	}
	out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*FFTW_SIZE1);
	if(!out) {
		sdr_log(ERROR, "fftw out alloc error occurred");
		return 0;
	}
	plan = fftw_plan_dft_r2c_1d(FFTW_SIZE1, in, out, FFTW_ESTIMATE);



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
int start(bq_t *_scatter_bq) {
	scatter_bq = _scatter_bq;
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
