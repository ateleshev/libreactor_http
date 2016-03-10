#ifndef REACTOR_HTTP_H_INCLUDED
#define REACTOR_HTTP_H_INCLUDED

#define REACTOR_HTTP_HEADER_MAX_FIELDS 32

typedef struct reactor_http_field reactor_http_field;
struct reactor_http_field
{
  char                  *key;
  char                  *value;
};

typedef struct reactor_http_request reactor_http_request;
struct reactor_http_request
{
  char                 *base;
  char                 *method;
  char                 *path;
  int                   minor_version;
  char                 *host;
  char                 *service;
  char                 *content;
  size_t                content_size;
  vector                fields;
};

typedef struct reactor_http_response reactor_http_response;
struct reactor_http_response
{
  int                    status;
  char                  *message;
  int                    minor_version;
  char                  *content;
  size_t                 content_size;
  vector                 fields;
};

int   reactor_http_split_url(char *, char **, char **, char **);

int   reactor_http_field_add_range(vector *, char *, size_t, char *, size_t);
char *reactor_http_field_lookup(vector *, char *);
void  reactor_http_field_offset(vector *, off_t);

void  reactor_http_request_init(reactor_http_request *);
void  reactor_http_request_clear(reactor_http_request *);
void  reactor_http_request_create(reactor_http_request *, char *, char *, char *, char *, char *, size_t);
void  reactor_http_request_add_header(reactor_http_request *, char *, char *);
void  reactor_http_request_send(reactor_http_request *, reactor_stream *);

void  reactor_http_response_init(reactor_http_response *);
void  reactor_http_response_create(reactor_http_response *, unsigned, char *, size_t);
void  reactor_http_response_add_header(reactor_http_response *, char *, char *);
void  reactor_http_response_send(reactor_http_response *, reactor_stream *);
void  reactor_http_response_clear(reactor_http_response *);

#endif /* REACTOR_HTTP_H_INCLUDED */
