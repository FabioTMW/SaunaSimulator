#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/shm.h>
#include <math.h>
#include <string.h>
#include "request.h"

FILE *fp_log;
SharedStruct *pt;
EndStats *results;
long max_duration;
double starting_time;
int fd_entrada, fd_rejeitados, max_requests, running;
pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;

void sigint_handler(int signo);
void *tpedido(void *arg);
void *tupdater(void *arg);
void *thandler(void *arg);
void open_results();
double calc_miliseconds(struct timespec curr_time);
double get_time();
double elapsed_time();
void open_log_file();
int write_to_log(int rid, char sex, long duration, char *type);
void open_fifos();
void open_shared();
void create_requests(pthread_t *tid);
void wait_requests_end(pthread_t *tid);
void create_updater(pthread_t *tidh);
void create_handler(pthread_t *tidh);
void wait_thread(pthread_t tidh);
void close_files();
void print_results();
void ending_cleanup();

int main(int argc, char *argv[]){

	// Setting the exection starting time in miliseconds
	starting_time = get_time();

	if (argc != 3) {
		printf("Usage: %s <n. pedidos> <max. utilização>\n", argv[0]);
		exit(1);
	}

	// Initial setup
	srand(time(NULL));
	signal(SIGINT, sigint_handler);

	max_requests = atoi(argv[1]);
	max_duration = atol(argv[2]);

	// Closing stats
	results = (EndStats *)malloc(sizeof(EndStats));
	open_results();

	running = 1;

	// Opening files
	char log_name[] = "/tmp/ger.";
	open_log_file(log_name);

	open_fifos();

	// Shared memory
	open_shared();

	// Generation of threads with unique id
	pthread_t tid[max_requests];
	create_requests(tid);

	// Waiting for generation threads to end
	wait_requests_end(tid);

	// Handler for incoming requests
	pthread_t tidh;
	create_handler(&tidh);

	// Handler for termination
	pthread_t tidu;
	create_updater(&tidu);


	// Waiting for threads
	wait_thread(tidh);
	wait_thread(tidu);

	// Print stats
	print_results();

	// End Gerador
	ending_cleanup();
	pthread_exit(NULL);
}

void sigint_handler(int signo) {
	print_results();
	ending_cleanup();
	exit(1);
}

void *tpedido(void *arg){

	int rid = *(int *)arg;

	// Request initialization
	Request *request = (Request*) calloc(1, sizeof(Request));
	request->rid = rid;

	if(rand()%2){
		request->sex = 'F';
	} else {
		request->sex = 'M';
	}


	request->duration = rand()%max_duration;
	request->rejections = 0;

	// Write calls protected with mutex locking
	pthread_mutex_lock(&write_mutex);
	if(write(fd_entrada, request, sizeof(Request)) == -1){
		perror("Failed to write to fifo entrada");
		exit(1);
	}
	pthread_mutex_unlock(&write_mutex);

	pthread_mutex_lock(&pt->produced_lock);
	pt->produced++;
	pthread_cond_signal(&pt->produced_cond);
	pthread_mutex_unlock(&pt->produced_lock);

	pthread_mutex_lock(&write_mutex);
	write_to_log(request->rid, request->sex, request->duration, "PEDIDO");

	if(request->sex == 'F'){
		results->f_ger++;
	}else{
		results->m_ger++;
	}
	pthread_mutex_unlock(&write_mutex);

	free(arg);
	free(request);
	pthread_exit(NULL);
}

void *tupdater(void *arg){
	// Need to improve (busy waiting)
	while(running){

		pthread_mutex_lock(&pt->items_lock);
		int t_ended = ((pt->served_requests+pt->discarted_requests)==pt->total_requests);
		pthread_mutex_unlock(&pt->items_lock);

		if(t_ended){

			running = 0;

			pthread_mutex_lock(&pt->rejected_lock);
			pthread_cond_signal(&pt->rejected_cond);
			pthread_mutex_unlock(&pt->rejected_lock);
		}
		sleep(1);
	}
	pthread_exit(NULL);
}

void *thandler(void *arg){

	while(TRUE){

		pthread_mutex_lock(&pt->rejected_lock);
		while((!(pt->rejected > 0)) && running)
			pthread_cond_wait(&pt->rejected_cond, &pt->rejected_lock);
		pt->rejected--;
		pthread_mutex_unlock(&pt->rejected_lock);

		if(!running){
			pthread_exit(NULL);
		}

		Request *request = (Request *)malloc(sizeof(Request));
		read(fd_rejeitados, request, sizeof(Request));

		pthread_mutex_lock(&write_mutex);
		write_to_log(request->rid, request->sex, request->duration, "REJEITADO");

		if(request->sex == 'F'){
			results->f_rej++;
		}else{
			results->m_rej++;
		}
		pthread_mutex_unlock(&write_mutex);

		if(request->rejections == 3){
			// Delete request
			pthread_mutex_lock(&pt->items_lock);
			pt->discarted_requests++;
			pthread_mutex_unlock(&pt->items_lock);

			pthread_mutex_lock(&write_mutex);
			write_to_log(request->rid, request->sex, request->duration, "DESCARTADO");

			if(request->sex == 'F'){
				results->f_desc++;
			}else{
				results->m_desc++;
			}
			pthread_mutex_unlock(&write_mutex);
		} else {
			// Resend to sauna
			pthread_mutex_lock(&write_mutex);
			if(write(fd_entrada, request, sizeof(Request)) == -1){
				perror("Failed to write to fifo entrada");
				exit(1);
			}
			write_to_log(request->rid, request->sex, request->duration, "PEDIDO");

			if(request->sex == 'F'){
				results->f_ger++;
			}else{
				results->m_ger++;
			}
			pthread_mutex_unlock(&write_mutex);

			pthread_mutex_lock(&pt->produced_lock);
			pt->produced++;
			pthread_cond_signal(&pt->produced_cond);
			pthread_mutex_unlock(&pt->produced_lock);
		}
		free(request);
	}
	pthread_exit(NULL);
}

void open_results(){
	results->f_ger = 0;
	results->m_ger = 0;
	results->f_rej = 0;
	results->m_rej = 0;
	results->f_desc = 0;
	results->m_desc = 0;
}

void print_results(){
	printf("\nPedidos gerados:\n");
	printf("Total:%d\n", results->f_ger + results->m_ger);
	printf("Femininos:%d Masculinos:%d\n", results->f_ger, results->m_ger);

	printf("Rejeicoes:\n");
	printf("Total:%d\n", results->f_rej + results->m_rej);
	printf("Femininas:%d Masculinas:%d\n", results->f_rej, results->m_rej);

	printf("Descartados:\n");
	printf("Total:%d\n", results->f_desc+ results->m_desc);
	printf("Femininos:%d Masculinos:%d\n", results->f_desc, results->m_desc);
}

double calc_miliseconds(struct timespec curr_time) {
	return (curr_time.tv_sec * 1000.0) + (floor((curr_time.tv_nsec / 1000000.0)*100)/100.0);
}

double get_time(){

	struct timespec curr_time;
	if((clock_gettime(CLOCK_MONOTONIC, &curr_time)) == -1){
		perror("Failed to get_time");
		exit(1);
	}

	return calc_miliseconds(curr_time);
}

double elapsed_time(){
	return get_time() - starting_time;
}

void open_log_file(char *log_name){
	// Adding pid to path
	char spid[BUFFER_SIZE];
	spid[0] = 0;
	if(sprintf(spid, "%d", getpid()) < 0){
		perror("Failed to build path");
		exit(1);
	}
	strcat(log_name, spid);

	if ((fp_log = fopen(log_name, "a")) == NULL) {
		perror("Failed to open fp_log");
		exit(1);
	}
}

int write_to_log(int rid, char sex, long duration, char *type){
	if(fprintf(fp_log, "inst:%-10.2f pid:%-7d p:%-3d g:%-3c dur:%-5d tip:%s\n", elapsed_time(), getpid(), rid, sex,	duration, type) < 0){
		fprintf(stderr, "Failed to write to log\n");
		return 1;
	}

	return 0;
}

void open_fifos(){
	// Only open for write fails if fifo is not yet open
	do{
		fd_entrada = open(FIFO_ENTRADA, O_WRONLY);
		if (fd_entrada == -1){
			printf("Waiting for Sauna to open\n");
			sleep(1);
		}
	} while (fd_entrada == -1);

	if ((fd_rejeitados = open(FIFO_REJEITADOS, O_RDONLY)) == -1) {
		perror("Failed to open fd_rejeitados");
		exit(1);
	}
}

void open_shared(){

	key_t key = SHARED_KEY;
	int shmid;
	/*if((key = ftok("sauna", 0)) == -1){
		perror("Failed to call ftok");
		exit(1);
	}*/

	if((shmid = shmget(key, 0, 0)) == -1){
		perror("Failed to call shmget");
		exit(1);
	}

	if((pt = (SharedStruct *) shmat(shmid, 0, 0)) == (void *) -1){
		perror("Failed to call shmat");
		exit(1);
	}

	pthread_mutex_lock(&pt->items_lock);
	pt->total_requests = max_requests;
	pthread_mutex_unlock(&pt->items_lock);
}

void create_requests(pthread_t *tid){

	int rc, t;
	int *thrArg;

	for(t=1; t<= max_requests; t++){

		thrArg = (int *) malloc(sizeof(t));
		*thrArg = t;

		rc = pthread_create(&tid[t-1], NULL, tpedido, thrArg);
		if (rc){
			printf("ERROR; return code from pthread_create() is %d\n", rc);
			exit(1);
		}
	}
}

void wait_requests_end(pthread_t *tid){

	int rc, i;
	for(i = 0; i < max_requests; i++){
		rc = pthread_join(tid[i], NULL);
		if (rc){
			printf("ERROR; return code from pthread_join() is %d\n", rc);
			exit(1);
		}
	}
}

void create_updater(pthread_t *tidu){
	int rc = pthread_create(tidu, NULL, tupdater, NULL);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		exit(1);
	}
}

void create_handler(pthread_t *tidh){
	int rc = pthread_create(tidh, NULL, thandler, NULL);
	if (rc){
		printf("ERROR; return code from pthread_create() is %d\n", rc);
		exit(1);
	}
}

void wait_thread(pthread_t tid){
	int rc = pthread_join(tid, NULL);
	if (rc){
		printf("ERROR; return code from pthread_join() is %d\n", rc);
		exit(1);
	}
}

void close_files(){
	if(fclose(fp_log) == EOF){
		perror("Failed to close fp_log");
		//exit(1);
	}

	if(close(fd_entrada) == -1){
		perror("Failed to close fd_entrada");
		//exit(1);
	}

	if(close(fd_rejeitados) == -1){
		perror("Failed to close fd_rejeitados");
		//exit(1);
	}
}

void ending_cleanup(){
	// Call if unexpected end
	// Stats release
	free(results);

	// Release shared memory
	if(shmdt(pt) == -1){
		perror("Failed to call shmdt");
		//exit(1);
	}

	// Close opened files
	close_files();
}
