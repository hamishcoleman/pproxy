/*
 * Simple Port Proxy program
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>

int process_connection(int fd, int addr, int port) {
	int rfd;
	struct sockaddr_in sa;

	rfd = socket(AF_INET,SOCK_STREAM,6);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = addr;
	if (connect(rfd,&sa,sizeof(sa)) == -1) {
		perror("connect");
		return -1;
	}

	while (1==1) {
		struct timeval tm;
		fd_set fdreads,fdwrites;
#define BUFSIZE	4096
		unsigned char buf[BUFSIZE];
		int i,j;
		
		tm.tv_sec = 10;
		tm.tv_usec = 0;
		FD_SET(fd,&fdreads);
		FD_SET(rfd,&fdreads);

		if (select(rfd + 1, &fdreads, NULL,NULL, &tm) == -1) {
			perror("select");
			return -1;
		}

		if (FD_ISSET(fd,&fdreads)) {
			i = read(fd,&buf,sizeof(buf));
			if (i < 1) {
/*				printf("fd read = %i\n",i); */
				close(rfd);
				return 0;
			}
			j = write(rfd,&buf,i);
			if (j < i) {
/*				printf("rfd write %i < %i\n",j,i); */
				close(rfd);
				return 0;
			}
		}
		if (FD_ISSET(rfd,&fdreads)) {
			i = read(rfd,&buf,sizeof(buf));
			if (i < 1) {
/*				printf("rfd read = %i\n",i); */
				close(rfd);
				return 0;
			}
			j = write(fd,&buf,i);
			if (j < i) {
/*				printf("fd write %i < %i\n",j,i); */
				close(rfd);
				return 0;
			}
		}
	}
}


int main( int argc, char **argv) {
	int portl, portr;
	int addrr;
	int sfd, i;
	struct sockaddr_in sa;
	int salen;
	struct hostent * he;
	
	if (argc < 3) {
		printf("Usage: %s <port> <dest addr> <dest port>\n",argv[0]);
		return -1;
	}
	
	portl = atoi(argv[1]);
	portr = atoi(argv[3]);
	he = gethostbyname(argv[2]);
	if (he == NULL) {
		printf("no hostname!\n");
		return -1;
	}
	memcpy(&addrr,he->h_addr_list[0],he->h_length);

/*
	printf("portl	%i\n",portl);
	printf("addrr	%08x\n",addrr);
	printf("portr	%i\n",portr);
*/

	sfd = socket(AF_INET,SOCK_STREAM,6);
	bzero(&sa,sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(portl);
	if (bind(sfd,&sa,sizeof(sa)) == -1) {
		perror("bind");
		return -1;
	}
	if (listen(sfd,1) == -1) {
		perror("listen");
		return -1;
	}
	
	while(1) {
		salen = sizeof(sa);
		if ((i = accept(sfd,&sa,&salen))== -1) {
			perror("accept");
			return -1;
		}
		if (!fork()) {	
			if (process_connection(i,addrr,portr) == -1) {
				printf("error\n");
			}
		}
		close(i);
	}
	return 0;
}
