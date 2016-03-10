#ifndef REACTOR_HTTP_PARSER_H_INCLUDED
#define REACTOR_HTTP_PARSER_H_INCLUDED

#ifndef REACTOR_HTTP_PARSER_MAX_FIELDS
#define REACTOR_HTTP_PARSER_MAX_FIELDS 32
#endif /* REACTOR_HTTP_PARSER_MAX_FIELDS */

enum reactor_http_parser_events
{
  REACTOR_HTTP_PARSER_ERROR,
  REACTOR_HTTP_PARSER_HEADER,
  REACTOR_HTTP_PARSER_CHUNK,
  REACTOR_HTTP_PARSER_CONTENT_END,
  REACTOR_HTTP_PARSER_MESSAGE
};

enum reactor_http_parser_state
{
  REACTOR_HTTP_PARSER_CLOSED,
  REACTOR_HTTP_PARSER_RESPONSE_HEADER,
  REACTOR_HTTP_PARSER_REQUEST_HEADER,
  REACTOR_HTTP_PARSER_BODY,
  REACTOR_HTTP_PARSER_CHUNKED_BODY,
  REACTOR_HTTP_PARSER_FINAL,
  REACTOR_HTTP_PARSER_DONE
};

enum reactor_http_parser_flags
{
  REACTOR_HTTP_PARSER_FLAGS_RESPONSE = 0x01,
  REACTOR_HTTP_PARSER_FLAGS_STREAM   = 0x02
};

typedef struct reactor_http_parser reactor_http_parser;
struct reactor_http_parser
{
  int                    state;
  int                    flags;
  reactor_user           user;
  reactor_http_request  *request;
  reactor_http_response *response;
  char                  *base;
  size_t                 size;
  size_t                 content_begin;
  size_t                 content_end;
  size_t                 chunk_begin;
};

void reactor_http_parser_init(reactor_http_parser *, reactor_user_call *, void *);
void reactor_http_parser_open_response(reactor_http_parser *, reactor_http_response *, int);
void reactor_http_parser_open_request(reactor_http_parser *, reactor_http_request *, int);
void reactor_http_parser_error(reactor_http_parser *);
void reactor_http_parser_close(reactor_http_parser *);
void reactor_http_parser_data(reactor_http_parser *, reactor_stream_data *);
void reactor_http_parser_request_header(reactor_http_parser *, reactor_stream_data *);
void reactor_http_parser_response_header(reactor_http_parser *, reactor_stream_data *);
void reactor_http_parser_body(reactor_http_parser *, reactor_stream_data *);
void reactor_http_parser_chunked_body(reactor_http_parser *, reactor_stream_data *);
void reactor_http_parser_final(reactor_http_parser *, reactor_stream_data *);
void reactor_http_parser_request_final(reactor_http_parser *, reactor_stream_data *);
void reactor_http_parser_response_final(reactor_http_parser *, reactor_stream_data *);

#endif /* REACTOR_HTTP_PARSER_H_INCLUDED */
