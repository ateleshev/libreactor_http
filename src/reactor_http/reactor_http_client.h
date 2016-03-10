#ifndef REACTOR_HTTP_CLIENT_H_INCLUDED
#define REACTOR_HTTP_CLIENT_H_INCLUDED

enum reactor_http_client_event
{
  REACTOR_HTTP_CLIENT_ERROR,
  REACTOR_HTTP_CLIENT_RESPONSE,
  REACTOR_HTTP_CLIENT_HEADER,
  REACTOR_HTTP_CLIENT_CHUNK,
  REACTOR_HTTP_CLIENT_CLOSE
};

enum reactor_http_client_state
{
  REACTOR_HTTP_CLIENT_CLOSED,
  REACTOR_HTTP_CLIENT_CONNECTING,
  REACTOR_HTTP_CLIENT_CONNECTED,
  REACTOR_HTTP_CLIENT_CLOSING
};

typedef struct reactor_http_client reactor_http_client;
struct reactor_http_client
{
  int                    state;
  reactor_user           user;
  char                  *uri;
  reactor_tcp_client     tcp_client;
  reactor_stream         stream;
  reactor_http_request   request;
  reactor_http_response  response;
  reactor_http_parser    parser;
};

void  reactor_http_client_init(reactor_http_client *, reactor_user_call *, void *);
int   reactor_http_client_open(reactor_http_client *, char *, char *, char *, size_t, int);
void  reactor_http_client_close(reactor_http_client *);
void  reactor_http_client_error(reactor_http_client *);
void  reactor_http_client_tcp_client_event(void *, int, void *);
void  reactor_http_client_stream_event(void *, int, void *);
void  reactor_http_client_parser_event(void *, int, void *);

#endif /* REACTOR_HTTP_CLIENT_H_INCLUDED */
