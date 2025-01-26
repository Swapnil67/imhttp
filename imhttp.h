
#ifndef IMHTTP_H_
#define IMHTTP_H_

// String View

typedef struct {
    size_t count;
    char *data;
} String_View;

#define SV_Fmt "%.*s"
#define SV_Arg(sv) (int)sv.count, sv.data
#define SV_PRINT(sv) fprintf(stdout, "%.*s\n", (int) sv.count, sv.data);

String_View cstr_to_sv(char* cstr);
String_View sv_chop_by_delim(String_View *sv, char delim);
void sv_trim_left(String_View *sv);
void sv_trim_right(String_View *sv);
void sv_trim(String_View *sv);
int sv_eq(String_View a, String_View b);
uint64_t sv_to_u64(String_View a);


typedef void* ImHTTP_Socket;
// function pointers
typedef ssize_t (*ImHTTP_Write)(ImHTTP_Socket socket, const void *buf, size_t count);
typedef ssize_t (*ImHTTP_Read)(ImHTTP_Socket socket, void *buf, size_t count);

typedef enum {
    IMHTTP_GET,
    IMHTTP_POST,
} ImHTTP_Method;

#define IMHTTP_RES_META_CAPACITY (8 * 1024)
#define IMHTTP_RES_BODY_CHUNK_CAPACITY IMHTTP_RES_META_CAPACITY

static_assert(IMHTTP_RES_META_CAPACITY <= IMHTTP_RES_BODY_CHUNK_CAPACITY,
"If we overshoot and read a part of the body into meta data "
"buffer, we want that \"tail\" to actually fit into the"
"res_body_chunk");

typedef struct {
    ImHTTP_Socket socket;
    ImHTTP_Write write;
    ImHTTP_Read read;

    char res_meta[IMHTTP_RES_META_CAPACITY];
    size_t res_meta_size;

    bool first_chunk;
    String_View meta_cursor;

    char res_body_chunk[IMHTTP_RES_BODY_CHUNK_CAPACITY];
    size_t res_body_chunk_size;

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

String_View cstr_to_sv(char* cstr) {
    return (String_View) {
	.count = strlen(cstr),
	.data = cstr
    };
}

String_View sv_chop_by_delim(String_View *sv, char delim) {
    size_t i = 0;
    while(i < sv->count && sv->data[i] != delim) {
	i += 1;
    }

    String_View result = {
	.count = i,
	.data = sv->data,
    };

    if(i < sv->count) {
	sv->count -= i + 1;
	sv->data += i + 1;
    }
    else {
	sv->count -= i;
	sv->data += i;
    }

    return result;
}

void sv_trim_left(String_View *sv) {
    size_t i = 0;
    while(i < sv->count && isspace(sv->data[i])) i++;

    sv->count -= i;
    sv->data += i;
}

void sv_trim_right(String_View *sv) {
    size_t i = 0;
    while(i < sv->count && isspace(sv->data[sv->count - 1 - i])) i++;
    sv->count -= i;
}

void sv_trim(String_View *sv) {
    sv_trim_left(sv);
    sv_trim_right(sv);
}

int sv_eq(String_View a, String_View b) {
    if(a.count != b.count) return 0;
    return memcmp(a.data, b.data, a.count) == 0;
}

uint64_t sv_to_u64(String_View sv) {
    uint64_t result = 0;
    for(size_t i = 0; (i < sv.count && isdigit(sv.data[i])); ++i) {
	result = result * 10 + sv.data[i] - '0';
    }
    return result;
}

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

static void imhttp_res_springback_meta(ImHTTP *imhttp) {

    // String_View of Response (Headers + Body)
    String_View sv = {
	.count = imhttp->res_meta_size,
	.data = imhttp->res_meta,
    };

    // * Separate body & headers from response
    while(sv.count > 0) {
	// * Single header lines
	String_View line = sv_chop_by_delim(&sv, '\n');
	if(sv_eq(line, cstr_to_sv("\r"))) {
	    // sv.data has response body [First Chunk of body]
	    memcpy(imhttp->res_body_chunk, sv.data, sv.count);
	    imhttp->res_body_chunk_size = sv.count;
	    assert(imhttp->res_meta < sv.data);
	    // * Pointer Arithmetic
	    // [res_meta ......... sv.data]
	    imhttp->res_meta_size = sv.data - imhttp->res_meta;
	    imhttp->first_chunk = true;
	    return;
	}	
    }

    assert(0 && "IMHTTP_RES_META_CAPACITY is too small");
}


void imhttp_res_begin(ImHTTP *imhttp) {

    // * Reset the content_length
    imhttp->content_length = -1;
    
    // Save whole response to res_meta buffer
    ssize_t n = imhttp->read(imhttp->socket, imhttp->res_meta, IMHTTP_RES_META_CAPACITY);
    printf("Response bytes: %ld\n", n);
    
    // TODO: imhttp_res_begin does not handle read errors 
    assert(n > 0);
    imhttp->res_meta_size = n;
    imhttp_res_springback_meta(imhttp);

    
    imhttp->meta_cursor = (String_View) {
	.count = imhttp->res_meta_size,
	.data = imhttp->res_meta 
    };
}

uint64_t imhttp_res_status_code(ImHTTP *imhttp) {
    String_View status_line = sv_chop_by_delim(&imhttp->meta_cursor, '\n');
    // * TODO: HTTP version is skipped in imhttp_res_status_code
    sv_chop_by_delim(&status_line, ' ');
    String_View code_sv = sv_chop_by_delim(&status_line, ' ');
    return sv_to_u64(code_sv);
}

bool imhttp_res_next_header(ImHTTP *imhttp, String_View *name, String_View *value) {
    String_View line = sv_chop_by_delim(&imhttp->meta_cursor, '\n');

    // * Check if we got \r\n
    if(!sv_eq(line, cstr_to_sv("\r"))) {
	*name = sv_chop_by_delim(&line, ':');
	sv_trim(&line);
	*value = line;
	
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
    assert(imhttp->content_length >= 0);

    if(imhttp->content_length > 0) {
	if(imhttp->first_chunk) {
	    // * Already Read the first chunk while reading the headers
	    imhttp->first_chunk = false;
	}
	else {
	    // * Read the chunk
	    ssize_t n = imhttp->read(imhttp->socket, imhttp->res_body_chunk, IMHTTP_RES_BODY_CHUNK_CAPACITY);
	    assert(n > 0);
	    imhttp->res_body_chunk_size = n;
	}

	if(chunk) {
	    *chunk = (String_View) {
		.count = imhttp->res_body_chunk_size,
		.data = imhttp->res_body_chunk
	    };
	}

	// * TODO: ImHTTP does not handle the situation when the server responded with more data than it claimed with Content-Length Header
	assert((int) imhttp->res_body_chunk_size <= imhttp->content_length);
	imhttp->content_length -= imhttp->res_body_chunk_size;
	return true;
    }
    return false;
}

void imhttp_res_end(ImHTTP *imhttp) {
    (void) imhttp;
}


#endif // IMHTTP_IMPLEMENTATION
