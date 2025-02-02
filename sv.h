#ifndef SV_H_
#define SV_H_

#include<stdint.h>
#include<stdlib.h>
#include<stdbool.h>

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

bool sv_starts_with(String_View sv, String_View suffix);
bool sv_ends_with(String_View sv, String_View suffix);

#endif // * SV_H_