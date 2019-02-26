#define ACCESS 0660
#define BUFFER_SIZE 256
#define FIFO_ENTRADA "/tmp/entrada"
#define FIFO_REJEITADOS "/tmp/rejeitados"
#define MAX_THREADS 256
#define SHARED_KEY 123123123
#define TRUE 1

typedef struct Request {
	int rid;
	char sex;
	long duration;
	int rejections;
} Request;

typedef struct EndStats{
	/* Diferent meanings for Sauna and Gerador */
	int f_ger, m_ger, f_rej, m_rej, f_desc, m_desc;
} EndStats;

typedef struct SharedStruct{
	int total_requests;
	int served_requests;
	int discarted_requests;
	int produced;
	int rejected;
	pthread_mutex_t items_lock;
	pthread_mutex_t produced_lock;
	pthread_mutex_t rejected_lock;
	pthread_cond_t items_cond;
	pthread_cond_t produced_cond;
	pthread_cond_t rejected_cond;
}SharedStruct;
