#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include "sdr.h"

#define	RTL_TCP_COMMAND_SET_FREQUENCY	 0x01
#define	RTL_TCP_COMMAND_SET_SAMPLERATE	 0x02
#define	RTL_TCP_COMMAND_SET_GAIN_MODE	 0x03
#define	RTL_TCP_COMMAND_SET_GAIN		 0x04
#define	RTL_TCP_COMMAND_SET_FREQ_CORR	 0x05
#define	RTL_TCP_COMMAND_SET_IFGAIN		 0x06
#define	RTL_TCP_COMMAND_SET_AGC_MODE	 0x08

#define CMDQ_SIZE 1
#define BUFQ_SIZE 1

static bq_t buf_bq;
static bq_t cmd_bq;

static sdr_thread_t *cmd_thread;
static sdr_thread_t *rx_thread;
static uint32_t _freq;

#define HOST "127.0.0.1"
#define PORT 1234

static int sockfd;


typedef struct {
	struct list_head list;
	uint8_t cmd[5];
} sdr_cmd_t;

static sdr_cmd_t cmds[CMDQ_SIZE];
static buf_t bufs[BUFQ_SIZE];
static float lookup_table[256];

// common functions


static sdr_cmd_t* cmd(uint8_t c, uint32_t arg) {
	bq_lock(&cmd_bq);
	sdr_cmd_t *cmd;
	while(list_empty(&cmd_bq.p)) 
		bq_wait(&cmd_bq);
	cmd = list_entry(cmd_bq.p.next, sdr_cmd_t, list);
	sdr_log(INFO, "cmd");
	cmd->cmd[0] = c; 
	cmd->cmd[1] = (arg >> 24) & 0xff; 
	cmd->cmd[2] = (arg >> 16) & 0xff; 
	cmd->cmd[3] = (arg >> 8) & 0xff; 
	cmd->cmd[4] = (arg ) & 0xff; 
	queue(&cmd->list, &cmd_bq);
	bq_broadcast(&cmd_bq);
	bq_unlock(&cmd_bq);
	return cmd;
}



//---------------------------------------------------------
// command thread

static int sdr_connect(char* host, uint16_t port) {
	struct sockaddr_in addr;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) {
		sdr_log(ERROR, "Could not create socket");
		return 0;
	}
	memset(&addr, '0', sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if(inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
		sdr_log(ERROR, "inet_pton error occured");
		return 0;
	}
	if(connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		sdr_log(ERROR, "cannot connect");
		return 0;
	}

	return 1;
}

static void sdr_exec(sdr_cmd_t *c) {
	sdr_log(INFO, "executing command");
	if(write(sockfd, c->cmd, 5) != 5) {
		sdr_log(ERROR, "cmd write error occupied");
	}
}

static void *sdr_cmd_td(void *arg) {
	struct list_head *pos, *n;
	if(!sdr_connect(HOST, PORT)) {
		sdr_log(ERROR, "connect error");
	} else {
		while(1) {
			bq_lock(&cmd_bq);
			while(list_empty(&cmd_bq.q)) {
				bq_wait(&cmd_bq);
			}
			list_for_each_safe(pos,n,&cmd_bq.q) {
				sdr_cmd_t *c = list_entry(pos, sdr_cmd_t, list); 
				sdr_exec(c);
				pool(&c->list, &cmd_bq);
			}
			bq_broadcast(&cmd_bq);
			bq_unlock(&cmd_bq);
		}
	}

	pthread_exit(NULL);
}


//---------------------------------------------------------
// receiver thread

static void* rx(void *arg) {
	struct list_head *pos, *n;
	while(1) {
		bq_lock(&buf_bq);
		while(list_empty(&buf_bq.p)) {
			bq_wait(&buf_bq);
		}
		list_for_each_safe(pos,n,&buf_bq.p) {
			int index = 0;
			buf_t *c = list_entry(pos, buf_t, avl); 
//			buf_t *c = list_entry(buf_bq.p.next, buf_t, avl); 
			while(index != BUF_SIZE) {
				int r = read(sockfd, c->data+index, BUF_SIZE-index);
				if(r < 0) {
					sdr_log(ERROR, "rx read error occured");
					break;
				}
				index += r;
			}

			queue(&c->avl, &buf_bq);
		}
		bq_broadcast(&buf_bq);
		bq_unlock(&buf_bq);
	}
	pthread_exit(NULL);
}



// ---------------------------------------------------------
// public interface

static void fill(buf_t *buf, packet_t *packet) {
	int i;
	packet->freq = _freq;
	for(i=0;i<BUF_SIZE/2;i++) {
		packet->payload[i] = lookup_table[buf->data[2*i]&0xff]
			+ _Complex_I*lookup_table[buf->data[2*i+1]&0xff];
	}
}

static 
int open(void* (*c)(void*)) {
	int i;
	for(i=0;i<256;i++)
		lookup_table[i] = (i-127.4f)/128.0f;
	if(cmd_thread != NULL){
		sdr_log(ERROR, "command thread is already running");
		return 0;
	}	
	bq_init(&cmd_bq);
	bq_lock(&cmd_bq);
	for(i=0;i<CMDQ_SIZE;i++) {
		list_add_tail(&cmds[i].list, &cmd_bq.p);
	}
	bq_unlock(&cmd_bq);

	cmd_thread = (sdr_thread_t*) sdr_malloc(sizeof(sdr_thread_t));
	if(cmd_thread == NULL) {
		sdr_log(ERROR, "error allocating cmd_thread");
		return 0;
	}
	if(sdr_thread_create(cmd_thread, sdr_cmd_td, (void*)c)) {
		sdr_log(ERROR, "thread error");
		return 0;
	}
	return 1;
}

static
int start() {
	int i;
	if(rx_thread != NULL){
		sdr_log(ERROR, "rx thread is already running");
		return 0;
	}	
	bq_init(&buf_bq);
	bq_lock(&buf_bq);
	for(i=0;i<BUFQ_SIZE;i++) {
		list_add_tail(&bufs[i].avl, &buf_bq.p);
	}
	bq_unlock(&buf_bq);

	rx_thread = (sdr_thread_t*) sdr_malloc(sizeof(sdr_thread_t));
	if(rx_thread == NULL) {
		sdr_log(ERROR, "error allocating rx_thread");
		return 0;
	}
	if(sdr_thread_create(rx_thread, rx, NULL)) {
		sdr_log(ERROR, "rx thread error");
		return 0;
	}
	return 1;
}

static 
int tune(uint32_t f) {
	_freq = f;
	cmd(RTL_TCP_COMMAND_SET_FREQUENCY,f);
	return 0;
}

static
uint32_t freq() {
	return 0;
}

static 
int agc(int agc) {
	cmd(RTL_TCP_COMMAND_SET_AGC_MODE,agc);
	return 0;
}

static 
int sps(uint32_t sr) {
	cmd(RTL_TCP_COMMAND_SET_SAMPLERATE,sr);
	return 0;
}

static
void packet(packet_t *p) {
	bq_lock(&buf_bq);
	while(list_empty(&buf_bq.q)) {
		bq_wait(&buf_bq);
	}
	buf_t *c = list_entry(buf_bq.q.next, buf_t, avl); 
	fill(c, p);
	pool(&c->avl, &buf_bq);
	bq_broadcast(&buf_bq);
	bq_unlock(&buf_bq);
}

static
buf_t* wait() {
	bq_lock(&buf_bq);
	while(list_empty(&buf_bq.q)) {
		bq_wait(&buf_bq);
	}
	buf_t *c = list_entry(buf_bq.q.next, buf_t, avl); 
	list_del(&c->avl);
	bq_broadcast(&buf_bq);
	bq_unlock(&buf_bq);
	return c;
}

static
buf_t* poll() {
	buf_t *c = NULL;
	bq_lock(&buf_bq);
	if(!list_empty(&buf_bq.q)) {
		c = list_entry(buf_bq.q.next, buf_t, avl); 
		list_del(&c->avl);
		bq_broadcast(&buf_bq);
	}
	bq_unlock(&buf_bq);
	return c;
}

static
void offer(buf_t *b) {
	bq_lock(&buf_bq);
	list_add_tail(&b->avl, &buf_bq.p);
	bq_broadcast(&buf_bq);
	bq_unlock(&buf_bq);
}


rtl_sdr_t rtl_sdr = {
	.open = open,
	.start = start,
	.tune = tune,
	.freq = freq,
	.agc = agc,
	.sps = sps,
	.packet = packet,
	.poll = poll,
	.offer = offer,
	.fill = fill,
	.wait = wait
};

