#ifndef __JSSP_H_
#define __JSSP_H_

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * JSON node/type identifier. Basic types are:
   * 	o Object
   * 	o Array
   * 	o String
   * 	o Object-Key
   * 	o Primitive: number, boolean (true/false) or null
   */
  typedef enum
  {
    JSSP_ARRAY_OPEN = 0,
    JSSP_ARRAY_VAL = 1,
    JSSP_ARRAY_CLOSE = 2,
    JSSP_OBJECT_OPEN = 3,
    JSSP_OBJECT_KEY = 4,
    JSSP_OBJECT_COMMA = 5,
    JSSP_OBJECT_VAL = 6,
    JSSP_OBJECT_CLOSE = 7
  } jssptype_t;

  typedef enum
  {
    JSSP_PRIMITIVE = 0,
    JSSP_STRING = 1
  } jsspliteral_t;

  typedef enum
  {
    JSSP_SUCCESS,
    JSSP_TERMINATE,
    /* Not enough buffer to hold path while travers the tree */
    JSSP_ERROR_NOMEM,
    /* Invalid character inside JSON string */
    JSSP_ERROR_INVAL,
    /* The string is not a full JSON packet, more bytes expected */
    JSSP_ERROR_PART,
    JSSP_ERROR_BROKEN
  } jssperr_t;

  /* The node type includes JSSP_ARRAY, JSSP_OBJECT, JSSP_OBJECT_KEY_INC, and JSSP_OBJECT_KEY*/
  typedef struct
  {
    jssptype_t type;
    size_t size;
  } jsspnode_t;

  /**
   * JSON parser. Contains an array of token blocks available. Also stores
   * the string being parsed now and current position in that string
   */
  typedef struct
  {
    size_t js_offset;
    size_t node; /* current working node offset in buffer */
    const char *key;
    size_t key_len;
    const char *start; /* start point of string/primitive in buffer */
    size_t len;
    jsspliteral_t literal_type;
    char reg[8]; /* store the partial escaped string */
    jssperr_t last_err;
    uint64_t stream_offset;
  } jssp_parser;

  typedef int
  (*jssp_process_callback) (void *cls,
                            jssptype_t type,
                            size_t depth,
                            size_t index,
                            const char *key,
                            size_t key_len,
                            const char *data,
                            size_t data_size,
                            uint64_t stream_offset);

  /**
   * Initial a JSON parser
   */
  void
  jssp_init (jssp_parser *parser);

  /**
   * Run JSON parser. It parses a JSON data string sequence json objects,
   * and make callback.
   */
  jssperr_t
  jssp_parse (jssp_parser *parser,
              const char *js,
              size_t len,
              void *buf,
              size_t buf_size,
              size_t max_buffered_key_size,
              jssp_process_callback,
              void *cls);

#ifdef __cplusplus
}
#endif

#endif /* __JSSP_H_ */
