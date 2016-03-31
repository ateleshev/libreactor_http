#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <netdb.h>
#include <sys/param.h>

#include <dynamic.h>
#include <clo.h>
#include <reactor_core.h>
#include <reactor_net.h>

#include "picohttpparser.h"
#include "reactor_http_request.h"
#include "reactor_http_response.h"
#include "reactor_http.h"

static const char *reactor_http_response_message[] =
  {
    [100] = "Continue",
    [101] = "Switching Protocols",
    [200] = "OK",
    [201] = "Created",
    [202] = "Accepted",
    [203] = "Non-Authoritative Information",
    [204] = "No Content",
    [205] = "Reset Content",
    [206] = "Partial Content",
    [300] = "Multiple Choices",
    [301] = "Moved Permanently",
    [302] = "Found",
    [303] = "See Other",
    [305] = "Use Proxy",
    [306] = "(Unused)",
    [307] = "Temporary Redirect",
    [400] = "Bad Request",
    [401] = "Unauthorized",
    [402] = "Payment Required",
    [403] = "Forbidden",
    [404] = "Not Found",
    [405] = "Method Not Allowed",
    [406] = "Not Acceptable",
    [407] = "Proxy Authentication Required",
    [408] = "Request Timeout",
    [409] = "Conflict",
    [410] = "Gone",
    [411] = "Length Required",
    [412] = "Precondition Failed",
    [413] = "Request Entity Too Large",
    [414] = "Request-URI Too Long",
    [415] = "Unsupported Media Type",
    [416] = "Requested Range Not Satisfiable",
    [417] = "Expectation Failed",
    [500] = "Internal Server Error",
    [501] = "Not Implemented",
    [502] = "Bad Gateway",
    [503] = "Service Unavailable",
    [504] = "Gateway Timeout",
    [505] = "HTTP Version Not Supported"
  };

void reactor_http_response_clear(reactor_http_response *response)
{
  vector_erase(&response->fields, 0, vector_size(&response->fields));
}

int reactor_http_response_create(reactor_http_response *response, int status, char *message, int version,
                                 char *content_type, char *content, size_t content_size)
{
  if (!message)
    message = (char *) reactor_http_response_message[status];

  *response = (reactor_http_response)
    {
      .status = status,
      .message = message,
      .version = version,
      .content = content,
      .content_size = content_size,
    };

  if (content_type)
    return vector_push_back(&response->fields, (reactor_http_field[]){{.key = "Content-Type", .value = content_type}});

  return 0;
}

void reactor_http_response_send(reactor_http_response *response, reactor_stream *stream)
{
  reactor_stream_puts(stream, "HTTP/1.1 ");
  reactor_stream_putu(stream, response->status);
  reactor_stream_puts(stream, " ");
  reactor_stream_puts(stream, response->message);
  reactor_stream_puts(stream, "\r\n");
  reactor_stream_puts(stream, "\r\n\r\n");
  if (response->content_size)
    reactor_stream_write(stream, response->content, response->content_size);
}

void reactor_http_response_parser_init(reactor_http_response_parser *parser)
{
  *parser = (reactor_http_response_parser) {.state = REACTOR_HTTP_RESPONSE_PARSER_HEADER};
  reactor_http_response_init(&parser->response);
}

void reactor_http_response_parser_set(reactor_http_response_parser *parser, int flags)
{
  parser->flags = flags;
}

void reactor_http_response_parser_data(reactor_http_response_parser *parser, reactor_stream_data *data, reactor_user *user)
{
  switch(parser->state)
    {
    case REACTOR_HTTP_RESPONSE_PARSER_HEADER:
      reactor_http_response_parser_header(parser, data, user);
      break;
    case REACTOR_HTTP_RESPONSE_PARSER_CONTENT:
      reactor_http_response_parser_content(parser, data, user);
      break;
    case REACTOR_HTTP_RESPONSE_PARSER_CHUNKED_CONTENT:
      reactor_http_response_parser_chunked_content(parser, data, user);
      break;
    }
}

void reactor_http_response_parser_header(reactor_http_response_parser *parser, reactor_stream_data *data, reactor_user *user)
{
  reactor_http_response *response;
  size_t message_size, fields_count, i;
  struct phr_header fields[REACTOR_HTTP_RESPONSE_MAX_FIELDS];
  reactor_http_field field;

  response = &parser->response;
  fields_count = REACTOR_HTTP_RESPONSE_MAX_FIELDS;
  parser->header_size = phr_parse_response(data->base, data->size, &response->version, &response->status,
                                           (const char **) &response->message, &message_size, fields, &fields_count, 0);
  if (parser->header_size == -2 || parser->header_size == -1)
    return;

  parser->base = data->base;
  parser->state = REACTOR_HTTP_RESPONSE_PARSER_CONTENT;
  parser->chunk_offset = parser->header_size;

  response->message[message_size] = '\0';
  response->content = data->base + parser->header_size;
  response->content_size = 0;
  for (i = 0; i < fields_count; i ++)
    {
      field.key = (char *) fields[i].name;
      field.key[fields[i].name_len] = '\0';
      field.value = (char *) fields[i].value;
      field.value[fields[i].value_len] = '\0';
      vector_push_back(&response->fields, &field);
      if (strcasecmp(field.key, "content-length") == 0)
        response->content_size = strtoul(field.value, NULL, 0);
      if (strcasecmp(field.key, "transfer-encoding") == 0 && strcasecmp(field.value, "chunked") == 0)
        parser->state = REACTOR_HTTP_RESPONSE_PARSER_CHUNKED_CONTENT;
    }

  if (parser->flags & REACTOR_HTTP_RESPONSE_PARSER_STREAM)
    {
      reactor_user_dispatch(user, REACTOR_HTTP_RESPONSE_HEADER, response);
      reactor_stream_data_consume(data, parser->header_size);
      parser->header_size = 0;
      parser->chunk_offset = 0;
    }

  reactor_http_response_parser_data(parser, data, user);
}

void reactor_http_response_parser_content(reactor_http_response_parser *parser, reactor_stream_data *data, reactor_user *user)
{
  size_t remaining, size, i;
  reactor_http_field *field;

  remaining = parser->header_size + parser->response.content_size;
  if (parser->flags & REACTOR_HTTP_RESPONSE_PARSER_STREAM && data->size && remaining)
    {
      size = MIN(remaining, data->size);
      reactor_user_dispatch(user, REACTOR_HTTP_RESPONSE_CONTENT, (reactor_stream_data[]){{.base = data->base, .size = size}});
      reactor_stream_data_consume(data, size);
      parser->response.content_size -= size;
      remaining -= size;
    }

  if (data->size < remaining)
    return;

  if (parser->flags & REACTOR_HTTP_RESPONSE_PARSER_STREAM)
    {
      reactor_user_dispatch(user, REACTOR_HTTP_RESPONSE_END, NULL);
    }
  else
    {
      if (data->base != parser->base)
        {
          parser->response.message += data->base - parser->base;
          parser->response.content += data->base - parser->base;
          for (i = 0; i < vector_size(&parser->response.fields); i ++)
            {
              field = vector_at(&parser->response.fields, i);
              field->key += data->base - parser->base;
              field->value += data->base - parser->base;
            }
        }
      reactor_user_dispatch(user, REACTOR_HTTP_RESPONSE, &parser->response);
    }

  reactor_stream_data_consume(data, remaining);
  reactor_http_response_clear(&parser->response);
  parser->state = REACTOR_HTTP_RESPONSE_PARSER_HEADER;
  reactor_http_response_parser_data(parser, data, user);
}

void reactor_http_response_parser_chunked_content(reactor_http_response_parser *parser, reactor_stream_data *data, reactor_user *user)
{
  size_t size, i;
  reactor_http_field *field;
  char *start, *end, *eol;

  start = data->base + parser->chunk_offset;
  end = data->base + data->size;
  eol = memmem(start, end - start, "\r\n", 2);
  if (!eol)
    return;

  size = strtoul(start, NULL, 16);
  if (size)
    {
      start = eol + 2;
      if ((size_t) (end - start) < size)
        return;
      if (parser->flags & REACTOR_HTTP_RESPONSE_PARSER_STREAM)
        {
          reactor_user_dispatch(user, REACTOR_HTTP_RESPONSE_CONTENT, (reactor_stream_data[]){{.base = start, .size = size}});
          reactor_stream_data_consume(data, (start + size + 2) - data->base);
        }
      else
        {
          memmove(data->base + parser->header_size + parser->response.content_size, start, size);
          parser->response.content_size += size;
          parser->chunk_offset = (start + size + 2) - data->base;
        }

      reactor_http_response_parser_data(parser, data, user);
    }
  else
    {
      if (data->base != parser->base)
        {
          parser->response.message += data->base - parser->base;
          parser->response.content += data->base - parser->base;
          for (i = 0; i < vector_size(&parser->response.fields); i ++)
            {
              field = vector_at(&parser->response.fields, i);
              field->key += data->base - parser->base;
              field->value += data->base - parser->base;
            }
        }
      reactor_user_dispatch(user, REACTOR_HTTP_RESPONSE, &parser->response);
      reactor_stream_data_consume(data, eol + 2 - data->base);
      reactor_http_response_clear(&parser->response);
    }
}
