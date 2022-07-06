/**************************************************************************
 * simpletun.c                                                            *
 *                                                                        *
 * A simplistic, simple-minded, naive tunnelling program using tun/tap    *
 * interfaces and TCP. DO NOT USE THIS PROGRAM FOR SERIOUS PURPOSES.      *
 *                                                                        *
 * You have been warned.                                                  *
 *                                                                        *
 * (C) 2010 Davide Brini.                                                 *
 *                                                                        *
 * DISCLAIMER AND WARNING: this is all work in progress. The code is      *
 * ugly, the algorithms are naive, error checking and input validation    *
 * are very basic, and of course there can be bugs. If that's not enough, *
 * the program has not been thoroughly tested, so it might even fail at   *
 * the few simple things it should be supposed to do right.               *
 * Needless to say, I take no responsibility whatsoever for what the      *
 * program might do. The program has been written mostly for learning     *
 * purposes, and can be used in the hope that is useful, but everything   *
 * is to be taken "as is" and without any kind of warranty, implicit or   *
 * explicit. See the file LICENSE for further details.                    *
 *************************************************************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h> 
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>

/* buffer for reading from tun/tap interface, must be >= 1500 */
#define BUFSIZE 2000   
#define CLIENT 0
#define SERVER 1
#define PORT 55555
int g=0;
int debug;
char *progname;

/**************************************************************************
 * tun_alloc: allocates or reconnects to a tun/tap device. The caller     *
 *            must reserve enough space in *dev.                          *
 **************************************************************************/
int edata(char *buff,int size){
	for(int i=0;i<size;i++)
		buff[i]++;
	return size;
}

int udata(char *buff,int size){
	for(int i=0;i<size;i++)
		buff[i]--;
	return size;
}



int tun_alloc(char *dev, int flags) {

	struct ifreq ifr;
	int fd, err;
	char *clonedev = "/dev/net/tun";
	if( (fd = open("/dev/tun" , O_RDWR)) < 0)
		if( (fd = open(clonedev , O_RDWR)) < 0 ) {
			perror("Opening /dev/net/tun");
			return fd;
		}

	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = flags;

	if (*dev) {
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	if( (err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
		perror("ioctl(TUNSETIFF)");
		close(fd);
		return err;
	}

	strcpy(dev, ifr.ifr_name);
	char cmd[60];
	sprintf(cmd,"ifconfig %s up",dev);
	system(cmd);
	return fd;
}

/**************************************************************************
 * cread: read routine that checks for errors and exits if an error is    *
 *        returned.                                                       *
 **************************************************************************/
int cread(int fd, char *buf, int n){

	int nread;

	if((nread=read(fd, buf, n)) < 0){
		perror("Reading data");
		return -1;
	}
	return nread;
}

/**************************************************************************
 * cwrite: write routine that checks for errors and exits if an error is  *
 *         returned.                                                      *
 **************************************************************************/
int cwrite(int fd, char *buf, int n){

	int nwrite;

	if((nwrite=write(fd, buf, n)) < 0){
		perror("Writing data");
		return -1;

	}
	return nwrite;
}

/**************************************************************************
 * read_n: ensures we read exactly n bytes, and puts them into "buf".     *
 *         (unless EOF, of course)                                        *
 **************************************************************************/
int read_n(int fd, char *buf, int n) {

	int nread, left = n;
	n=0;
	while(left > 0) {
		if ((nread = cread(fd, buf, left)) == 0){
			return 0 ;      
		}else {
			left -= nread;
			n+= nread;
			buf += nread;
		}
	}
	return n;  
}

/**************************************************************************
 * do_debug: prints debugging stuff (doh!)                                *
 **************************************************************************/
/*void do_debug(char *msg, ...){

  va_list argp;

  if(debug) {
  va_start(argp, msg);
  vfprintf(stderr, msg, argp);
  va_end(argp);
  }
  }
  */
/**************************************************************************
 * my_err: prints custom error messages on stderr.                        *
 **************************************************************************/
void my_err(char *msg, ...) {

	va_list argp;

	va_start(argp, msg);
	vfprintf(stderr, msg, argp);
	va_end(argp);
}

/**************************************************************************
 * usage: prints usage and exits.                                         *
 **************************************************************************/
void usage(void) {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "%s -i <ifacename> [-s|-c <serverIP>] [-p <port>] [-u|-a] [-d]\n", progname);
	fprintf(stderr, "%s -h\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "-i <ifacename>: Name of interface to use (mandatory)\n");
	fprintf(stderr, "-s|-c <serverIP>: run in server mode (-s), or specify server address (-c <serverIP>) (mandatory)\n");
	fprintf(stderr, "-p <port>: port to listen on (if run in server mode) or to connect to (in client mode), default 55555\n");
	fprintf(stderr, "-u|-a: use TUN (-u, default) or TAP (-a)\n");
	fprintf(stderr, "-d: outputs debug information while running\n");
	fprintf(stderr, "-h: prints this help text\n");
	exit(1);
}

int main(int argc, char *argv[]) {
	int pth=100;
	int net[100];
	memset(&net,0,sizeof net);
	int tap_fd, option;
	int flags = IFF_TUN;
	char if_name[IFNAMSIZ] = "";
	int maxfd;
	uint16_t nread, nwrite, plength;
	char buffer[BUFSIZE];
	struct sockaddr_in local;
	char remote_ip[16] = "";            /* dotted quad IP string */
	unsigned short int port = PORT;
	int sock_fd,  optval = 1;
	socklen_t remotelen;
	int cliserv = -1;    /* must be specified on cmd line */
	unsigned long int tap2net = 0, net2tap = 0;

	progname = argv[0];

	/* Check command line options */
	while((option = getopt(argc, argv, "i:sc:p:uahd")) > 0) {
		switch(option) {
			case 'd':
				debug = 1;
				break;
			case 'h':
				usage();
				break;
			case 'i':
				strncpy(if_name,optarg, IFNAMSIZ-1);
				break;
			case 's':
				cliserv = SERVER;
				break;
			case 'c':
				cliserv = CLIENT;
				strncpy(remote_ip,optarg,15);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'u':
				flags = IFF_TUN;
				break;
			case 'a':
				flags = IFF_TAP;
				break;
			default:
				my_err("Unknown option %c\n", option);
				usage();
		}
	}

	argv += optind;
	argc -= optind;

	if(argc > 0) {
		my_err("Too many options!\n");
		usage();
	}

	if(*if_name == '\0') {
		my_err("Must specify interface name!\n");
		usage();
	} else if(cliserv < 0) {
		my_err("Must specify client or server mode!\n");
		usage();
	} else if((cliserv == CLIENT)&&(*remote_ip == '\0')) {
		my_err("Must specify server address!\n");
		usage();
	}

	/* initialize tun/tap interface */
	if ( (tap_fd = tun_alloc(if_name, flags | IFF_NO_PI)) < 0 ) {
		my_err("Error connecting to tun/tap interface %s!\n", if_name);
		exit(1);
	}

	//do_debug("Successfully connected to interface %s\n", if_name);

	struct sockaddr_in remote[pth];

	memset(&remote, 0, sizeof(remote));
	int len=sizeof(remote[0]);

	if(cliserv == CLIENT) {
		for(int i=0;i<pth;i++){
			/* Client, try to connect to server */

			/* assign the destination address */
			remote[i].sin_family = AF_INET;
			remote[i].sin_addr.s_addr = inet_addr(remote_ip);
			remote[i].sin_port = htons(port);

			/* connection request */
			if ( (net[i]= socket(AF_INET, SOCK_STREAM, 0)) < 0) {
				perror("socket()");
				exit(1);
			}

			if (connect(net[i], (struct sockaddr*) &remote[i], len) < 0) {
				perror("connect()");
				exit(1);
			}
		}

		//net_fd = sock_fd;
		///do_debug("CLIENT: Connected to server %s\n", inet_ntoa(remote.sin_addr));

	}
	else
	{
		if ( (sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			perror("socket()");
			exit(1);
		}

		/* Server, wait for connections */

		/* avoid EADDRINUSE error on bind() */
		if(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) < 0) {
			perror("setsockopt()");
			exit(1);
		}

		memset(&local, 0, sizeof(local));
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		local.sin_port = htons(port);
		if (bind(sock_fd, (struct sockaddr*) &local, sizeof(local)) < 0) {
			perror("bind()");
			exit(1);
		}

		if (listen(sock_fd, 5) < 0) {
			perror("listen()");
			exit(1);
		}
ser:
		/* wait for connection request */
		remotelen = sizeof(remote[0] );
		memset(&remote, 0, remotelen);
		fprintf(stderr,"等待客户端\n");
		for(int i=0;i<pth;i++){
			if ((net[i] = accept(sock_fd, (struct sockaddr*)&(remote[i]), &remotelen)) < 0) {
				perror("accept()");
				exit(1);
			}
			fprintf(stderr,"SERVER: Client connected from %s\n", inet_ntoa(remote[i].sin_addr));
		}

		//do_debug("SERVER: Client connected from %s\n", inet_ntoa(remote.sin_addr));
	}

	/* use select() to handle two descriptors at once */
	//maxfd = (tap_fd > net_fd)?tap_fd:net_fd;
	maxfd = tap_fd ;
	for(int i=0;i<pth;i++){
		if(net[i]>maxfd )
			maxfd=net[i];
	}
	int ret;
	fd_set rd_set,wd_set;
	maxfd=maxfd + 1;
	// struct timeval timeout;
	//memset(& timeout, 0 ,sizeof  timeout);
	//timeout. tv_usec=900000;
	fprintf(stderr,"连接完毕\n");
	int  nw=0;
	int nr=0;
	//int  g=0;
	tap2net=0;
	int r=0; 
	net2tap=0;
	while(1) {
		if(nw==pth)nw=0;
		if(nr==pth)nr=0;
		//	for(int i=0;i<pth;i++){
		FD_ZERO(&rd_set);
		//FD_ZERO(&wd_set);
		FD_SET(tap_fd, &rd_set);
		//		for(int i=0;i<pth;i++){
		FD_SET(net[nr], &rd_set);
		//	FD_SET(net[i], &wd_set);
		//		}
		//FD_SET(net[nw], &wd_set);
		ret = select(maxfd, &rd_set, 0, NULL,  NULL);
		/*if (ret==-1)
		  {
		  perror(" select()");
		  exit(0);
		  }*/
		if (ret < 0 && errno == EINTR){
			continue;
		}

		if (ret < 0) {
			perror("select()");
			exit(1);
		}
		//  fprintf(stderr,"%d\n",y++);
		if(FD_ISSET(tap_fd, &rd_set)){
			if(g)fprintf(stderr,"开始读取tap");
			FD_ZERO(&wd_set);
			FD_SET(net[nw], &wd_set);
			ret = select(maxfd, 0, &wd_set, NULL,  NULL);
			if (ret==-1)
			{
				perror(" select()");
				exit(1);
			}
			/*if (ret < 0 && errno == EINTR){
			  continue;
			  }*/
			if( ret >0)
				if (FD_ISSET(net[nw], &wd_set)) {
					/* data from tun/tap: just read it and write it to the network */

					nread = cread(tap_fd, buffer, BUFSIZE);
					if(g)fprintf(stderr,"\t\t\tread tap %d\n",nread);

					//tap2net++;
					//	printf("TAP2NET %lu: Read %d bytes from the tap interface\n", tap2net, nread);

					/* write length + packet */
					plength = htons(nread);
					nwrite = cwrite(net[nw], (char *)&  tap2net , sizeof(int ));
					tap2net++;
					nwrite = cwrite(net[nw], (char *)&plength, sizeof(plength));
					
					if( nwrite == -1)
						break;

					edata( buffer, nread);
					nwrite = cwrite(net[nw], buffer, nread);

					/*	ret = select(maxfd,0,  &wd_set, NULL,  NULL);
						nwrite =0;
						for(int i=0;i<pth;i++)
						if(FD_ISSET(net[i], &wd_set)) {
						nwrite = cwrite(net[i], (char *)&plength, sizeof(plength));
						if( nwrite == -1)
						break;

						edata( buffer, nread);
						nwrite = cwrite(net[i], buffer, nread);
					//if( nwrite == -1)
					break;
					}

*/
					if( nwrite == -1)
						break;
					nw++;
					if(g)fprintf(stderr,"\t\t\twrite net %d\n",nwrite);

					//do_debug("TAP2NET %lu: Written %d bytes to the network\n", tap2net, nwrite);
				}
		}
		//for(int i=0;i<pth;i++)
		if(FD_ISSET(net[nr], &rd_set)) {
			FD_ZERO(&wd_set);
			FD_SET(tap_fd, &wd_set);
			ret = select(maxfd, 0, &wd_set, NULL,  NULL);
			if (ret==-1){
				perror(" select()"); 
				exit(1);
			}
			if( ret >0)
				if (FD_ISSET(tap_fd , &wd_set)) {
					/*if( nr!=i){
					  fprintf(stderr,"%d=?%d\n",nr,i);
					  break;
					  }*/	       
					/* data from the network: read it, and write it to the tun/tap interface. 
					 * We need to read the length first, and then the packet */

					/* Read length */ 	
					nread = read_n(net[nr], (char *)&r, sizeof(int ));
					
					nread = read_n(net[nr], (char *)&plength, sizeof(plength));
					
					if(nread == 0) {
						/* ctrl-c at the other end */
						break;
					}

					//net2tap++;
					nwrite=ntohs(plength);
					/* read packet */
					nread = read_n(net[nr], buffer,nwrite );
					if(g)fprintf(stderr,"read net %d\n",nread);

					if( nread== -1)
						break;

					//do_debug("NET2TAP %lu: Read %d bytes from the network\n", net2tap, nread);

					/* now buffer[] contains a full packet or frame, write it into the tun/tap interface */ 
					udata( buffer, nread);
					if(r==-1)
						r==net2tap; 

					if(nwrite==nread )
						if(r==net2tap ){
					nwrite = cwrite(tap_fd, buffer, nread);
					net2tap++;
						}
					else{	
						r=-1; 
						fprintf(stderr,"丢包\n");
					}

					if( nwrite == -1)
						break;
					if(g)fprintf(stderr,"write tap %d\n",nwrite);
					nr++;
					//do_debug("NET2TAP %lu: Written %d bytes to the tap interface\n", net2tap, nwrite);
				}
		}
	}
	if(cliserv == CLIENT) {
		for(int i=0;i<pth;i++)
			close(net[i]);
	}
	else
	{
		for(int i=0;i<pth;i++){
			close(net[i]);
		}
		goto ser;
	}
	return(0);
	}
