#include <stdio.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <math.h>
#include <string.h>
#include "request.h"

SharedStruct *pt;
FILE *fp_log;
char curr_sex;
EndStats *results;
double starting_time;
int fd_entrada, fd_rejeitados, max_cap, curr_cap, shmid;
pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t space_mutex = PTHREAD_MUTEX_INITIALIZER;

void sigint_handler(int signo);
void *twatcher(void *arg);
void *thandler(void *arg);
void open_results();
double calc_miliseconds(struct timespec curr_time);
double get_time();
double elapsed_time();
void open_log_file();
int write_to_log(pid_t tid, int rid, char sex, long duration, char *type);
void open_fifos();
void open_shared();
void init_sync_objects_in_shared_memory();
void create_handler(pthread_t *tidh);
void wait_handler_end(pthread_t tidh);
void wait_watchers(pthread_t *tis);
void close_files();
void print_results();
void ending_cleanup();

int main(int argc, char *argv[]){

	// Setting the exection starting time in miliseconds
	starting_time = get_time();

	if (argc != 2) {
		printf("Usage: %s <n. lugares>\n", argv[0]);
		exit(1);
	}

	// Initial setup
	signal(SIGINT, sigint_handler);

	max_cap=curr_cap=atoi(argv[1]);

	// Closing stats
	results = (EndStats *)malloc(sizeof(EndStats));
	open_results();

	// Shared memory
	open_shared();

	// Opening files
	char log_name[] = "/tmp/bal.";
	open_log_file(log_name);

	open_fifos();

	// Handler thread
	pthread_t tidh;
	create_handler(&tidh);

	// Waiting for Gerador to signal end
	wait_handler_end(tidh);

	// Print stats
	print_results();

	// End Sauna
	ending_cleanup();
	pthread_exit(NULL);
	return 0;
}

void sigint_handler(int signo) {
	ending_cleanup();
        exit(1);
}

void *twatcher(void *arg){

	Request *request = (Request *) arg;

	// Using Sauna
	usleep(request->duration*1000);

	pthread_mutex_lock(&space_mutex);
	curr_cap++;
	pthread_mutex_unlock(&space_mutex);

	pthread_mutex_lock(&pt->items_lock);
	pt->served_requests++;
	pthread_mutex_unlock(&pt->items_lock);

	// Linux API
	pid_t tid = syscall(SYS_gettid);
	// POSIX API
	//pid_t tid = pthread_self();

	pthread_mutex_lock(&write_mutex);
	write_to_log(tid, request->rid,	request->sex, request->duration, "SERVIDO");

	if(request->sex == 'F'){
		results->f_desc++;
	}else{
		results->m_desc++;
	}
	pthread_mutex_unlock(&write_mutex);

	free(request);
	pthread_exit(NULL);
}

void *thandler(void *arg){

	pthread_mutex_lock(&pt->items_lock);
	pthread_t tids[pt->total_requests];
	pthread_mutex_unlock(&pt->items_lock);

	int i = 0;
	// Linux API
	pid_t tid = syscall(SYS_gettid);
	// POSIX API
	//pid_t tid = pthread_self();
	while(TRUE){

		Request *request = (Request *)malloc(sizeof(Request));
		if(read(fd_entrada, request, sizeof(Request)) == 0){
			// FIFO closed
			printf("Sauna fechada.\n");
			free(request);
			wait_watchers(tids);
			pthread_exit(NULL);
		}

		pthread_mutex_lock(&pt->produced_lock);
		while(!(pt->produced > 0))
			pthread_cond_wait(&pt->produced_cond, &pt->produced_lock);
		pt->produced--;
		pthread_mutex_unlock(&pt->produced_lock);

		// First request to arrive defines Sauna sex
		pthread_mutex_lock(&space_mutex);
		if((curr_sex == 0) || (curr_cap == max_cap)){
			curr_sex = request->sex;
		}
		pthread_mutex_unlock(&space_mutex);

		pthread_mutex_lock(&write_mutex);
		write_to_log(tid, request->rid, request->sex, request->duration, "RECEBIDO");

		if(request->sex == 'F'){
			results->f_ger++;
		}else{
			results->m_ger++;
		}
		pthread_mutex_unlock(&write_mutex);

		pthread_mutex_lock(&space_mutex);
		if((curr_cap <= 0) || (curr_sex != request->sex) ){
			pthread_mutex_unlock(&space_mutex);

			request->rejections++;

			pthread_mutex_lock(&write_mutex);
			if(write(fd_rejeitados, request, sizeof(Request)) == -1){
				perror("Failed to write to fifo rejeitados");
				exit(1);
			}
			write_to_log(tid, request->rid,	request->sex, request->duration, "REJEITADO");

			if(request->sex == 'F'){
				results->f_rej++;
			}else{
				results->m_rej++;
			}
			pthread_mutex_unlock(&write_mutex);

			pthread_mutex_lock(&pt->rejected_lock);
			pt->rejected++;
			pthread_cond_signal(&pt->rejected_cond);
			pthread_mutex_unlock(&pt->rejected_lock);

			free(request);
		}else{
			curr_cap--;
			pthread_mutex_unlock(&space_mutex);

			if (pthread_create(&tids[i], NULL, twatcher, request) != 0) {
				perror("Failed to create watcher thread");
				exit(1);
			}
			i++;
		}
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
	printf("\nPedidos recebidos:\n");
	printf("Total:%d\n", results->f_ger + results->m_ger);
	printf("Femininos:%d Masculinos:%d\n", results->f_ger, results->m_ger);

	printf("Rejeicoes:\n");
	printf("Total:%d\n", results->f_rej + results->m_rej);
	printf("Femininas:%d Masculinas:%d\n", results->f_rej, results->m_rej);

	printf("Servidos:\n");
	printf("Total:%d\n", results->f_desc+ results->m_desc);
	printf("Femininos:%d Masculinos:%d\n", results->f_desc, results->m_desc);
}

double calc_miliseconds(struct timespec curr_time) {
	return (curr_time.tv_sec * 1000.0) +
		(floor((curr_time.tv_nsec / 1000000.0)*100)/100.0);
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

int write_to_log(pid_t tid, int rid, char sex, long duration, char *type){
	if(fprintf(fp_log, "inst:%-10.2f pid:%-7d tid:%-7d p:%-3d g:%-3c dur:%-5d tip:%s\n", elapsed_time(), getpid(), tid, rid, sex, duration,	type) < 0){
		fprintf(stderr, "Failed to write to log\n");
		return 1;
	}
	return 0;
}

void open_fifos(){

	if ((mkfifo(FIFO_ENTRADA, ACCESS)) == -1) {
		perror("Failed to mkfifo entrada");
		exit(1);
	}

	if ((mkfifo(FIFO_REJEITADOS, ACCESS)) == -1) {
		perror("Failed to mkfifo rejeitados");
		exit(1);
	}


	if ((fd_entrada = open(FIFO_ENTRADA, O_RDONLY)) == -1) {
		perror("Failed to open fd_entrada");
		exit(1);
	}

	// Only open for write fails if fifo is not yet open
	do{
		fd_rejeitados = open(FIFO_REJEITADOS, O_WRONLY);
		if (fd_rejeitados == -1){
			printf("Waiting for Gerador to open\n");
			sleep(1);
		}
	} while (fd_rejeitados== -1);
}

void open_shared(){
	key_t key = SHARED_KEY;
	/*if((key = ftok("sauna", 0)) == -1){
		perror("Failed to call ftok");
		exit(1);
	}*/

	if((shmid = shmget(key, sizeof(SharedStruct), IPC_CREAT | IPC_EXCL | SHM_R | SHM_W)) == -1){
		perror("Failed to call shmget");
		exit(1);
	}

	if((pt = (SharedStruct *) shmat(shmid, 0, 0)) == (void *) -1){
		perror("Failed to call shmat");
		exit(1);
	}

	pthread_mutex_lock(&pt->items_lock);
	pt->total_requests = 0;
	pt->discarted_requests = 0;
	pt->served_requests = 0;
	pt->produced = 0; /* Items in production fifo */
	pt->rejected = 0; /* Items in rejection fifo */
	pthread_mutex_unlock(&pt->items_lock);

	init_sync_objects_in_shared_memory();
}

void init_sync_objects_in_shared_memory(){

	pthread_mutexattr_t mattr;
	if(pthread_mutexattr_init(&mattr) != 0){
		fprintf(stderr, "Failed call pthread_mutexattr_init\n");
		exit(1);
	}
	if(pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED) != 0){
		fprintf(stderr, "Failed call pthread_mutexattr_setpshared\n");
		exit(1);
	}

	if(pthread_mutex_init(&pt->items_lock, &mattr) != 0){
		fprintf(stderr, "Failed call pthread_mutex_init\n");
		exit(1);
	}
	if(pthread_mutex_init(&pt->produced_lock, &mattr) != 0){
		fprintf(stderr, "Failed call pthread_mutex_init\n");
		exit(1);
	}
	if(pthread_mutex_init(&pt->rejected_lock, &mattr) != 0){
		fprintf(stderr, "Failed call pthread_mutex_init\n");
		exit(1);
	}


	pthread_condattr_t cattr;
	if(pthread_condattr_init(&cattr) != 0){
		fprintf(stderr, "Failed call pthread_condattr_init\n");
		exit(1);
	}
	if(pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED) != 0){
		fprintf(stderr, "Failed call pthread_condattr_setpshared\n");
		exit(1);
	}

	if(pthread_cond_init(&pt->items_cond, &cattr) != 0){
		fprintf(stderr, "Failed call pthread_cond_init\n");
		exit(1);
	}
	if(pthread_cond_init(&pt->produced_cond, &cattr) != 0){
		fprintf(stderr, "Failed call pthread_cond_init\n");
		exit(1);
	}
	if(pthread_cond_init(&pt->rejected_cond, &cattr) != 0){
		fprintf(stderr, "Failed call pthread_cond_init\n");
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

void wait_handler_end(pthread_t tidh){
	int rc = pthread_join(tidh, NULL);
	if (rc){
		printf("ERROR; return code from pthread_join() is %d\n", rc);
		exit(1);
	}
}

void wait_watchers(pthread_t *tids){
	int rc, i;
	for(i = 0; i < pt->served_requests; i++){
		rc = pthread_join(tids[i], NULL);
		if (rc){
			printf("ERROR; return code from pthread_join() is %d\n", rc);
			exit(1);
		}
	}
}

void close_files(){
	if(fclose(fp_log) == EOF){
		perror("Failed to close fp_log");
		exit(1);
	}

	if(close(fd_entrada) == -1){
		perror("Failed to close fd_entrada");
		exit(1);
	}

	if(close(fd_rejeitados) == -1){
		perror("Failed to close fd_rejeitados");
		exit(1);
	}

	if(unlink(FIFO_ENTRADA) == -1){
		perror("Failed to close fifo entrada");
		exit(1);
	}

	if(unlink(FIFO_REJEITADOS) == -1){
		perror("Failed to close fifo rejeitados");
		exit(1);
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
	if(shmctl(shmid, IPC_RMID, NULL) == -1){
		perror("Failed to call shmctl");
		//exit(1);
	}

	// Close opened files
	close_files();

}
