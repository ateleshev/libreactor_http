#ifndef REACTOR_HTTP_SERVER_H_INCLUDED
#define REACTOR_HTTP_SERVER_H_INCLUDED

enum reactor_http_server_event
{
  REACTOR_HTTP_SERVER_ERROR,
  REACTOR_HTTP_SERVER_ACCEPT,
  REACTOR_HTTP_SERVER_REQUEST,
  REACTOR_HTTP_SERVER_CLOSE
};

enum reactor_http_server_state
{
  REACTOR_HTTP_SERVER_CLOSED,
  REACTOR_HTTP_SERVER_LISTENING,
  REACTOR_HTTP_SERVER_CLOSING
};

typedef struct reactor_http_server reactor_http_server;
struct reactor_http_server
{
  int                    state;
  reactor_user           user;
  reactor_tcp_server     tcp_server;
  reactor_timer          date_timer;
  char                   date[32];
  char                  *name;
};

typedef struct reactor_http_server_session reactor_http_server_session;
struct reactor_http_server_session
{
  reactor_stream         stream;
  reactor_http_request   request;
  reactor_http_parser    parser;
  reactor_http_server   *server;
};

void reactor_http_server_init(reactor_http_server *, reactor_user_call *, void *);
int  reactor_http_server_open(reactor_http_server *, char *, char *);
void reactor_http_server_name(reactor_http_server *, char *);
void reactor_http_server_error(reactor_http_server *);
void reactor_http_server_close(reactor_http_server *);

void reactor_http_server_tcp_event(void *, int, void *);

void reactor_http_server_date_event(void *, int, void *);
void reactor_http_server_date_update(reactor_http_server *);

void reactor_http_server_session_init(reactor_http_server_session *, reactor_http_server *);
int  reactor_http_server_session_open(reactor_http_server_session *, int);
void reactor_http_server_session_close(reactor_http_server_session *);
void reactor_http_server_session_stream_event(void *, int, void *);
void reactor_http_server_session_parser_event(void *, int, void *);
void reactor_http_server_session_respond(reactor_http_server_session *, unsigned, char *, char *, size_t);

#endif /* REACTOR_HTTP_SERVER_H_INCLUDED */
