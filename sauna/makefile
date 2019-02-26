all : sauna gerador

clean :
	rm -f ./bin/* /tmp/ger.* /tmp/bal.* /tmp/entrada /tmp/rejeitados

sauna: sauna.c
	gcc sauna.c -o ./bin/sauna -pthread -lrt -lm

gerador: gerador.c
	gcc gerador.c -o ./bin/gerador -pthread -lrt -lm
