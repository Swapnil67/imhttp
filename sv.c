#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "sv.h"

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

bool sv_starts_with(String_View sv, String_View prefix) {
    if(sv.count >= prefix.count) {
	const String_View temp = {
	    .data = sv.data,
	    .count = prefix.count
	};
	return sv_eq(prefix, temp);
    }
    return false;
}

bool sv_ends_with(String_View sv, String_View suffix) {
    if(sv.count >= suffix.count) {
	const String_View temp = {
	    .data = sv.data + sv.count - suffix.count,
	    .count = suffix.count
	};
	return sv_eq(suffix, temp);
    }
    return false;
}
