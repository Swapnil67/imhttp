
#ifndef IMHTTP_H_
#define IMHTTP_H_

#include<assert.h>

#include "./sv.h"


typedef void* ImHTTP_Socket;

// function pointers
typedef ssize_t (*ImHTTP_Write)(ImHTTP_Socket socket, const void *buf, size_t count);
typedef ssize_t (*ImHTTP_Read)(ImHTTP_Socket socket, void *buf, size_t count);

typedef enum {
    IMHTTP_GET,
    IMHTTP_POST,
} ImHTTP_Method;

#define IMHTTP_ROLLIN_BUFFER_CAPACITY (8 * 1024)
#define IMHTTP_USER_BUFFER_CAPACITY IMHTTP_ROLLIN_BUFFER_CAPACITY


static_assert(IMHTTP_USER_BUFFER_CAPACITY >= IMHTTP_ROLLIN_BUFFER_CAPACITY,
"The user buffer should be at least as big as the rolling buffer"
"because sometimes you may wanna put the whole rollin content into"
"the user buffer.");

typedef struct {
    ImHTTP_Socket socket;
    ImHTTP_Write write;
    ImHTTP_Read read;

    char rollin_buffer[IMHTTP_ROLLIN_BUFFER_CAPACITY];
    size_t rollin_buffer_size;

    char user_buffer[IMHTTP_USER_BUFFER_CAPACITY];
    size_t user_buffer_size;
    
    int content_length;
} ImHTTP;

void imhttp_req_begin(ImHTTP *imhttp, ImHTTP_Method method, const char *resource);
void imhttp_req_header(ImHTTP *imhttp, const char *header_name, const char *header_value);
void imhttp_req_headers_end(ImHTTP *imhttp);
void imhttp_req_body_chunk(ImHTTP *imhttp, const char *chunk_cstr);
void imhttp_req_body_chunk_sized(ImHTTP *imhttp, const char *chunk, size_t chunk_size);
void imhttp_req_end(ImHTTP *imhttp);

// Response handlers
void imhttp_res_begin(ImHTTP *imhttp);
uint64_t imhttp_res_status_code(ImHTTP *imhttp);
bool imhttp_res_next_header(ImHTTP *imhttp, String_View *name, String_View *value);
bool imhttp_res_next_body_chunk(ImHTTP *imhttp, String_View *chunk);
void imhttp_res_end(ImHTTP *imhttp);

#endif // IMHTTP_H_


#ifdef IMHTTP_IMPLEMENTATION


static const char* imhttp_method_as_cstr(ImHTTP_Method method) {
    switch(method) {
    case IMHTTP_GET: return "GET";
    case IMHTTP_POST: return "POST";
default:
    assert(0 && "imhttp_as_method_as_cstr: unreachable");
    }
}


static void imhttp_write_cstr(ImHTTP *imhttp, const char* cstr) {
    size_t cstr_size = strlen(cstr);
    imhttp->write(imhttp->socket, cstr, cstr_size);
}

void imhttp_req_begin(ImHTTP *imhttp, ImHTTP_Method method, const char *resource) {
    imhttp_write_cstr(imhttp, imhttp_method_as_cstr(method));
    imhttp_write_cstr(imhttp, " ");
    imhttp_write_cstr(imhttp, resource);
    imhttp_write_cstr(imhttp, " HTTP/1.0\r\n");
}

void imhttp_req_header(ImHTTP *imhttp, const char *header_name, const char *header_value) {
    imhttp_write_cstr(imhttp, header_name);
    imhttp_write_cstr(imhttp, ": ");
    imhttp_write_cstr(imhttp, header_value);
    imhttp_write_cstr(imhttp, "\r\n");
}

void imhttp_req_headers_end(ImHTTP *imhttp) {
    imhttp_write_cstr(imhttp, "\r\n");
}

void imhttp_req_body_chunk(ImHTTP *imhttp, const char *chunk_cstr) {
    imhttp_write_cstr(imhttp, chunk_cstr);
}

void imhttp_req_body_chunk_sized(ImHTTP *imhttp, const char *chunk, size_t chunk_size) {
    imhttp->write(imhttp->socket, chunk, chunk_size);
}

void imhttp_req_end(ImHTTP *imhttp) {
    (void) imhttp;
}

// * Response Handling Code

static void imhttp_shift_rollin_buffer(ImHTTP *imhttp, const char *end) {
    assert(end >= imhttp->rollin_buffer);
    size_t n = end - imhttp->rollin_buffer;
    assert(n <= imhttp->rollin_buffer_size);
    memmove(imhttp->rollin_buffer, end, imhttp->rollin_buffer_size);
}

static void imhttp_top_rollin_buffer(ImHTTP *imhttp) {
    if(imhttp->rollin_buffer_size < IMHTTP_ROLLIN_BUFFER_CAPACITY) {
	size_t n = imhttp->read(
	               imhttp->socket,
		       imhttp->rollin_buffer + imhttp->rollin_buffer_size,		     
		       IMHTTP_ROLLIN_BUFFER_CAPACITY - imhttp->rollin_buffer_size);
		       
	assert(n > 0);	       
	imhttp->rollin_buffer_size += n;
    }
}

static String_View imhttp_rollin_buffer_as_sv(ImHTTP *imhttp) {
    return (String_View) {
	.data = imhttp->rollin_buffer,
	.count = imhttp->rollin_buffer_size,
    };
}

void imhttp_res_begin(ImHTTP *imhttp) {
    // * Reset the content_length
    imhttp->content_length = -1;
    
}

uint64_t imhttp_res_status_code(ImHTTP *imhttp) {
    imhttp_top_rollin_buffer(imhttp);
    String_View rollin = imhttp_rollin_buffer_as_sv(imhttp);

    String_View status_line = sv_chop_by_delim(&rollin, '\n');
    // SV_PRINT(status_line);    
    assert(
	 sv_ends_with(status_line, cstr_to_sv("\r")) &&
	 "The rolling buffer is so small that it could not fit the whole status line."    
	 "Or maybe the status line was not fully read after the imhttp_top_rollin_buffer()"
	 "above");
    
    // * TODO: HTTP version is skipped in imhttp_res_status_code
    sv_chop_by_delim(&status_line, ' ');
    String_View code_sv = sv_chop_by_delim(&status_line, ' ');
    // SV_PRINT(code_sv);
    imhttp_shift_rollin_buffer(imhttp, rollin.data);
    return sv_to_u64(code_sv);
}

bool imhttp_res_next_header(ImHTTP *imhttp, String_View *name, String_View *value) {
    imhttp_top_rollin_buffer(imhttp);    
    String_View rollin = imhttp_rollin_buffer_as_sv(imhttp);
    String_View header_line = sv_chop_by_delim(&rollin, '\n');

    assert(
	 sv_ends_with(header_line, cstr_to_sv("\r")) &&
	 "The rolling buffer is so small that it could not fit the whole status line."    
	 "Or maybe the header line was not fully read after the imhttp_top_rollin_buffer()"
	 "above");


    // * Transfer the ownership of header_line from rollin_buffer to user_buffer
    {	     
        // * Copy the rollin_buffer to user_buffer
	memcpy(imhttp->user_buffer, header_line.data, header_line.count);
	header_line.data = imhttp->user_buffer;
    }

    imhttp_shift_rollin_buffer(imhttp, rollin.data);
    

    // * Check if we got \r\n
    if(!sv_eq(header_line, cstr_to_sv("\r"))) {
	*name = sv_chop_by_delim(&header_line, ':');
	sv_trim(&header_line);
	*value = header_line;
	
	if(sv_eq(*name, cstr_to_sv("Content-Length"))) {
	    // TODO content_length overflow
	    imhttp->content_length = sv_to_u64(*value);
	}
		
	return true;
    }

    return false;
    
}

bool imhttp_res_next_body_chunk(ImHTTP *imhttp, String_View *chunk) {

    // * TODO: ImHTTP can't handle the responses that do not set Content-Length    
    // printf("Content Length: %d\n", imhttp->content_length);
    assert(imhttp->content_length >= 0);

    if(imhttp->content_length > 0) {
	imhttp_top_rollin_buffer(imhttp);
	String_View rollin = imhttp_rollin_buffer_as_sv(imhttp);

	// * TODO: ImHTTP does not handle the situation when the server responded with more data than it claimed with Content-Length Header	
	assert(rollin.count <= (size_t) imhttp->content_length);

	assert(rollin.count <= IMHTTP_USER_BUFFER_CAPACITY);
	memcpy(imhttp->user_buffer, rollin.data, rollin.count);

	if(chunk) {
	    chunk->data = imhttp->user_buffer;
	    chunk->count = rollin.count;
	}

	imhttp_shift_rollin_buffer(imhttp, rollin.data);
	return true;
    }
   
    return false;
}

void imhttp_res_end(ImHTTP *imhttp) {
    (void) imhttp;
}


#endif // IMHTTP_IMPLEMENTATION
