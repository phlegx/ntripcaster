.PHONY: clean
CC=

all: caster client server

caster: caster.o util.o
	$(CC)g++ $^ -o $@ 

client: client.o util.o
	$(CC)g++ $^ -o $@ 

server: server.o util.o
	$(CC)g++ $^ -o $@ 

clean:
	rm -rf *.o caster client server
