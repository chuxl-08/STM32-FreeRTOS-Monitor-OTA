#ifndef __AT_PARSER_H
#define __AT_PARSER_H

#include <stdint.h>

typedef enum
{
    AT_PARSER_OK = 0,
    AT_PARSER_ERR_PARAM,
    AT_PARSER_ERR_TIMEOUT,
    AT_PARSER_ERR_OVERFLOW,
    AT_PARSER_ERR_RESPONSE,
    AT_PARSER_ERR_PORT
} AtParserStatus_t;

AtParserStatus_t AtParser_SendAndWait(const char *cmd,
                                      const char *expect,
                                      uint32_t timeout_cycles);
const char *AtParser_StatusString(AtParserStatus_t status);

#endif
