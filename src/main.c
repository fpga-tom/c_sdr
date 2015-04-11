#include <stdio.h>
#include <unistd.h>
#include "sdr.h"


int main(int argc, char** argv) {
	packet_t p;
	int i=0;
	int j=0;
	int k=0;
//	printf("connecting to rtl sdr ...");
	surface.open(&argc, &argv);
	rtl_sdr.open(NULL);
	usleep(100000);
//	printf("connected\n");
	rtl_sdr.sps(SPS);
	rtl_sdr.tune(91200000);
//	rtl_sdr.tune(88800000);
	rtl_sdr.agc(1);
//	printf("starting rtl sdr ...");
	rtl_sdr.start();
	usleep(100000);
//	printf("started\n");
	dsp.open();
	dsp.start();
	usleep(100000);
	scheduler.start(dsp.demod_bq);
	usleep(100000);
	analyzer.start();
	usleep(100000);
	surface.start();



	printf("%u %u\n", j,k);
	return 0;
}
