#define _POSIX_C_SOURCE 200112L

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<ctype.h>
#include<stdbool.h>
#include <inttypes.h>

#include<netdb.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<assert.h>

#define IMHTTP_IMPLEMENTATION
#include "./imhttp.h"

#define HOST "google.com"
#define PORT "80"

ssize_t imhttp_write(ImHTTP_Socket socket, const void *buf, size_t count) {
    // * Write Linux System Call
    return write((int) (int64_t)socket, buf, count);
}

ssize_t imhttp_read(ImHTTP_Socket socket, void *buf, size_t count) {
    // * Read Linux System Call
    // printf("Descriptor: %p\n", socket);
    // printf("Buffer: %p\n", buf);
    // printf("Count: %ld\n", count);
    return read((int) (int64_t)socket, buf, count);
}


int main() {
    // * Resolve the host (DNS Resolution)
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *addrs; // * Linked List
    if(getaddrinfo(HOST, PORT, &hints, &addrs) < 0) {
	fprintf(stderr, "Could not get address of `"HOST"`:%s\n", strerror(errno));
	exit(1);
    }

    // Loop over all resolved IPv4 addresses
    int sd = 0;
    for(struct addrinfo *addr = addrs; addr != NULL; addr = addr->ai_next) {
	sd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

	if(sd == -1) break;
	if(connect(sd, addr->ai_addr, addr->ai_addrlen) == 0) break;

	close(sd);
	sd = -1;
    }
    freeaddrinfo(addrs);

    if (sd == -1) {
	fprintf(stderr, "Could not connect to " HOST ":" PORT ": %s\n", strerror(errno));	
	exit(1);	
    }

    // * imhttp socket object
    static ImHTTP imhttp = {
		    .write = imhttp_write,
		    .read = imhttp_read,
    };
    imhttp.socket = (void*) (int64_t) sd;

    imhttp_req_begin(&imhttp, IMHTTP_GET, "/");
    // printf("Socket: %d\n", sd);
    // * Add some headers to request headers
    {
	imhttp_req_header(&imhttp, "Host", HOST);
	imhttp_req_header(&imhttp, "Foo", "Bar");
	imhttp_req_header(&imhttp, "Hello", "World");
	imhttp_req_headers_end(&imhttp);
	imhttp_req_body_chunk(&imhttp, "Hello World\n");
	imhttp_req_body_chunk(&imhttp, "Test, test, test\n");
    }
    imhttp_req_end(&imhttp);

    imhttp_res_begin(&imhttp);
    {
	uint64_t status_code = imhttp_res_status_code(&imhttp);
	printf("Status Code: %"PRIu64"\n", status_code);

	// * Read headers
	String_View name, value;
	while(imhttp_res_next_header(&imhttp, &name, &value)) {
	    printf("-----------------------------------------\n");	    
	    printf("Header Name: "SV_Fmt"\n", SV_Arg(name));
	    printf("Header Value: "SV_Fmt"\n", SV_Arg(value));
	}
	printf("-----------------------------------------\n");

	// * Read body chunks
	String_View chunk;
	while(imhttp_res_next_body_chunk(&imhttp, &chunk)) {
	    printf(SV_Fmt, SV_Arg(chunk));
	}

    }
    imhttp_res_end(&imhttp);

    
    close(sd);

    return 0;
}
