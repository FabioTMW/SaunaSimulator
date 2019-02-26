# Sauna Simulator
Multi process and shared resources simulation of a sauna. Similar to the producer-consumer problem and makes use of threads, shared memory, mutexes and fifos.

This program simulates a sauna usage by allowing people to enter it, as long as it has the capacity for them.  
The first person to arrive at the sauna defines the gender allowed for the next persons to come in. If the sauna becomes empty it accepts any gender again.  
Each person can try to enter the sauna 3 times, after that the request is discarted.

## Usage

### Building
* cd sauna
* make

### Runing

* cd sauna/bin
* ./sauna <n. lugares>
  * the arg taken is the max allowed people to be in the sauna at any given time
* ./gerador <n. pedidos> <max. utilização>
  * the first arg sets the total requests sent to the sauna process
  * the second arg sets the max time in miliseconds any person can spend inside

### Cleaning

* make clean

### Logs

* logs are stored at /tmp folder
* sauna writes to bal.<sauna pid>
* gerador writes to ger.<gerador pid>

