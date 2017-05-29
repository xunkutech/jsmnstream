#include <string.h>

#include "jssp.h"

#ifndef JSSP_DEBUG
#define jssp_debug(M, ...)
#else
#include <stdio.h>
#define jssp_debug(M, ...) do { fprintf(stderr, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while(0)
static char *JSSP_TYPE[8] =
  { "JSSP_ARRAY_OPEN" ,
    "JSSP_ARRAY_VAL" ,
    "JSSP_ARRAY_CLOSE" ,
    "JSSP_OBJECT_OPEN" ,
    "JSSP_OBJECT_KEY" ,
    "JSSP_OBJECT_COMMA" ,
    "JSSP_OBJECT_VAL" ,
    "JSSP_OBJECT_CLOSE"};
#endif

#define jssp_get_node(n, b) (&((jsspnode_t *) (b))[n])

#define __skip_chars(_1,_2,_3,_4,_5,NAME,...) NAME
#define __skip_char1(base,offset,X) X == *((base) + (offset))
#define __skip_char2(base,offset,X,...) X == *((base) + (offset)) || __skip_char1(base,offset,__VA_ARGS__)
#define __skip_char3(base,offset,X,...) X == *((base) + (offset)) || __skip_char2(base,offset,__VA_ARGS__)
#define __skip_char4(base,offset,X,...) X == *((base) + (offset)) || __skip_char3(base,offset,__VA_ARGS__)
#define __skip_char5(base,offset,X,...) X == *((base) + (offset)) || __skip_char4(base,offset,__VA_ARGS__)

#define jssp_skip_chars(base, offset, ...) do {\
  while(__skip_chars(__VA_ARGS__,__skip_char5,__skip_char4,__skip_char3,__skip_char2,__skip_char1)(base,offset,__VA_ARGS__)) \
    { \
      jssp_debug("skipping %d", js[(offset)]); \
      (offset)++; \
    } \
} while(0)

#define jssp_min(a, b) \
   ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
      _a < _b ? _a : _b; })

#define jssp_valid_word(c) \
  (((c) >= 48 && (c) <= 57) || /* 0-9 */ \
   ((c) >= 65 && (c) <= 70) || /* A-F */ \
   ((c) >= 97 && (c) <= 102))   /* a-f */

#define jssp_valid_utf8data(c) (((c) & 0xC0) == 0x80)

#define jssp_do_callback(p, b, cb, cls) do { \
  __typeof__ (p) _p = (p); \
  __typeof__ (b) _b = (b); \
  jssp_debug("Invoke user's callback function. type: %s, depth: %zu, index: %zu, key: %.*s, data: %.*s, offset: %zu", \
             JSSP_TYPE[jssp_get_node(_p->node, _b)->type], \
             _p->node, \
             jssp_get_node(_p->node -1, _b)->size, \
             (int)_p->key_len, \
             _p->key, \
             (int)_p->len, \
             _p->start, \
             _p->stream_offset + (uint64_t)_p->js_offset); \
  if((cb)((cls), \
          jssp_get_node(_p->node, _b)->type, \
          _p->node, \
          jssp_get_node(_p->node -1, _b)->size, \
          _p->key, \
          _p->key_len, \
          _p->start, \
          _p->len, \
          _p->stream_offset + (uint64_t)_p->js_offset) != 0) \
    { \
      jssp_debug("Terminated by user's callback function."); \
       _p->last_err = JSSP_TERMINATE; \
       return JSSP_TERMINATE; \
    } \
} while(0)

#define jssp_alloc_node(p, b, bs, t) do { \
  __typeof__ (p) _p = (p); \
  __typeof__ (b) _b = (b); \
  __typeof__ (bs) _bs = (bs); \
  __typeof__ (t) _t = (t); \
  if (NULL == _b || _p->node < 0) \
    { \
      jssp_debug("buffer is null or node array is not initialized."); \
      _p->last_err = JSSP_ERROR_NOMEM; \
      return JSSP_ERROR_NOMEM; \
    } \
  if (sizeof(jsspnode_t) * (_p->node + 2) > _bs) \
    { \
      jssp_debug("node length %zu exceed buf size %zu.", \
                 _p->node + 2, \
                 _bs); \
      _p->last_err = JSSP_ERROR_NOMEM; \
      return JSSP_ERROR_NOMEM; \
    } \
    (&((jsspnode_t *) (_b))[++_p->node])->type = _t; \
    (&((jsspnode_t *) (_b))[_p->node])->size = 0; \
    jssp_debug("Create node[%zu] type is %s", _p->node, JSSP_TYPE[_t]); \
} while (0)

#define jssp_release_node(p) do { \
  __typeof__ (p) _p = (p); \
  jssp_debug("Release node[%zu]", _p->node); \
  _p->node--; \
}while(0)

#define jssp_remained_buf(bs, n) \
  (bs - sizeof(jsspnode_t) * (n + 1))

/* 1.NOMEM reenter while saving key in broken key.
 * 1.broken in key (key is null)
 * 3.broken in key success. (key is not null)
 * 2.NOMEM reenter while saving key in broken obj val. (key is not null)
 * 5.broken in obj val. (key is not null)
 * */
#define jssp_save_key(b, bs, p, mx) do {  \
  __typeof__ (b) _b = (b); \
  __typeof__ (bs) _bs = (bs); \
  __typeof__ (p) _p = (p); \
  __typeof__ (mx) _mx = (mx); \
  char *k = _b + sizeof(jsspnode_t) * (_p->node + 1); \
  if (NULL == _p->key) \
    { \
      if (jssp_min(_p->len, _mx) + 1 > jssp_remained_buf(_bs, _p->node)) \
        { \
          _p->last_err = JSSP_ERROR_NOMEM; \
          return JSSP_ERROR_NOMEM; \
        } \
      strncpy(k, _p->start, jssp_min(_p->len, _mx)); \
      _p->key = k; \
      _p->key_len = jssp_min(_p->len, _mx); \
      _p->start = NULL; \
      _p->len = 0; \
    } \
  else if (k == _p->key) \
    { \
      if (jssp_min(_p->len + strlen(k), _mx) + 1 > jssp_remained_buf(_bs, _p->node)) \
        { \
          _p->last_err = JSSP_ERROR_NOMEM; \
          return JSSP_ERROR_NOMEM; \
        } \
      strncat(k, _p->start, jssp_min(_p->len, _mx)); \
      p->key_len = strlen(k); \
      _p->start = NULL; \
      _p->len = 0; \
    } \
  else \
    { \
      if (jssp_min(_p->key_len, _mx) + 1 > jssp_remained_buf(_bs, _p->node)) \
        { \
          _p->last_err = JSSP_ERROR_NOMEM; \
          return JSSP_ERROR_NOMEM; \
        } \
      strncpy(k, _p->key, jssp_min(_p->key_len, _mx)); \
      _p->key = k; \
      _p->key_len = jssp_min(_p->key_len, _mx); \
    } \
} while (0)

static unsigned char utf8_bom[] =
    { 0xEF ,0xBB ,0xBF };

/* calculate how many bytes remained since
 * the pos (included) in the given buffer
 *
 *  e.g.
 *          012345
 *  data = "ABCDE"
 *  if (data_len == 5 && *pos == 'C') return 3;
 *  if (data_len == 4 && *pos == 'C') return 2;
 *  if (data_len == 6 && *pos == 'C') return 3;
 *
 *  data <= pos <= data+data_len
 */
static size_t
jssp_remained_len (const char *data,
                   size_t data_len,
                   const char *pos,
                   int upmost)
{
  int i;

  for (i = 0; i < upmost; i++)
    {
      if (pos + i > data + data_len - 1 || *(pos + i) == '\0')
        break;
    }
  return i;
}

/**
 * A re-enterable function that we can
 * We do not break the UTF-8 byte squence in string partial.

 bitsFirst     Last      Bytes B1        B2        B3        B4        B5        B6
 07  U+0000    U+007F      1   0xxxxxxx
 11  U+0080    U+07FF      2   110xxxxx  10xxxxxx
 16  U+0800    U+FFFF      3   1110xxxx  10xxxxxx  10xxxxxx
 21  U+10000   U+1FFFFF    4   11110xxx  10xxxxxx  10xxxxxx  10xxxxxx
 26  U+200000  U+3FFFFFF   5   111110xx  10xxxxxx  10xxxxxx  10xxxxxx  10xxxxxx
 31  U+4000000 U+7FFFFFFF  6   1111110x  10xxxxxx  10xxxxxx  10xxxxxx  10xxxxxx  10xxxxxx
 * */
static jssperr_t
jssp_parse_literal (jsspliteral_t type,
                    const char *js,
                    size_t len,
                    size_t *js_offset,
                    const char **start,
                    size_t *size,
                    char *reg)
{
  if (NULL != *start)
    {
      jssp_debug("Warning: Previous output does not reset. Check your code.");
      return JSSP_ERROR_INVAL;
    }
  int i, j;

  const char *pos = js + (*js_offset);

  jssp_debug("Parsing data ---%.*s---", (int )(len - *js_offset), pos);
  jssp_debug("Literal type is  %d", type);
  for (; pos < js + len && *pos != '\0'; pos++)
    {
      char c = *pos;
      jssp_debug("reg[0] is %d.", reg[0]);
      /*check whether in broken status*/
      if ((int) reg[0] > 0)
        {
          /* deal with escape symbol broken  */
          if (reg[0] == 1 && reg[1] == '\\')
            {
              jssp_debug("Continue with escaple symbol broken.");
              goto escaped_symbols_broken;
            }

          /* deal with \uxxxx  broken  */
          if (reg[0] > 1 && reg[1] == '\\' && reg[2] == 'u')
            {
              jssp_debug("Continue with \\uxxxx broken.");
              goto hex_symbols_broken;
            }

          jssp_debug("Continue with utf8 broken.");
          goto utf8_symbols_broken;
        }

      switch (c)
        {
#ifndef JSSP_STRICT
        /* In strict mode primitive must be followed by "," or "}" or "]" */
        case ':':

#endif
        case '\t':

        case '\r':

        case '\n':

        case ' ':

        case ',':

        case ']':

        case '}':
          if (NULL != *start)
            {
              jssp_debug("Found broken literal is %.*s, offset is %zu, size is %zu",
                  (int )(*size),
                  *start,
                  *js_offset,
                  *size);
              return (type == JSSP_STRING) ? JSSP_ERROR_BROKEN : JSSP_SUCCESS;
            }

          if (type == JSSP_STRING)
            break;

          *start = js + *js_offset;
          *size = pos - *start;
          *js_offset = pos - js;
          jssp_debug("Found primitive literal is %.*s, offset is %zu, size is %zu",
              (int )(*size),
              *start,
              *js_offset,
              *size);
          jssp_debug("New offset char is %c", js[*js_offset]);
          return JSSP_SUCCESS;
        case '\"':
          if (NULL != *start)
            {
              jssp_debug("Found broken literal is %.*s, offset is %zu, size is %zu",
                  (int )(*size),
                  *start,
                  *js_offset,
                  *size);
              (*js_offset)++;
              return (type == JSSP_STRING) ? JSSP_SUCCESS : JSSP_ERROR_INVAL;
            }

          if (type == JSSP_STRING)
            {
              *start = js + *js_offset;
              *size = pos - *start;
              *js_offset = pos - js + 1;
              jssp_debug("Found string literal is %.*s, offset is %zu, size is %zu",
                  (int )(*size),
                  *start,
                  *start - js,
                  *size);
              jssp_debug("New offset char is %c", js[*js_offset]);
              return JSSP_SUCCESS;
            }

          jssp_debug("We claimed the literal is not start with \", but we met \".");
          return JSSP_ERROR_INVAL;
        case '\\':
          if (NULL != *start)
            {
              jssp_debug("Found broken literal is %.*s, offset is %zu, size is %zu",
                  (int )(*size),
                  *start,
                  *js_offset,
                  *size);
              return JSSP_ERROR_BROKEN;
            }
          j = jssp_remained_len (js, len, pos, 2);
          if (j == 1) /*replace code here*/
            {
              reg[0] = 1;
              reg[1] = '\\';
              *start = js + *js_offset;
              *size = pos - *start;
              *js_offset = pos - js + 1;
              jssp_debug("Found broken escape symbol \\ without following chars");
              return JSSP_ERROR_BROKEN;
            }

          pos++; /*step in, so as same as the escaped_symbols_broken */
          escaped_symbols_broken:
          /* broken status unknown */
          switch (*pos)
            {
            /* Allowed escaped symbols */
            case '\"':

            case '/':

            case '\\':

            case 'b':

            case 'f':

            case 'r':

            case 'n':

            case 't':
              if (reg[0] == 1 && reg[1] == '\\')
                {
                  reg[2] = *pos;
                  *start = reg + 1;
                  *size = 2;
                  *js_offset = pos - js + 1;
                  /* clear broken status */
                  reg[0] = 0;
                  jssp_debug("Found matched char %c for broken escape symbol",
                      *pos);
                  jssp_debug("Next char is %c", js[*js_offset]);
                  /* flush buffered symbol first */
                  return JSSP_ERROR_BROKEN;
                }
              /* no broken and matched, continue */
              break;
              /* Allows escaped symbol \uXXXX */
            case 'u':
              pos++; /* step into data part, it will be same with
               the hex_symbols_broken re-enter condition */

              hex_symbols_broken:

              /* i is the length (0~3) of data part of \uXXXX stored in the buffer */
              i = reg[0] > 0 ? reg[0] - 2 : 0;

              /* j = [1,4] is the avaible chars to match \uXXXX, i+j must equals 4*/
              j = jssp_remained_len (js, len, pos, 4 - i);

              jssp_debug("i is %d, j is %d", i, j);

              /* can not hold the completed part */
              if (i + j < 4)
                {
                  /* We should output chars before \uXXX if we could */
                  if (pos - 2 - js > *js_offset)
                    {
                      *start = js + *js_offset;
                      *size = pos - 2 - *start;
                    }

                  /* skip to the data part */
                  *js_offset = pos - js + j;

                  /* save or append data (\u ~ \uXXX) to buffer */
                  reg[0] = (char) (i + j + 2);
                  reg[1] = '\\';
                  reg[2] = 'u';
                  for (; j > 0; j--)
                    {
                      if (!jssp_valid_word(*(pos + j - 1)))
                        {
                          jssp_debug("There is invalid char %c in \\uxxxx",
                              *(pos + j - 1));
                          return JSSP_ERROR_INVAL;
                        }
                      reg[2 + i + j] = *(pos + j - 1);
                    }
                  jssp_debug("return broken \\uxxxx, saved count %d, current offset is %zu, char is %c",
                      (int )reg[0],
                      *js_offset,
                      js[*js_offset]);
                  return JSSP_ERROR_BROKEN;
                }

              /* We can process the whole symbol, but some part is from the buffer */
              if (reg[0] != 0)
                {
                  /* prepare output */
                  *start = reg + 1;
                  *size = 6;
                  /* clear the broken flag*/
                  reg[0] = 0;
                  /* Anyway move after the data part */
                  *js_offset = pos - js + j;

                  pos += j - 1;

                  jssp_debug("char at pos is %c", *pos);

                  for (; j > 0; j--)
                    {
                      if (!jssp_valid_word(*(pos + 1 - j)))
                        {
                          jssp_debug("There is an invalid char %c in \\uxxxx sequence",
                              *(pos + 1 - j));
                          return JSSP_ERROR_INVAL;
                        }
                      reg[3 + i++] = *(pos + 1 - j);
                    }
                  /* if it is the latest char in a literal sequence, how can we deal with it? */
                  /* continue; for next loop if we meet the end return success, else, output the broken chars and return broken*/
                  continue;
                }
              /* The normal process */
              jssp_debug("j must be 4 here, j is %d", j);

              pos += j - 1;

              for (; j > 0; j--)
                {
                  if (!jssp_valid_word(*(pos + 1 - j)))
                    {
                      jssp_debug("There is an invalid char %c in \\uxxxx sequence",
                          *(pos + 1 - j));
                      return JSSP_ERROR_INVAL;
                    }
                }
              break;
            default:
              jssp_debug("we can not find proper char followed by escaple symbol \\, the invalid char is %c",
                  *pos);
              return JSSP_ERROR_INVAL;
            }
          break;
        default:
          jssp_debug("Parse %c, %lX", c, (long unsigned int )c);
          if (NULL != *start)
            {
              jssp_debug("Found broken literal is %.*s, offset is %zu, size is %zu",
                  (int )(*size),
                  *start,
                  *js_offset,
                  *size);
              return JSSP_ERROR_BROKEN;
            }
          /* below 1000 0000 is a valid literal char */
          if ((c & 0x80) == 0x0)
            break;

          /*  above 1000 0000 and below 1100 0000 is an invalid char
           *  utf8 data sequence will not occur here */
          if ((c & 0xC0) == 0xB0)
            {
              jssp_debug("found invalid literal char %lX",
                  (long unsigned int )c);
              return JSSP_ERROR_INVAL;
            }

          pos++; /* step into data part, it will be same with
           the utf8_symbols_broken re-enter condition */

          utf8_symbols_broken:

          i = 0;
          if (reg[0] != 0)
            {
              c = reg[1];
              i = reg[0] - 1;
            }

          /* scan utf8 leading byte:  1111 110x to 110x xxxx */
          for (j = 1; j < 6; j++)
            {
              jssp_debug("%d %lX", j, (long unsigned int )(c >> j));
              if (((c >> j) & 0xFF) == 0xFE)
                break;
            }
          /* utf8 leading byte not found */
          if (j == 6)
            {
              jssp_debug("%lX is not a valid utf8 leading char",
                  (long unsigned int )c);
              return JSSP_ERROR_INVAL;
            }
          /* calculate how many utf8 bytes (data part without the leading
           * byte) need to fetch . And save it to j
           */
          j = (6 - j) - i;

          jssp_debug("Stored utf8 char number is %d, required utf8 char num is %d",
              i,
              j);
          /* check how many bytes available this time */
          i = jssp_remained_len (js, len, pos, j);

          /* can not hold the whole utf8 sequence, save them to buffer*/
          if (i < j)
            {
              jssp_debug("Found broken utf8 char sequence");
              /* Check we should output sth this time ? */
              if (pos - 1 - js > *js_offset)
                {
                  jssp_debug("output chars before broken utf8");
                  *start = js + *js_offset;
                  *size = pos - 1 - *start;
                }

              /* move after the data part */
              *js_offset = pos - js + i;

              /* dump utf8 chars and update register buffer */
              j = reg[0] != 0 ? reg[0] - 1 : 0;
              reg[0] = (char) (j + i + 1);

              /*saving leading char*/
              reg[1] = *(pos - 1);

              /*saving data char*/
              for (; i > 0; i--)
                {
                  if (!jssp_valid_utf8data(*(pos + i - 1)))
                    {
                      jssp_debug("Invalid utf8 data char %lX",
                          (long unsigned int )(*(pos + i - 1)));
                      return JSSP_ERROR_INVAL;
                    }
                  jssp_debug("Save reg[%d] with %lX",
                      j + i + 1,
                      (long unsigned int )(*(pos + i - 1)));
                  reg[j + i + 1] = *(pos + i - 1);
                }
              jssp_debug("Saved broken utf8 char sequence");
              return JSSP_ERROR_BROKEN;
            }

          /* can hold the whole utf8 sequence, but part from the buffer, flush
           * them first  */
          if (reg[0] != 0)
            {
              i = reg[0] - 1;
              *start = reg + 1;
              *size = i + j + 1;
              /* Anyway move after the data part */
              *js_offset = pos - js + j;

              pos += j - 1;
              /* clear the broken flag*/
              reg[0] = 0;

              /* dump the utf8 chars */
              for (; j > 0; j--)
                {
                  if (!jssp_valid_utf8data(*(pos + 1 - j)))
                    {
                      jssp_debug("Invalid utf8 data char %lX",
                          (long unsigned int )(*(pos + 1 - j)));
                      return JSSP_ERROR_INVAL;
                    }
                  jssp_debug("Save reg[%d] with %lX",
                      1 + i,
                      (long unsigned int )(*(pos + 1 - j)));
                  reg[2 + i++] = *(pos + 1 - j);
                }
              jssp_debug("continued utf8 char is %.*s",
                  (int )(*size),
                  *start);
              /* if it is the latest char in a literal sequence, how can we deal with it? */
              continue;
            }
          /*normal process*/
          jssp_debug("Bypassed utf8 char %.*s", j + 1, pos - 1);
          pos += j - 1;
          for (; j > 0; j--)
            {
              if (!jssp_valid_utf8data(*(pos + 1 - j)))
                {
                  jssp_debug("Invalid utf8 data sequence %lX",
                      (long unsigned int )(*(pos + 1 - j)));
                  return JSSP_ERROR_INVAL;
                }
            }

          /* need to check other invalid non-asciss charactor here ? */
          break;
        } /* end of switch (c) */
    }/* end of for loop */

  if (NULL != *start)
    {
      jssp_debug("Found broken literal is %.*s, offset is %zu, size is %zu",
          (int )(*size),
          *start,
          *js_offset,
          *size);
      return JSSP_ERROR_BROKEN;
    }
  *start = js + *js_offset;
  *size = pos - *start;
  *js_offset = pos - js;
#ifndef JSSP_NODEBUG
  if (reg[0] != 0)
    jssp_debug("Invalid broken status.");
  #endif
  jssp_debug("End of input string and can not find literal termination. return broken");
  return JSSP_ERROR_BROKEN;
}

/* A re-entry function to parser given json string, controller is stored in the parser object */
jssperr_t
jssp_parse (jssp_parser *parser,
            const char *js,
            size_t len,
            void *buf,
            size_t buf_size,
            size_t max_key_len,
            jssp_process_callback cb,
            void *cls)
{
  if (JSSP_ERROR_INVAL == parser->last_err
    || JSSP_TERMINATE == parser->last_err)
    {
      jssp_debug("Wrong status of last exit error type. %d", parser->last_err);
      return parser->last_err;
    }

  /* first we create an array node to wrap the json object */
  if (parser->node == SIZE_MAX)
    {
      if (sizeof(jsspnode_t) > buf_size)
        {
          jssp_debug("Can not create initial node. Increase your memory.");
          return JSSP_ERROR_NOMEM;
        }
      jssp_debug("Init jssp parser.");
      jssp_get_node(0, buf)->type = JSSP_ARRAY_OPEN;
      jssp_get_node(0, buf)->size = 0;
      parser->node = 0;
    }

  while (parser->js_offset < len && js[parser->js_offset] != '\0')
    {
      if (parser->js_offset == 0 && len >= 3 && memcmp (js, utf8_bom, 3) == 0)
        {
          jssp_debug("Found utf8 bom. skipping 3 bytes.");
          parser->js_offset = 3;
        }
      jssp_skip_chars(js, parser->js_offset, '\t', '\r', '\n', ' ');
      switch (jssp_get_node(parser->node, buf)->type)
        {
        case JSSP_ARRAY_OPEN:
          /*==================================================================*/
          jssp_debug("Enter node[%zu], type is JSSP_ARRAY, current char is %c",
              parser->node,
              js[parser->js_offset]);
          switch (js[parser->js_offset])
            {
            case ']': /* <-JSSP_ARRAY */
              if (0 == parser->node)
                {
                  jssp_debug("closed wrapped arrary. invalid json data.");
                  return JSSP_ERROR_INVAL;
                }
              jssp_get_node(parser->node, buf)->type = JSSP_ARRAY_CLOSE;
              jssp_do_callback(parser, buf, cb, cls);
              jssp_release_node(parser);
              parser->js_offset++;
              continue;
            case '[': /* ->JSSP_ARRAY */
              jssp_alloc_node(parser, buf, buf_size, JSSP_ARRAY_OPEN);
              jssp_do_callback(parser, buf, cb, cls);
              parser->js_offset++;
              continue;
            case '{': /* ->JSSP_OBJECT */
              jssp_alloc_node(parser, buf, buf_size, JSSP_OBJECT_OPEN);
              jssp_do_callback(parser, buf, cb, cls);
              parser->js_offset++;
              continue;
            case ',':
              jssp_get_node(parser->node, buf)->size++;
              parser->js_offset++;
              continue;
            case '\"':
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              parser->js_offset++;
              parser->literal_type = JSSP_STRING;
              break;
#ifdef JSSP_STRICT
              case '0': case '1': case '2': case '3': case '4':
              case '5': case '6': case '7': case '8': case '9':
              case '-': case 't': case 'f': case 'n':
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              break;
              /* Unexpected char in strict mode */
              default:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

              jssp_debug("In strict mode, we met unexpected char %c", js[parser->js_offset]);
              return JSSP_ERROR_INVAL;
#else
            default:
              break;
#endif
            }
          jssp_alloc_node(parser, buf, buf_size, JSSP_ARRAY_VAL);
          /* no break */
        case JSSP_ARRAY_VAL:
          /*==================================================================*/
          jssp_debug("Enter node[%zu], type is JSSP_ARRAY_VAL, current char is %c",
              parser->node,
              js[parser->js_offset]);
          parser->last_err = jssp_parse_literal (parser->literal_type,
                                                 js,
                                                 len,
                                                 &(parser->js_offset),
                                                 &(parser->start),
                                                 &(parser->len),
                                                 parser->reg);
          switch (parser->last_err)
            {
            case JSSP_ERROR_BROKEN:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              if (parser->start != NULL)
                jssp_do_callback(parser, buf, cb, cls);
              parser->start = NULL;
              parser->len = 0;
              continue;
            case JSSP_SUCCESS:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              if (parser->start != NULL)
                jssp_do_callback(parser, buf, cb, cls);
              parser->start = NULL;
              parser->len = 0;
              jssp_release_node(parser);
              parser->literal_type = JSSP_PRIMITIVE;
              continue;
            default:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              jssp_debug("Invalid literal parse return value. check message above.");
              return parser->last_err;
            } /*  End of switch (jssp_parse_literal (parser, js, len)) */
          jssp_debug("Can not hapen here.");
          continue;
        case JSSP_OBJECT_OPEN:
          /*==================================================================*/
          jssp_debug("Enter node[%zu], type is JSSP_OBJECT, current char is %c",
              parser->node,
              js[parser->js_offset]);
          switch (js[parser->js_offset])
            {
            case '}': /* <-JSSP_OBJECT */
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              jssp_get_node(parser->node, buf)->type = JSSP_OBJECT_CLOSE;
              jssp_do_callback(parser, buf, cb, cls);
              jssp_release_node(parser);
              parser->js_offset++;
              continue;
            case ':': /* ->JSSP_OBJECT_COMMA */
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              jssp_alloc_node(parser, buf, buf_size, JSSP_OBJECT_COMMA);
              parser->js_offset++;
              continue;
            case ',':
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              jssp_get_node(parser->node, buf)->size++;
              parser->js_offset++;
              continue;
            case '\"':
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              parser->js_offset++;
              parser->literal_type = JSSP_STRING;
              break;
#ifdef JSSP_STRICT
              case '0': case '1': case '2': case '3': case '4':
              case '5': case '6': case '7': case '8': case '9':
              case '-': case 't': case 'f': case 'n':
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              break;
              /* Unexpected char in strict mode */
              default:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

              jssp_debug("In strict mode, we met unexpected char");
              return JSSP_ERROR_INVAL;
#else
            default:
              break;
#endif
            }
          jssp_alloc_node(parser, buf, buf_size, JSSP_OBJECT_KEY);
          parser->key = NULL;
          parser->key_len = 0;
          /* no break */
        case JSSP_OBJECT_KEY:
          /*==================================================================*/
          jssp_debug("Enter node[%zu], type is JSSP_OBJECT_KEY, current offset is %zu, char is %c",
              parser->node,
              parser->js_offset,
              js[parser->js_offset]);
          /* deal with broken key if key is partial or NOMEM occured  */
          if (JSSP_ERROR_NOMEM == parser->last_err)
            jssp_save_key(buf, buf_size, parser, max_key_len);

          parser->last_err = jssp_parse_literal (parser->literal_type,
                                                 js,
                                                 len,
                                                 &(parser->js_offset),
                                                 &(parser->start),
                                                 &(parser->len),
                                                 parser->reg);

          switch (parser->last_err)
            {
            case JSSP_ERROR_BROKEN:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              jssp_save_key(buf, buf_size, parser, max_key_len);
              continue;
            case JSSP_SUCCESS:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              if (NULL != parser->key
                || (jssp_min (parser->len + parser->key_len, max_key_len)
                  + sizeof(jsspnode_t) * (parser->node + 1) > buf_size))
                {
                  jssp_debug("Not enough memory, save key.");
                  jssp_save_key(buf, buf_size, parser, max_key_len);
                }
              else
                {
                  parser->key = parser->start;
                  parser->key_len = parser->len;
                  parser->start = NULL;
                  parser->len = 0;
                }
              jssp_debug("Found object key: %.*s",
                  (int )parser->key_len,
                  parser->key);
              jssp_release_node(parser);
              parser->literal_type = JSSP_PRIMITIVE;
              continue;
            default:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              jssp_debug("Invalid literal parse return value. check message above.");
              return parser->last_err;
            }
          jssp_debug("Can not hapen here.");
          continue;
        case JSSP_OBJECT_COMMA:
          /*==================================================================*/
          jssp_debug("Enter node[%zu], type is JSSP_OBJECT_COMMA, current char is %c",
              parser->node,
              js[parser->js_offset]);
          switch (js[parser->js_offset])
            {
            case '[': /* ->JSSP_ARRAY */
              jssp_release_node(parser);
              jssp_alloc_node(parser, buf, buf_size, JSSP_ARRAY_OPEN);
              jssp_do_callback(parser, buf, cb, cls);
              parser->key = NULL;
              parser->key_len = 0;
              parser->js_offset++;
              continue;
            case '{': /* ->JSSP_OBJECT */
              jssp_release_node(parser);
              jssp_alloc_node(parser, buf, buf_size, JSSP_OBJECT_OPEN);
              jssp_do_callback(parser, buf, cb, cls);
              parser->key = NULL;
              parser->key_len = 0;
              parser->js_offset++;
              continue;
            case '\"':
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              parser->js_offset++;
              parser->literal_type = JSSP_STRING;
              break;
#ifdef JSSP_STRICT
              case '0': case '1': case '2': case '3': case '4':
              case '5': case '6': case '7': case '8': case '9':
              case '-': case 't': case 'f': case 'n':
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              break;
              /* Unexpected char in strict mode */
              default:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

              jssp_debug("In strict mode, we met unexpected char");
              return JSSP_ERROR_INVAL;
#else
            default:
              break;
#endif
            }
          jssp_release_node(parser);
          jssp_alloc_node(parser, buf, buf_size, JSSP_OBJECT_VAL);
          /* no break */
        case JSSP_OBJECT_VAL:
          /*==================================================================*/
          jssp_debug("Enter node[%zu], type is JSSP_OBJECT_VAL, current char is %c",
              parser->node,
              js[parser->js_offset]);
          if (JSSP_ERROR_NOMEM == parser->last_err)
            jssp_save_key(buf, buf_size, parser, max_key_len);

          parser->last_err = jssp_parse_literal (parser->literal_type,
                                                 js,
                                                 len,
                                                 &(parser->js_offset),
                                                 &(parser->start),
                                                 &(parser->len),
                                                 parser->reg);
          switch (parser->last_err)
            {
            case JSSP_ERROR_BROKEN:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              if (parser->start != NULL)
                jssp_do_callback(parser, buf, cb, cls);

              jssp_debug("Before saving object key pos: %zu value: %.*s",
                  (size_t)parser->key,
                  (int )parser->key_len,
                  parser->key);
              /* set the data point to the key segment if it is not set before */
              if (parser->key
                != buf + sizeof(jsspnode_t) * (parser->node + 1))
                {
                  parser->start = parser->key;
                  parser->len = parser->key_len;
                  jssp_save_key(buf, buf_size, parser, max_key_len);
                }
              jssp_debug("Saved object key pos: %zu value: %.*s",
                  (size_t)parser->key,
                  (int )parser->key_len,
                  parser->key);
              parser->start = NULL;
              parser->len = 0;
              continue;
            case JSSP_SUCCESS:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              jssp_debug("Found object value: %.*s", (int )parser->len,
                  parser->start
              );
              jssp_debug("Found object key pos: %zu value: ---%.*s---",
                  (size_t)parser->key,
                  (int )parser->key_len,
                  parser->key);

              if (parser->start != NULL)
                jssp_do_callback(parser, buf, cb, cls);

              parser->start = NULL;
              parser->len = 0;
              parser->key = NULL;
              parser->key_len = 0;
              jssp_release_node(parser);
              parser->literal_type = JSSP_PRIMITIVE;
              continue;
            default:
              /*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
              jssp_debug("Invalid literal parse return value. check message above.");
              return parser->last_err;
            } /*  End of  switch (parser->last_err) */
          jssp_debug("Can not hapen here.");
          continue;
        default:
          /*==================================================================*/
          jssp_debug("Invalid node type is %d",
              jssp_get_node(parser->node, buf)->type);
          return JSSP_ERROR_INVAL;
        } /* end of switch (jssp_get_node(parser->node, buf)->type) */
    } /* end of while (parser->js_offset < len && js[parser->js_offset] != '\0') */

  parser->stream_offset += parser->js_offset;
  if (JSSP_SUCCESS != parser->last_err)
    return parser->last_err;

  if (0 == parser->node)
    return JSSP_SUCCESS;

  return JSSP_ERROR_PART;
}
/**
 * Creates a new parser based over a given  buffer with an array of tokens
 * available.
 */
void
jssp_init (jssp_parser *parser)
{
  parser->js_offset = 0;
  parser->stream_offset = 0;
  parser->start = NULL;
  parser->len = 0;
  parser->key = NULL;
  parser->key_len = 0;
  parser->node = SIZE_MAX;
  parser->last_err = JSSP_SUCCESS;
  parser->reg[0] = 0;
}
