#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>

#include <dynamic.h>
#include <clo.h>
#include <reactor_core.h>
#include <reactor_net.h>

#include "picohttpparser.h"
#include "reactor_http.h"
#include "reactor_http_parser.h"
#include "reactor_http_client.h"

void reactor_http_client_init(reactor_http_client *client, reactor_user_call *call, void *state)
{
  *client = (reactor_http_client) {.state = REACTOR_HTTP_CLIENT_CLOSED};
  reactor_user_init(&client->user, call, state);
  reactor_http_request_init(&client->request);
  reactor_http_response_init(&client->response);
  reactor_stream_init(&client->stream, reactor_http_client_stream_event, client);
  reactor_tcp_client_init(&client->tcp_client, reactor_http_client_tcp_client_event, client);
  reactor_http_parser_init(&client->parser, reactor_http_client_parser_event, client);
}

int reactor_http_client_open(reactor_http_client *client, char *method, char *uri, char *content, size_t content_size, int flags)
{
  int e;
  char *host, *service, *path;

  if (client->state != REACTOR_HTTP_CLIENT_CLOSED)
    return -1;

  client->uri = strdup(uri);
  if (!client->uri)
    return -1;

  e = reactor_http_split_url(client->uri, &host, &service, &path);
  if (e == -1)
    return -1;

  reactor_http_request_create(&client->request, host, service, method, path, content, content_size);
  reactor_http_request_add_header(&client->request, "Connection", "close");
  reactor_http_parser_open_response(&client->parser, &client->response, flags);

  e = reactor_tcp_client_open(&client->tcp_client, &client->stream, host, service);
  if (e == -1)
    return -1;

  client->state = REACTOR_HTTP_CLIENT_CONNECTING;
  return 0;
}

void reactor_http_client_close(reactor_http_client *client)
{
  if (client->state == REACTOR_HTTP_CLIENT_CLOSED)
    return;

  if (client->state != REACTOR_HTTP_CLIENT_CLOSING)
    {
      client->state = REACTOR_HTTP_CLIENT_CLOSING;
      reactor_tcp_client_close(&client->tcp_client);
      reactor_stream_close(&client->stream);
      reactor_http_parser_close(&client->parser);
    }

  if (client->state != REACTOR_HTTP_CLIENT_CLOSED &&
      client->tcp_client.state == REACTOR_TCP_CLIENT_CLOSED &&
      client->stream.state == REACTOR_STREAM_CLOSED)
    {
      client->state = REACTOR_HTTP_CLIENT_CLOSED;
      free(client->uri);
      reactor_http_request_clear(&client->request);
      reactor_http_response_clear(&client->response);
      reactor_user_dispatch(&client->user, REACTOR_HTTP_CLIENT_CLOSE, NULL);
    }
}

void reactor_http_client_error(reactor_http_client *client)
{
  if (client->state == REACTOR_HTTP_CLIENT_CONNECTED)
    reactor_user_dispatch(&client->user, REACTOR_HTTP_CLIENT_ERROR, NULL);
}

void reactor_http_client_tcp_client_event(void *state, int type, void *data)
{
  reactor_http_client *client;

  (void) data;
  client = state;
  switch (type)
    {
    case REACTOR_TCP_CLIENT_ERROR:
      reactor_user_dispatch(&client->user, REACTOR_HTTP_CLIENT_ERROR, NULL);
      reactor_http_client_close(client);
      break;
    case REACTOR_TCP_CLIENT_CLOSE:
      reactor_http_client_close(client);
      break;
    }
}

void reactor_http_client_stream_event(void *state, int type, void *data)
{
  reactor_http_client *client;
  reactor_stream_data *in;

  client = state;

  switch (type)
    {
    case REACTOR_STREAM_CONNECT:
      client->state = REACTOR_HTTP_CLIENT_CONNECTED;
      reactor_http_request_send(&client->request, &client->stream);
      break;
    case REACTOR_STREAM_DATA:
      in = data;
      reactor_http_parser_data(&client->parser, in);
      break;
    case REACTOR_STREAM_CLOSE:
      reactor_http_client_close(client);
      break;
    case REACTOR_STREAM_END:
      reactor_http_client_close(client);
      break;
    case REACTOR_STREAM_ERROR:
      reactor_http_client_error(client);
      reactor_http_client_close(client);
      break;
    default:
      reactor_http_client_close(client);
      break;
    }
}

void reactor_http_client_parser_event(void *state, int type, void *data)
{
  reactor_http_client *client;

  client = state;
  switch (type)
    {
    case REACTOR_HTTP_PARSER_ERROR:
      reactor_user_dispatch(&client->user, REACTOR_HTTP_CLIENT_ERROR, NULL);
      reactor_http_client_close(client);
      break;
    case REACTOR_HTTP_PARSER_DONE:
      reactor_user_dispatch(&client->user, REACTOR_HTTP_CLIENT_RESPONSE, &client->response);
      reactor_http_client_close(client);
      break;
    case REACTOR_HTTP_PARSER_HEADER:
      reactor_user_dispatch(&client->user, REACTOR_HTTP_CLIENT_HEADER, &client->response);
      break;
    case REACTOR_HTTP_PARSER_CHUNK:
      reactor_user_dispatch(&client->user, REACTOR_HTTP_CLIENT_CHUNK, data);
      break;
    }
}
