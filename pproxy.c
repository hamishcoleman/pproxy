/*
 * Completely single-task port proxy program
 */

#ifdef __LCC__
#include <winsock.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>
#define closesocket close
#define SOCKET int
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

SOCKET establish_port(int local_addr, SOCKET local_port) {
	SOCKET fd;
	struct sockaddr_in sa;

	fd = socket(AF_INET, SOCK_STREAM, 6);
	memset(&sa,0,sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(local_port);
	sa.sin_addr.s_addr = local_addr;
	if (bind(fd,&sa,sizeof(sa)) == -1) {
		perror("bind");
		return -1;
	}
	if (listen(fd,1) == -1) {
		perror("listen");
		return -1;
	}

	return fd;
}

#define BUFSIZE	4096
struct connection_info {
	struct connection_info * next;
	char lbuf[BUFSIZE];	/* local fd buffer */
	char rbuf[BUFSIZE];	/* remote fd buffer */
	int lbufw;		/* local buffer write pointer */
	int rbufw;		/* remote buffer write pointer */
	int lbufr;		/* local buffer read pointer */
	int rbufr;		/* local buffer read pointer */
	SOCKET lfd;		/* local fd */
	SOCKET rfd;		/* remote fd */
};

struct connection_info * get_new_connection( SOCKET fd, int remote_addr, int remote_port ) {
	struct sockaddr_in sa;
	int salen;
	SOCKET rfd;
	SOCKET lfd;
	struct connection_info *p;

	salen = sizeof(sa);
	if ((lfd=accept(fd,&sa,&salen))==-1) {
		perror("accept");
		return NULL;
	}
    printf("Accept for %08x\n",sa.sin_addr.s_addr);

	rfd = socket(AF_INET, SOCK_STREAM, 6);
	sa.sin_family = AF_INET;
	sa.sin_port = htons(remote_port);
	sa.sin_addr.s_addr = remote_addr;
	if (connect(rfd,&sa,sizeof(sa)) == -1) {
		perror("connect");
		return NULL;
	}

	if ((p=malloc(sizeof(struct connection_info)))==NULL) {
		printf("malloc\n");
		return NULL;
	}
	
	memset(p,0,sizeof(struct connection_info));
	p->rfd = rfd;
	p->lfd = lfd;

	return p;
}

struct connection_info * destroy_connection( struct connection_info * head, struct connection_info * conn) {
    struct connection_info *prev, *p, *next;

	closesocket(conn->lfd);
	closesocket(conn->rfd);
	
	p=head;
	prev=NULL;
	while (p) {
        next=p->next;
		if (p=conn) {
			if (prev) {
				prev->next = p->next;
			} else {
				head = p->next;
			}
            free(p);
            return head;
		}
		prev=p;
        p=next;
	}
	return head;
}

int do_connection_loop( SOCKET fd, int remote_addr, int remote_port ) {
	fd_set fdreads, fdwrites;
    struct connection_info *head, *p, *next;
	int maxfd;
	int maxbytes;
	int i;

	head = NULL;

	while (1==1) {
		FD_ZERO(&fdreads);
		FD_ZERO(&fdwrites);
		FD_SET(fd,&fdreads);
		maxfd = fd;
		p=head;
		while(p) {
			if (p->lfd > maxfd) maxfd=p->lfd;
			if (p->rfd > maxfd) maxfd=p->rfd;
			if (p->lbufw < BUFSIZE-1) FD_SET(p->lfd,&fdreads);
			if (p->rbufw < BUFSIZE-1) FD_SET(p->rfd,&fdreads);
			if (p->lbufw > 0) FD_SET(p->rfd,&fdwrites);
			if (p->rbufw > 0) FD_SET(p->lfd,&fdwrites);
			p=p->next;
		}

		maxfd++;
		if ((i=select(maxfd, &fdreads, &fdwrites, NULL, NULL))==-1) {
			if (errno!=0) {
				perror("select");
				return -1;
			}
		}

		if (i==0) {
			printf("timeout\n");
			continue;
		}

		if (FD_ISSET(fd, &fdreads)) {
			p = get_new_connection(fd, remote_addr, remote_port);
			if (p) {
				p->next = head;
				head = p;
			}
		}
		
        next = head;
        while (next) {
            p = next;
            next = p->next;
			if (FD_ISSET(p->lfd,&fdreads)) {
				maxbytes = BUFSIZE-1 - p->lbufw;
				if (maxbytes >0) {
					i = recv(p->lfd,&p->lbuf[p->lbufw],maxbytes,0);
					if (i < 1) {
						head =destroy_connection(head,p);
                        continue;
					}
					p->lbufw += i;
				}
			}
			if (FD_ISSET(p->rfd,&fdwrites)) {
				maxbytes = p->lbufw - p->lbufr;
				if (maxbytes >0) {
					i = send(p->rfd,&p->lbuf[p->lbufr],maxbytes,0);
					if (i < maxbytes) {
						head =destroy_connection(head,p);
                        continue;
					}
					p->lbufr += i;
					if (p->lbufr == p->lbufw) {
						p->lbufr = p->lbufw = 0;
					}
				}
			}
			if (FD_ISSET(p->rfd,&fdreads)) {
				maxbytes = BUFSIZE-1 - p->rbufw;
				if (maxbytes >0) {
					i = recv(p->rfd,&p->rbuf[p->rbufw],maxbytes,0);
					if (i < 1) {
						head =destroy_connection(head,p);
                        continue;
                    }
                    p->rbufw += i;
				}
			}
			if (FD_ISSET(p->lfd,&fdwrites)) {
				maxbytes = p->rbufw - p->rbufr;
				if (maxbytes >0) {
					i = send(p->lfd,&p->rbuf[p->rbufr],maxbytes,0);
					if (i < maxbytes) {
						head =destroy_connection(head,p);
                        continue;
					}
					p->rbufr += i;
					if (p->rbufr == p->rbufw) {
						p->rbufr = p->rbufw = 0;
					}
				}
			}
        }
	}
	/* NOT REACHED */
	return 0;
}

int main(int argc, char **argv) {
	SOCKET fd;
	int local_port;
	int remote_port;
	char *remote_host;
	int remote_addr;
	char *local_host;
	int local_addr;
	struct hostent *he;

#ifdef __LCC__
	WSADATA info;
	
	if (WSAStartup(MAKEWORD(1,1), &info) != 0) {
		MessageBox(NULL, "Cannot initialize WinSock!", "WSAStartup", MB_OK);
		return -1;
	}
#endif

	if (argc < 4) {
		printf("Usage: %s <local_addr> <local_port> <remote_host> <remote_port>\n",argv[0]);
		return -1;
	}

	local_host = argv[1];
	local_port = atoi(argv[2]);
	remote_host = argv[3];
	remote_port = atoi(argv[4]);

	he = gethostbyname(local_host);
	if (he == NULL) {
		printf("local Host %s not found\n",remote_host);
		return -1;
	}
	memcpy(&local_addr,he->h_addr_list[0],4);

	he = gethostbyname(remote_host);
	if (he == NULL) {
		printf("Host %s not found\n",remote_host);
		return -1;
	}
	memcpy(&remote_addr,he->h_addr_list[0],4);

	fd = establish_port(local_addr,local_port);
	if (fd == -1) {
		return -1;
	}

	do_connection_loop(fd,remote_addr,remote_port);

	/* NOT REACHED */
	return 0;
}
