#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <time.h>
#include <sys/socket.h>

#include <dynamic.h>
#include <clo.h>
#include <reactor_core.h>
#include <reactor_net.h>

#include "picohttpparser.h"
#include "reactor_http.h"
#include "reactor_http_parser.h"
#include "reactor_http_server.h"

void reactor_http_server_init(reactor_http_server *server, reactor_user_call *call, void *state)
{
  server->state = 0;
  *server = (reactor_http_server) {.state = REACTOR_HTTP_SERVER_CLOSED};
  reactor_user_init(&server->user, call, state);
  reactor_tcp_server_init(&server->tcp_server, reactor_http_server_tcp_event, server);
  reactor_timer_init(&server->date_timer, reactor_http_server_date_event, server);
}

int reactor_http_server_open(reactor_http_server *server, char *node, char *service)
{
  int e;

  reactor_http_server_date_update(server);
  e = reactor_timer_open(&server->date_timer, 1000000000, 1000000000);
  if (e == -1)
    return -1;

  e = reactor_tcp_server_open(&server->tcp_server, node ? node : "0.0.0.0", service ? service : "http");
  if (e == -1)
    return -1;

  server->state = REACTOR_HTTP_SERVER_LISTENING;
  return 0;
}

void reactor_http_server_name(reactor_http_server *server, char *name)
{
  server->name = name;
}

void reactor_http_server_error(reactor_http_server *server)
{
  if (server->state == REACTOR_HTTP_SERVER_LISTENING)
    reactor_user_dispatch(&server->user, REACTOR_HTTP_SERVER_ERROR, NULL);
}

void reactor_http_server_close(reactor_http_server *server)
{
  if (server->state == REACTOR_HTTP_SERVER_CLOSED)
    return;

  if (server->state != REACTOR_HTTP_SERVER_CLOSING)
    {
      server->state = REACTOR_HTTP_SERVER_CLOSING;
      reactor_tcp_server_close(&server->tcp_server);
      reactor_timer_close(&server->date_timer);
    }

  if (server->state != REACTOR_HTTP_SERVER_CLOSED &&
      server->tcp_server.state == REACTOR_TCP_SERVER_CLOSED &&
      server->date_timer.state == REACTOR_TIMER_CLOSED)
    {
      server->state = REACTOR_HTTP_SERVER_CLOSED;
      reactor_user_dispatch(&server->user, REACTOR_HTTP_SERVER_CLOSE, NULL);
    }
}

void reactor_http_server_tcp_event(void *state, int type, void *data)
{
  reactor_http_server *server;
  reactor_http_server_session *session;
  reactor_tcp_server_data *client;
  int e;

  server = state;
  switch (type)
    {
    case REACTOR_TCP_SERVER_ACCEPT:
      client = data;
      session = malloc(sizeof *session);
      if (!session)
        {
          reactor_http_server_error(server);
          break;
        }

      reactor_http_server_session_init(session, server);
      e = reactor_http_server_session_open(session, client->fd);
      if (e == -1)
        {
          (void) close(client->fd);
          free(session);
          break;
        }

      reactor_user_dispatch(&server->user, REACTOR_HTTP_SERVER_ACCEPT, session);
      break;
    case REACTOR_TCP_SERVER_CLOSE:
      reactor_http_server_close(server);
      break;
    case REACTOR_TCP_SERVER_ERROR:
      reactor_user_dispatch(&server->user, REACTOR_HTTP_SERVER_ERROR, data);
      break;
    }
}

void reactor_http_server_date_event(void *state, int type, void *data)
{
  reactor_http_server *server;

  (void) data;
  server = state;
  if (type == REACTOR_TIMER_TIMEOUT)
    reactor_http_server_date_update(server);
  else if (type == REACTOR_TIMER_CLOSE)
    reactor_http_server_close(server);
}

void reactor_http_server_date_update(reactor_http_server *server)
{
  static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  time_t t;
  struct tm tm;

  (void) time(&t);
  (void) gmtime_r(&t, &tm);
  (void) strftime(server->date, sizeof server->date, "---, %d --- %Y %H:%M:%S GMT", &tm);
  memcpy(server->date, days[tm.tm_wday], 3);
  memcpy(server->date + 8, months[tm.tm_mon], 3);
}

void reactor_http_server_session_init(reactor_http_server_session *session, reactor_http_server *server)
{
  *session = (reactor_http_server_session) {.server = server};
  reactor_stream_init(&session->stream, reactor_http_server_session_stream_event, session);
  reactor_http_parser_init(&session->parser, reactor_http_server_session_parser_event, session);
  reactor_http_request_init(&session->request);
}

int reactor_http_server_session_open(reactor_http_server_session *session, int fd)
{
  reactor_http_parser_open_request(&session->parser, &session->request, 0);
  return reactor_stream_open(&session->stream, fd);
}

void reactor_http_server_session_close(reactor_http_server_session *session)
{
  if (session->stream.state == REACTOR_STREAM_OPEN)
    {
      reactor_http_request_clear(&session->request);
      reactor_stream_close(&session->stream);
    }
}

int reactor_http_server_session_peer(reactor_http_server_session *session, struct sockaddr_in *sin, socklen_t *len)
{
  if (session->stream.state != REACTOR_STREAM_OPEN)
    return -1;

  *len = sizeof(*sin);
  return getpeername(reactor_desc_fd(&session->stream.desc), sin, len);
}

void reactor_http_server_session_stream_event(void *state, int type, void *data)
{
  reactor_http_server_session *session;

  session = state;
  (void) data;
  switch (type)
    {
    case REACTOR_STREAM_DATA:
      reactor_http_parser_data(&session->parser, data);
      break;
    case REACTOR_STREAM_ERROR:
      reactor_http_server_session_close(session);
      reactor_user_dispatch(&session->server->user, REACTOR_HTTP_SERVER_ERROR, NULL);
      break;
    case REACTOR_STREAM_END:
      reactor_http_server_session_close(session);
      break;
    case REACTOR_STREAM_CLOSE:
      free(session);
      break;
    }
}

void reactor_http_server_session_parser_event(void *state, int type, void *data)
{
  reactor_http_server_session *session;

  session = state;
  (void) data;
  switch (type)
    {
    case REACTOR_HTTP_PARSER_ERROR:
      reactor_user_dispatch(&session->server->user, REACTOR_HTTP_SERVER_ERROR, NULL);
      reactor_http_server_session_close(session);
      break;
    case REACTOR_HTTP_PARSER_DONE:
      reactor_user_dispatch(&session->server->user, REACTOR_HTTP_SERVER_REQUEST, session);
      break;
    default:
      break;
    }
}

void reactor_http_server_session_respond(reactor_http_server_session *session, unsigned status,
                                         char *content_type, char *content, size_t content_size)
{
  reactor_http_server_session_respond_fields(session, status, content_type, content, content_size, NULL, 0);
}

void reactor_http_server_session_respond_fields(reactor_http_server_session *session, unsigned status,
                                                char *content_type, char *content, size_t content_size,
                                                reactor_http_field *fields, size_t nfields)
{
  reactor_http_response response;
  size_t i;

  reactor_http_response_create(&response, status, content, content_size);
  if (session->server->name)
    reactor_http_response_add_header(&response, "Server", session->server->name);
  reactor_http_response_add_header(&response, "Date", session->server->date);
  if (content_type)
    reactor_http_response_add_header(&response, "Content-Type", content_type);
  for (i = 0; i < nfields; i ++)
    reactor_http_response_add_header(&response, fields[i].key, fields[i].value);
  reactor_http_response_send(&response, &session->stream);
  reactor_http_response_clear(&response);
}
