/*
 * Simple Port Proxy program
 *
 */

#undef DEBUG
#define BUFSIZE 32768

#ifdef __LCC__
#include <winsock.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#ifndef __LCC__
#define closesocket close
#define SOCKET	int
#define SOCKET_ERROR -1
#endif

#define DEBUG_MAIN  0x01
#define DEBUG_OUTER 0x02
#define DEBUG_INNER 0x04
#define DEBUG_SRCR  0x08
#define DEBUG_SRCW  0x10
#define DEBUG_DSTR  0x20
#define DEBUG_DSTW  0x40
#define DEBUG_THREAD 0x80
#define DEBUG_LEVEL (DEBUG_THREAD|DEBUG_MAIN)
#ifdef DEBUG
int Dprintf(int level, const char *fmt, ... )
{
        va_list args;
        int i;

        if (level & DEBUG_LEVEL) {
                va_start(args,fmt);
                i=vprintf(fmt,args);
                va_end(args);
                return i;
        }
        return 0;
}

void hdump( int level, char *p, int count) {
        if (level & DEBUG_LEVEL) {
                while (count--) {
                        if ((*p>=' ')&&(*p<='~')) {
                                printf("%c",*p++);
                        } else {
                                printf("\\x%02x",(*p++)&0xff);
                        }
                }
                printf("\n");
        }
}
#else
int Dprintf(int level, const char *fmt, ...) { return 0; }
void hdump(int level, char *p, int count) {}
#endif

int process_connection(SOCKET fd, int addr, int port) {
        SOCKET rfd;
        struct sockaddr_in sa;
        SOCKET maxfd;
        fd_set fdreads,fdwrites;

        char sbuf[BUFSIZE];
        char rbuf[BUFSIZE];
        int sbufw,sbufr;
        int rbufw,rbufr;

        rfd = socket(AF_INET,SOCK_STREAM,6);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = addr;
        if (connect(rfd,&sa,sizeof(sa)) == -1) {
                perror("connect");
                return -1;
        }

        maxfd = ((rfd>fd)?rfd:fd)+1;

        Dprintf(2,"rfd\t%i\n",rfd);
        Dprintf(2,"fd\t%i\n",fd);

        sbufw = sbufr = 0;
        rbufw = rbufr = 0;

        FD_ZERO(&fdreads);
        FD_ZERO(&fdwrites);

        while (1==1) {
                struct timeval tm;
                int i,j;
                int maxbytes;
                int checks;
                
                checks = 0;
                if (sbufw < BUFSIZE-1) {
                        FD_SET(fd,&fdreads);
                        checks |= DEBUG_SRCR;
                } else {
                        FD_CLR(fd,&fdreads);
                }
                if (rbufw < BUFSIZE-1) {
                        FD_SET(rfd,&fdreads);
                        checks |= DEBUG_DSTR;
                } else {
                        FD_CLR(rfd,&fdreads);
                }
                if (sbufw > 0) {
                        FD_SET(rfd,&fdwrites);
                        checks |= DEBUG_DSTW;
                } else {
                        FD_CLR(rfd,&fdwrites);
                }
                if (rbufw > 0) {
                        FD_SET(fd,&fdwrites);
                        checks |= DEBUG_SRCW;
                } else {
                        FD_CLR(fd,&fdwrites);
                }
                if (!checks) {
                        Dprintf(DEBUG_INNER,"Stall!\n");
                        return -1;
                }

                tm.tv_sec = 10;
                tm.tv_usec = 0;

                Dprintf(DEBUG_INNER,"select %02x S:%i,%i\tD:%i,%i\t",checks,sbufw,sbufr,rbufw,rbufr);
                if ((i = select(maxfd , &fdreads, &fdwrites, NULL, NULL)) == SOCKET_ERROR) {
                        perror("select");
                        closesocket(rfd);
                        return -1;
                }

                if (i == 0) {
                        Dprintf(DEBUG_INNER,"timeout\n");
                        continue;
                }
                Dprintf(DEBUG_INNER,"= %i\n",i);

                if (FD_ISSET(fd,&fdreads)) {
                        maxbytes = BUFSIZE-1 - sbufw;
                        if (maxbytes >0) {
                                i = recv(fd,&sbuf[sbufw],maxbytes,0);
                                Dprintf(DEBUG_SRCR,"recvS:%i\t",i);
                                hdump(DEBUG_SRCR,&sbuf[sbufw],i);
                                if (i < 1) {
                                        Dprintf(DEBUG_INNER,"fd read = %i\n",i);
                                        closesocket(rfd);
                                        return 0;
                                }
                                sbufw += i;
                        }
                }
                if (FD_ISSET(rfd,&fdwrites)) {
                        maxbytes = sbufw - sbufr;
                        if (maxbytes >0) {
                                j = send(rfd,&sbuf[sbufr],maxbytes,0);
                                Dprintf(DEBUG_DSTW,"sendD:%i\t",j);
                                hdump(DEBUG_DSTW,&sbuf[sbufr],j);
                                if (j < maxbytes) {
                                        Dprintf(DEBUG_INNER,"rfd write %i < %i\n",j,maxbytes);
                                        closesocket(rfd);
                                        return 0;
                                }
                                sbufr += j;
                                if (sbufr == sbufw) {
                                        sbufr = sbufw = 0;
                                }
                        }
                }
                if (FD_ISSET(rfd,&fdreads)) {
                        maxbytes = BUFSIZE-1 - sbufw;
                        if (maxbytes >0) {
                                i = recv(rfd,&rbuf[rbufw],maxbytes,0);
                                Dprintf(DEBUG_DSTR,"recvD:%i\t",i);
                                hdump(DEBUG_DSTR,&rbuf[rbufw],i);
                                if (i < 1) {
                                        Dprintf(DEBUG_INNER,"rfd read = %i\n",i);
                                        closesocket(rfd);
                                        return 0;
                                }
                                rbufw += i;
                        }
                }
                if (FD_ISSET(fd,&fdwrites)) {
                        maxbytes = rbufw - rbufr;
                        if (maxbytes >0) {
                                j = send(fd,&rbuf[rbufr],maxbytes,0);
                                Dprintf(DEBUG_SRCW,"sendS:%i\t",j);
                                hdump(DEBUG_SRCW,&rbuf[rbufr],j);
                                if (j < maxbytes) {
                                        Dprintf(DEBUG_INNER,"fd write %i < %i\n",j,maxbytes);
                                        closesocket(rfd);
                                        return 0;
                                }
                                rbufr += j;
                                if (rbufr == rbufw) {
                                        rbufr = rbufw = 0;
                                }
                        }
                }
        }
        /* NOT REACHED */
        return -1;
}

/*
 * Global so that the windows thread handler can see them without me
 * fussing arround with structs
 */
int portr;
int addrr;

#ifdef __LCC__
DWORD WINAPI conThread( LPVOID lpArg )
{
        SOCKET fd = (SOCKET)(lpArg);

        Dprintf(DEBUG_THREAD,"I am a thread, my socket is %i\n",fd);

        if (process_connection(fd,addrr,portr) == -1) {
                Dprintf(1,"error\n");
        }
        closesocket(fd);

        Dprintf(DEBUG_THREAD,"thread exiting\n");
        return TRUE;
}
#endif

int main( int argc, char **argv) {
        int portl;
        SOCKET sfd;
        SOCKET i;
        struct sockaddr_in sa;
        int salen;
        int j;
        struct hostent * he;
#ifdef __LCC__
        WSADATA info;
        HANDLE hThread;
#endif

        if (argc < 3) {
                printf("Usage: %s <port> <dest addr> <dest port>\n",argv[0]);
                return -1;
        }
        
#ifdef __LCC__
        if (WSAStartup(MAKEWORD(1,1), &info) != 0)
          MessageBox(NULL, "Cannot initialize WinSock!", "WSAStartup", MB_OK);
#endif

        portl = atoi(argv[1]);
        portr = atoi(argv[3]);
        he = gethostbyname(argv[2]);
        if (he == NULL) {
                printf("no hostname!\n");
                return -1;
        }
        memcpy(&addrr,he->h_addr_list[0],he->h_length);

        Dprintf(1,"portl   %i\n",portl);
        Dprintf(1,"addrr   %08x\n",addrr);
        Dprintf(1,"portr   %i\n",portr);

        sfd = socket(AF_INET,SOCK_STREAM,6);
        memset(&sa,0,sizeof(sa));
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
                Dprintf(1,"Loop Start\n");
                salen = sizeof(sa);
                if ((i = accept(sfd,&sa,&salen))== -1) {
                        perror("accept");
                        return -1;
                }
#ifdef __LCC__
                Dprintf(DEBUG_THREAD,"My Socket is %i\n",i);
#if 0
                beginthread( conThread, 0, (LPVOID)(i));
#endif
                conThread((LPVOID)(i));
#else
                if (!fork()) {
                        if (process_connection(i,addrr,portr) == -1) {
                                Dprintf(1,"error\n");
                        }
                closesocket(i);
                return 0;
		}
#endif

                closesocket(i);
        }
        return 0;
}

