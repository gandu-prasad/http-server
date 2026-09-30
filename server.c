
#include<sys/socket.h>
#include<stdio.h>
#include<netinet/in.h>
#include<string.h>
#include<unistd.h>
#include<stdbool.h>

const char* CRLF = "\r\n";
const char* SP = " ";

typedef struct {
	char* methiod;
	char* uri;
	char* version;
} http_req_line;

typedef enum {
	HTTP_RES_OK;
	HTTP_RES_INTERNAL_SERVER_ERR;
} http_result;

typedef struct {
	const char* start;
	const char* end;
} string_view;

typedef struct {
	string_view* splits;
	size_t count;
	size_t capacity;
} string_splits;

static string_splits split_string(const char* str, size_t len, char split_by) {
	string_splits result;
	const char* start = str;
	size_t result_i = 0;

	result.capacity = 8;
	result.splits = calloc(sizeof(string_view),result.capacity);
	result.count = 0;

	for(size_t i = 0; i < len; ++i) {
		if(str[i] == split_by) {
			result.splits[result_i].start = start;
			result.splits[result_i].end = &str[i];
			result.count += 1;
			start = &str[i];
			
		}
	}
