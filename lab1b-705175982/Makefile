Bdefault:
	gcc -g -lz -Wall -Wextra lab1b-client.c -o lab1b-client
	gcc -g -lz -Wall -Wextra lab1b-server.c -o lab1b-server
clean:
	rm -f lab1b-client lab1b-server *.tar.gz
dist:
	tar -czf lab1b-705175982.tar.gz README Makefile lab1b-server.c lab1b-client.c
