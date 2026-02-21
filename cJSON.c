/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* cJSON */
/* JSON parser in C. */

/* disable warnings about old C89 functions in MSVC */
#if !defined(_CRT_SECURE_NO_DEPRECATE) && defined(_MSC_VER)
#define _CRT_SECURE_NO_DEPRECATE
#endif
/*_MSC_VER is a macro of Visual C++ compiler. 
So defined(_MSC_VER) means the compiler is Visual C++.
The whole setence means when _CRT_SECURE_NO_DEPRECATE is not defined and 
the compiler is Visual C++, define the _CRT_SECURE_NO_DEPRECATE macro.*/

#ifdef __GNUC__
#pragma GCC visibility push(default)
#endif
/*It is used to set the visibility of symbols. 
"push(default)" sets the current symbol visibility to the default state.*/

#if defined(_MSC_VER)
#pragma warning (push)
/* disable warning about single line comments in system headers */
#pragma warning (disable : 4001)
#endif

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

#ifdef ENABLE_LOCALES
#include <locale.h>
#endif

#if defined(_MSC_VER)
#pragma warning (pop)
#endif
#ifdef __GNUC__
#pragma GCC visibility pop
#endif

#include "cJSON.h"

/* define our own boolean type */
#ifdef true
#undef true
#endif
#define true ((cJSON_bool)1)

#ifdef false
#undef false
#endif
#define false ((cJSON_bool)0)

/* define isnan and isinf for ANSI C, if in C99 or above, isnan and isinf has been defined in math.h */
#ifndef isinf
#define isinf(d) (isnan((d - d)) && !isnan(d))
#endif
#ifndef isnan
#define isnan(d) (d != d)
#endif

#ifndef NAN
#ifdef _WIN32
#define NAN sqrt(-1.0)
#else
#define NAN 0.0/0.0
#endif
#endif

typedef struct {
    const unsigned char *json;  /*`json` is a pointer to data of type `unsigned char`, used for storing JSON data.*/
    size_t position;       /*size_t represents the position information. 
                            It may be used to record the position of errors in the JSON data.*/
} error;

/*A static global variable of type "error" named "global_error" was defined.
 it was initialized with a JSON pointer set to NULL and a position of 0.*/
static error global_error = { NULL, 0 };

/*cJSON_GetErrorPtr's purpose is to obtain the error pointer. 
 It returns a const char pointer that points to the position of the json pointer 
 within the global_error structure.*/
CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void)
{
    return (const char*) (global_error.json + global_error.position);
}

/*cJSON_GetStringValue is used to obtain the string-type value within the cJSON object.*/
/*Check if the input item is of string type. If not, return NULL.
 If it is of string type, return the valuestring member of the item, which is the pointer pointing to the string value stored.*/
CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item)
{
    if (!cJSON_IsString(item))
    {
        return NULL;
    }

    return item->valuestring;
}

/*cJSON_GetNumberValue is used to obtain the value of the numeric type within the cJSON object.*/
/*Determine whether the input item is of a numeric type. If not, return NAN.
 If it is a numeric type, return the valuedouble member of the item, which is the double-type value storing the numeric value.*/
CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item)
{
    if (!cJSON_IsNumber(item))
    {
        return (double) NAN;
    }

    return item->valuedouble;
}

/* This is a safeguard to prevent copy-pasters from using incompatible C and header files */
#if (CJSON_VERSION_MAJOR != 1) || (CJSON_VERSION_MINOR != 7) || (CJSON_VERSION_PATCH != 19)
    #error cJSON.h and cJSON.c have different versions. Make sure that both have the same.
#endif

CJSON_PUBLIC(const char*) cJSON_Version(void)
{
    static char version[15];
    sprintf(version, "%i.%i.%i", CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR, CJSON_VERSION_PATCH);

    return version;
}

/* Case insensitive string comparison, doesn't consider two NULL pointers equal though */
static int case_insensitive_strcmp(const unsigned char *string1, const unsigned char *string2)
{
    if ((string1 == NULL) || (string2 == NULL))
    {
        return 1;
    }

    if (string1 == string2)
    {
        return 0;
    }

    for(; tolower(*string1) == tolower(*string2); (void)string1++, string2++)
    {
        if (*string1 == '\0')
        {
            return 0;
        }
    }

    return tolower(*string1) - tolower(*string2);
}

typedef struct internal_hooks
{
    void *(CJSON_CDECL *allocate)(size_t size);
    void (CJSON_CDECL *deallocate)(void *pointer);
    void *(CJSON_CDECL *reallocate)(void *pointer, size_t size);
} internal_hooks;

#if defined(_MSC_VER)
/* work around MSVC error C2322: '...' address of dllimport '...' is not static */
static void * CJSON_CDECL internal_malloc(size_t size)
{
    return malloc(size);
}
static void CJSON_CDECL internal_free(void *pointer)
{
    free(pointer);
}
static void * CJSON_CDECL internal_realloc(void *pointer, size_t size)
{
    return realloc(pointer, size);
}
#else
#define internal_malloc malloc
#define internal_free free
#define internal_realloc realloc
#endif

/* strlen of character literals resolved at compile time */
#define static_strlen(string_literal) (sizeof(string_literal) - sizeof(""))

static internal_hooks global_hooks = { internal_malloc, internal_free, internal_realloc };

static unsigned char* cJSON_strdup(const unsigned char* string, const internal_hooks * const hooks)
{
    size_t length = 0;
    unsigned char *copy = NULL;

    if (string == NULL)
    {
        return NULL;
    }

    length = strlen((const char*)string) + sizeof("");
    copy = (unsigned char*)hooks->allocate(length);
    if (copy == NULL)
    {
        return NULL;
    }
    memcpy(copy, string, length);

    return copy;
}

CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks)
{
    if (hooks == NULL)
    {
        /* Reset hooks */
        global_hooks.allocate = malloc;
        global_hooks.deallocate = free;
        global_hooks.reallocate = realloc;
        return;
    }

    global_hooks.allocate = malloc;
    if (hooks->malloc_fn != NULL)
    {
        global_hooks.allocate = hooks->malloc_fn;
    }

    global_hooks.deallocate = free;
    if (hooks->free_fn != NULL)
    {
        global_hooks.deallocate = hooks->free_fn;
    }

    /* use realloc only if both free and malloc are used */
    global_hooks.reallocate = NULL;
    if ((global_hooks.allocate == malloc) && (global_hooks.deallocate == free))
    {
        global_hooks.reallocate = realloc;
    }
}

/* Internal constructor. */
static cJSON *cJSON_New_Item(const internal_hooks * const hooks)
{
    cJSON* node = (cJSON*)hooks->allocate(sizeof(cJSON));
    if (node)
    {
        memset(node, '\0', sizeof(cJSON));
    }

    return node;
}

/* 简单介绍：递归释放cJSON节点树的内存，防止内存泄漏 
   item指向待填充的cJSON根节点，可以是任意节点，函数会递归释放其所有子节点的兄弟节点
   返回void
   主要流程：①遍历兄弟节点链表，通过while循环逐个处理当前节点及其next指针指向的所有兄弟节点
            ②递归释放子节点，如果当前节点不是引用类型且有子节点，递归调用cJSON_Delete释放子树
            ③释放字符串内存，如果节点包含非const的valuestring或string，调用deallocate释放
            ④释放节点本身，使用global_hooks.deallocate释放当前节点的内存
            ⑤移动到下一个节点，将item指针移动到next，继续循环直到链表结束
    存储器内存管理：递归释放：子节点（child）会被递归处理，确保整个子树的内存都被释放
                  字符串释放：仅释放非const的字符串（const字符串由外部管理，避免重复释放）
                  引用节点：如果节点是引用类型（cJSON_IsReference），则不释放其子节点和字符串，仅释放节点本身
                  释放时机：函数调用后，所有相关内存都被释放，指针不再有效*/
/* Delete a cJSON structure. */
CJSON_PUBLIC(void) cJSON_Delete(cJSON *item)
{
    cJSON *next = NULL; /* 临时保存下一个兄弟节点的指针，避免释放当前节点后丢失链表 */
    
    /* 遍历兄弟节点链表，直到item为NULL，链表结束 */
    while (item != NULL)
    {
        /* 先保存下一个兄弟节点的指针，释放当前节点后，item->next将无效 */
        next = item->next;
        
        /* 递归释放子节点，如果当前节点不是引用类型且有子节点 */
        /* cJSON_IsReference：判断节点是否为引用类型，引用类型不拥有子节点的内存 */
        if (!(item->type & cJSON_IsReference) && (item->child != NULL))
        {
            cJSON_Delete(item->child); /* 递归释放子节点树*/
        }
        
        /* 释放valuestring，如果节点不是引用类型且valuestring非空 */
        /* valuestring存储JSON字符串类型的值，非引用类型节点拥有该内存 */
        if (!(item->type & cJSON_IsReference) && (item->valuestring != NULL))
        {
            global_hooks.deallocate(item->valuestring); /* 释放字符串内存 */
            item->valuestring = NULL; /* 清空指针，避免野指针 */
        }
        
        /* 释放string，如果节点的string不是const类型且非空 */
        /* cJSON_StringIsConst：判断string是否为const（const字符串由外部管理，不释放） */
        if (!(item->type & cJSON_StringIsConst) && (item->string != NULL))
        {
            global_hooks.deallocate(item->string); /* 释放字符串内存 */
            item->string = NULL; /* 清空指针，避免野指针 */
        }
        
        /* 释放当前节点本身，使用全局内存钩子释放节点内存 */
        global_hooks.deallocate(item);
        /* 移动到下一个兄弟节点，继续处理链表的下一个元素 */
        item = next;
    }
}

/* get the decimal point character of the current locale */
static unsigned char get_decimal_point(void)
{
#ifdef ENABLE_LOCALES
    struct lconv *lconv = localeconv();
    return (unsigned char) lconv->decimal_point[0];
#else
    return '.';
#endif
}

typedef struct
{
    const unsigned char *content;
    size_t length;
    size_t offset;
    size_t depth; /* How deeply nested (in arrays/objects) is the input at the current offset. */
    internal_hooks hooks;
} parse_buffer;

/* check if the given size is left to read in a given parse buffer (starting with 1) */
#define can_read(buffer, size) ((buffer != NULL) && (((buffer)->offset + size) <= (buffer)->length))
/* check if the buffer can be accessed at the given index (starting with 0) */
#define can_access_at_index(buffer, index) ((buffer != NULL) && (((buffer)->offset + index) < (buffer)->length))
#define cannot_access_at_index(buffer, index) (!can_access_at_index(buffer, index))
/* get a pointer to the buffer at the position */
#define buffer_at_offset(buffer) ((buffer)->content + (buffer)->offset)

/* Parse the input text to generate a number, and populate the result into item. */
static cJSON_bool parse_number(cJSON * const item, parse_buffer * const input_buffer)
{
    double number = 0;
    unsigned char *after_end = NULL;
    unsigned char *number_c_string;
    unsigned char decimal_point = get_decimal_point();
    size_t i = 0;
    size_t number_string_length = 0;
    cJSON_bool has_decimal_point = false;

    if ((input_buffer == NULL) || (input_buffer->content == NULL))
    {
        return false;
    }

    /* copy the number into a temporary buffer and replace '.' with the decimal point
     * of the current locale (for strtod)
     * This also takes care of '\0' not necessarily being available for marking the end of the input */
    for (i = 0; can_access_at_index(input_buffer, i); i++)
    {
        switch (buffer_at_offset(input_buffer)[i])
        {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '+':
            case '-':
            case 'e':
            case 'E':
                number_string_length++;
                break;

            case '.':
                number_string_length++;
                has_decimal_point = true;
                break;

            default:
                goto loop_end;
        }
    }
loop_end:
    /* malloc for temporary buffer, add 1 for '\0' */
    number_c_string = (unsigned char *) input_buffer->hooks.allocate(number_string_length + 1);
    if (number_c_string == NULL)
    {
        return false; /* allocation failure */
    }

    memcpy(number_c_string, buffer_at_offset(input_buffer), number_string_length);
    number_c_string[number_string_length] = '\0';

    if (has_decimal_point)
    {
        for (i = 0; i < number_string_length; i++)
        {
            if (number_c_string[i] == '.')
            {
                /* replace '.' with the decimal point of the current locale (for strtod) */
                number_c_string[i] = decimal_point;
            }
        }
    }

    number = strtod((const char*)number_c_string, (char**)&after_end);
    if (number_c_string == after_end)
    {
        /* free the temporary buffer */
        input_buffer->hooks.deallocate(number_c_string);
        return false; /* parse_error */
    }

    item->valuedouble = number;

    /* use saturation in case of overflow */
    if (number >= INT_MAX)
    {
        item->valueint = INT_MAX;
    }
    else if (number <= (double)INT_MIN)
    {
        item->valueint = INT_MIN;
    }
    else
    {
        item->valueint = (int)number;
    }

    item->type = cJSON_Number;

    input_buffer->offset += (size_t)(after_end - number_c_string);
    /* free the temporary buffer */
    input_buffer->hooks.deallocate(number_c_string);
    return true;
}

/* don't ask me, but the original cJSON_SetNumberValue returns an integer or double */
CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number)
{
    if (number >= INT_MAX)
    {
        object->valueint = INT_MAX;
    }
    else if (number <= (double)INT_MIN)
    {
        object->valueint = INT_MIN;
    }
    else
    {
        object->valueint = (int)number;
    }

    return object->valuedouble = number;
}

/* Note: when passing a NULL valuestring, cJSON_SetValuestring treats this as an error and return NULL */
CJSON_PUBLIC(char*) cJSON_SetValuestring(cJSON *object, const char *valuestring)
{
    char *copy = NULL;
    size_t v1_len;
    size_t v2_len;
    /* if object's type is not cJSON_String or is cJSON_IsReference, it should not set valuestring */
    if ((object == NULL) || !(object->type & cJSON_String) || (object->type & cJSON_IsReference))
    {
        return NULL;
    }
    /* return NULL if the object is corrupted or valuestring is NULL */
    if (object->valuestring == NULL || valuestring == NULL)
    {
        return NULL;
    }

    v1_len = strlen(valuestring);
    v2_len = strlen(object->valuestring);

    if (v1_len <= v2_len)
    {
        /* strcpy does not handle overlapping string: [X1, X2] [Y1, Y2] => X2 < Y1 or Y2 < X1 */
        if (!( valuestring + v1_len < object->valuestring || object->valuestring + v2_len < valuestring ))
        {
            return NULL;
        }
        strcpy(object->valuestring, valuestring);
        return object->valuestring;
    }
    copy = (char*) cJSON_strdup((const unsigned char*)valuestring, &global_hooks);
    if (copy == NULL)
    {
        return NULL;
    }
    if (object->valuestring != NULL)
    {
        cJSON_free(object->valuestring);
    }
    object->valuestring = copy;

    return copy;
}

typedef struct
{
    unsigned char *buffer;
    size_t length;
    size_t offset;
    size_t depth; /* current nesting depth (for formatted printing) */
    cJSON_bool noalloc;
    cJSON_bool format; /* is this print a formatted print */
    internal_hooks hooks;
} printbuffer;

/* realloc printbuffer if necessary to have at least "needed" bytes more */
static unsigned char* ensure(printbuffer * const p, size_t needed)
{
    unsigned char *newbuffer = NULL;
    size_t newsize = 0;

    if ((p == NULL) || (p->buffer == NULL))
    {
        return NULL;
    }

    if ((p->length > 0) && (p->offset >= p->length))
    {
        /* make sure that offset is valid */
        return NULL;
    }

    if (needed > INT_MAX)
    {
        /* sizes bigger than INT_MAX are currently not supported */
        return NULL;
    }

    needed += p->offset + 1;
    if (needed <= p->length)
    {
        return p->buffer + p->offset;
    }

    if (p->noalloc) {
        return NULL;
    }

    /* calculate new buffer size */
    if (needed > (INT_MAX / 2))
    {
        /* overflow of int, use INT_MAX if possible */
        if (needed <= INT_MAX)
        {
            newsize = INT_MAX;
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        newsize = needed * 2;
    }

    if (p->hooks.reallocate != NULL)
    {
        /* reallocate with realloc if available */
        newbuffer = (unsigned char*)p->hooks.reallocate(p->buffer, newsize);
        if (newbuffer == NULL)
        {
            p->hooks.deallocate(p->buffer);
            p->length = 0;
            p->buffer = NULL;

            return NULL;
        }
    }
    else
    {
        /* otherwise reallocate manually */
        newbuffer = (unsigned char*)p->hooks.allocate(newsize);
        if (!newbuffer)
        {
            p->hooks.deallocate(p->buffer);
            p->length = 0;
            p->buffer = NULL;

            return NULL;
        }

        memcpy(newbuffer, p->buffer, p->offset + 1);
        p->hooks.deallocate(p->buffer);
    }
    p->length = newsize;
    p->buffer = newbuffer;

    return newbuffer + p->offset;
}

/* calculate the new length of the string in a printbuffer and update the offset */
static void update_offset(printbuffer * const buffer)
{
    const unsigned char *buffer_pointer = NULL;
    if ((buffer == NULL) || (buffer->buffer == NULL))
    {
        return;
    }
    buffer_pointer = buffer->buffer + buffer->offset;

    buffer->offset += strlen((const char*)buffer_pointer);
}

/* securely comparison of floating-point variables */
static cJSON_bool compare_double(double a, double b)
{
    double maxVal = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return (fabs(a - b) <= maxVal * DBL_EPSILON);
}

/* Render the number nicely from the given item into a string. */
static cJSON_bool print_number(const cJSON * const item, printbuffer * const output_buffer)
{
    unsigned char *output_pointer = NULL;
    double d = item->valuedouble;
    int length = 0;
    size_t i = 0;
    unsigned char number_buffer[26] = {0}; /* temporary buffer to print the number into */
    unsigned char decimal_point = get_decimal_point();
    double test = 0.0;

    if (output_buffer == NULL)
    {
        return false;
    }

    /* This checks for NaN and Infinity */
    if (isnan(d) || isinf(d))
    {
        length = sprintf((char*)number_buffer, "null");
    }
    else if(d == (double)item->valueint)
    {
        length = sprintf((char*)number_buffer, "%d", item->valueint);
    }
    else
    {
        /* Try 15 decimal places of precision to avoid nonsignificant nonzero digits */
        length = sprintf((char*)number_buffer, "%1.15g", d);

        /* Check whether the original double can be recovered */
        if ((sscanf((char*)number_buffer, "%lg", &test) != 1) || !compare_double((double)test, d))
        {
            /* If not, print with 17 decimal places of precision */
            length = sprintf((char*)number_buffer, "%1.17g", d);
        }
    }

    /* sprintf failed or buffer overrun occurred */
    if ((length < 0) || (length > (int)(sizeof(number_buffer) - 1)))
    {
        return false;
    }

    /* reserve appropriate space in the output */
    output_pointer = ensure(output_buffer, (size_t)length + sizeof(""));
    if (output_pointer == NULL)
    {
        return false;
    }

    /* copy the printed number to the output and replace locale
     * dependent decimal point with '.' */
    for (i = 0; i < ((size_t)length); i++)
    {
        if (number_buffer[i] == decimal_point)
        {
            output_pointer[i] = '.';
            continue;
        }

        output_pointer[i] = number_buffer[i];
    }
    output_pointer[i] = '\0';

    output_buffer->offset += (size_t)length;

    return true;
}

/* parse 4 digit hexadecimal number */
static unsigned parse_hex4(const unsigned char * const input)
{
    unsigned int h = 0;
    size_t i = 0;

    for (i = 0; i < 4; i++)
    {
        /* parse digit */
        if ((input[i] >= '0') && (input[i] <= '9'))
        {
            h += (unsigned int) input[i] - '0';
        }
        else if ((input[i] >= 'A') && (input[i] <= 'F'))
        {
            h += (unsigned int) 10 + input[i] - 'A';
        }
        else if ((input[i] >= 'a') && (input[i] <= 'f'))
        {
            h += (unsigned int) 10 + input[i] - 'a';
        }
        else /* invalid */
        {
            return 0;
        }

        if (i < 3)
        {
            /* shift left to make place for the next nibble */
            h = h << 4;
        }
    }

    return h;
}

/* converts a UTF-16 literal to UTF-8
 * A literal can be one or two sequences of the form \uXXXX */
static unsigned char utf16_literal_to_utf8(const unsigned char * const input_pointer, const unsigned char * const input_end, unsigned char **output_pointer)
{
    long unsigned int codepoint = 0;
    unsigned int first_code = 0;
    const unsigned char *first_sequence = input_pointer;
    unsigned char utf8_length = 0;
    unsigned char utf8_position = 0;
    unsigned char sequence_length = 0;
    unsigned char first_byte_mark = 0;

    if ((input_end - first_sequence) < 6)
    {
        /* input ends unexpectedly */
        goto fail;
    }

    /* get the first utf16 sequence */
    first_code = parse_hex4(first_sequence + 2);

    /* check that the code is valid */
    if (((first_code >= 0xDC00) && (first_code <= 0xDFFF)))
    {
        goto fail;
    }

    /* UTF16 surrogate pair */
    if ((first_code >= 0xD800) && (first_code <= 0xDBFF))
    {
        const unsigned char *second_sequence = first_sequence + 6;
        unsigned int second_code = 0;
        sequence_length = 12; /* \uXXXX\uXXXX */

        if ((input_end - second_sequence) < 6)
        {
            /* input ends unexpectedly */
            goto fail;
        }

        if ((second_sequence[0] != '\\') || (second_sequence[1] != 'u'))
        {
            /* missing second half of the surrogate pair */
            goto fail;
        }

        /* get the second utf16 sequence */
        second_code = parse_hex4(second_sequence + 2);
        /* check that the code is valid */
        if ((second_code < 0xDC00) || (second_code > 0xDFFF))
        {
            /* invalid second half of the surrogate pair */
            goto fail;
        }


        /* calculate the unicode codepoint from the surrogate pair */
        codepoint = 0x10000 + (((first_code & 0x3FF) << 10) | (second_code & 0x3FF));
    }
    else
    {
        sequence_length = 6; /* \uXXXX */
        codepoint = first_code;
    }

    /* encode as UTF-8
     * takes at maximum 4 bytes to encode:
     * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    if (codepoint < 0x80)
    {
        /* normal ascii, encoding 0xxxxxxx */
        utf8_length = 1;
    }
    else if (codepoint < 0x800)
    {
        /* two bytes, encoding 110xxxxx 10xxxxxx */
        utf8_length = 2;
        first_byte_mark = 0xC0; /* 11000000 */
    }
    else if (codepoint < 0x10000)
    {
        /* three bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx */
        utf8_length = 3;
        first_byte_mark = 0xE0; /* 11100000 */
    }
    else if (codepoint <= 0x10FFFF)
    {
        /* four bytes, encoding 1110xxxx 10xxxxxx 10xxxxxx 10xxxxxx */
        utf8_length = 4;
        first_byte_mark = 0xF0; /* 11110000 */
    }
    else
    {
        /* invalid unicode codepoint */
        goto fail;
    }

    /* encode as utf8 */
    for (utf8_position = (unsigned char)(utf8_length - 1); utf8_position > 0; utf8_position--)
    {
        /* 10xxxxxx */
        (*output_pointer)[utf8_position] = (unsigned char)((codepoint | 0x80) & 0xBF);
        codepoint >>= 6;
    }
    /* encode first byte */
    if (utf8_length > 1)
    {
        (*output_pointer)[0] = (unsigned char)((codepoint | first_byte_mark) & 0xFF);
    }
    else
    {
        (*output_pointer)[0] = (unsigned char)(codepoint & 0x7F);
    }

    *output_pointer += utf8_length;

    return sequence_length;

fail:
    return 0;
}

/* Parse the input text into an unescaped cinput, and populate item. */
static cJSON_bool parse_string(cJSON * const item, parse_buffer * const input_buffer)
{
    const unsigned char *input_pointer = buffer_at_offset(input_buffer) + 1;
    const unsigned char *input_end = buffer_at_offset(input_buffer) + 1;
    unsigned char *output_pointer = NULL;
    unsigned char *output = NULL;

    /* not a string */
    if (buffer_at_offset(input_buffer)[0] != '\"')
    {
        goto fail;
    }

    {
        /* calculate approximate size of the output (overestimate) */
        size_t allocation_length = 0;
        size_t skipped_bytes = 0;
        while (((size_t)(input_end - input_buffer->content) < input_buffer->length) && (*input_end != '\"'))
        {
            /* is escape sequence */
            if (input_end[0] == '\\')
            {
                if ((size_t)(input_end + 1 - input_buffer->content) >= input_buffer->length)
                {
                    /* prevent buffer overflow when last input character is a backslash */
                    goto fail;
                }
                skipped_bytes++;
                input_end++;
            }
            input_end++;
        }
        if (((size_t)(input_end - input_buffer->content) >= input_buffer->length) || (*input_end != '\"'))
        {
            goto fail; /* string ended unexpectedly */
        }

        /* This is at most how much we need for the output */
        allocation_length = (size_t) (input_end - buffer_at_offset(input_buffer)) - skipped_bytes;
        output = (unsigned char*)input_buffer->hooks.allocate(allocation_length + sizeof(""));
        if (output == NULL)
        {
            goto fail; /* allocation failure */
        }
    }

    output_pointer = output;
    /* loop through the string literal */
    while (input_pointer < input_end)
    {
        if (*input_pointer != '\\')
        {
            *output_pointer++ = *input_pointer++;
        }
        /* escape sequence */
        else
        {
            unsigned char sequence_length = 2;
            if ((input_end - input_pointer) < 1)
            {
                goto fail;
            }

            switch (input_pointer[1])
            {
                case 'b':
                    *output_pointer++ = '\b';
                    break;
                case 'f':
                    *output_pointer++ = '\f';
                    break;
                case 'n':
                    *output_pointer++ = '\n';
                    break;
                case 'r':
                    *output_pointer++ = '\r';
                    break;
                case 't':
                    *output_pointer++ = '\t';
                    break;
                case '\"':
                case '\\':
                case '/':
                    *output_pointer++ = input_pointer[1];
                    break;

                /* UTF-16 literal */
                case 'u':
                    sequence_length = utf16_literal_to_utf8(input_pointer, input_end, &output_pointer);
                    if (sequence_length == 0)
                    {
                        /* failed to convert UTF16-literal to UTF-8 */
                        goto fail;
                    }
                    break;

                default:
                    goto fail;
            }
            input_pointer += sequence_length;
        }
    }

    /* zero terminate the output */
    *output_pointer = '\0';

    item->type = cJSON_String;
    item->valuestring = (char*)output;

    input_buffer->offset = (size_t) (input_end - input_buffer->content);
    input_buffer->offset++;

    return true;

fail:
    if (output != NULL)
    {
        input_buffer->hooks.deallocate(output);
        output = NULL;
    }

    if (input_pointer != NULL)
    {
        input_buffer->offset = (size_t)(input_pointer - input_buffer->content);
    }

    return false;
}

/* Render the cstring provided to an escaped version that can be printed. */
static cJSON_bool print_string_ptr(const unsigned char * const input, printbuffer * const output_buffer)
{
    const unsigned char *input_pointer = NULL;
    unsigned char *output = NULL;
    unsigned char *output_pointer = NULL;
    size_t output_length = 0;
    /* numbers of additional characters needed for escaping */
    size_t escape_characters = 0;

    if (output_buffer == NULL)
    {
        return false;
    }

    /* empty string */
    if (input == NULL)
    {
        output = ensure(output_buffer, sizeof("\"\""));
        if (output == NULL)
        {
            return false;
        }
        strcpy((char*)output, "\"\"");

        return true;
    }

    /* set "flag" to 1 if something needs to be escaped */
    for (input_pointer = input; *input_pointer; input_pointer++)
    {
        switch (*input_pointer)
        {
            case '\"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                /* one character escape sequence */
                escape_characters++;
                break;
            default:
                if (*input_pointer < 32)
                {
                    /* UTF-16 escape sequence uXXXX */
                    escape_characters += 5;
                }
                break;
        }
    }
    output_length = (size_t)(input_pointer - input) + escape_characters;

    output = ensure(output_buffer, output_length + sizeof("\"\""));
    if (output == NULL)
    {
        return false;
    }

    /* no characters have to be escaped */
    if (escape_characters == 0)
    {
        output[0] = '\"';
        memcpy(output + 1, input, output_length);
        output[output_length + 1] = '\"';
        output[output_length + 2] = '\0';

        return true;
    }

    output[0] = '\"';
    output_pointer = output + 1;
    /* copy the string */
    for (input_pointer = input; *input_pointer != '\0'; (void)input_pointer++, output_pointer++)
    {
        if ((*input_pointer > 31) && (*input_pointer != '\"') && (*input_pointer != '\\'))
        {
            /* normal character, copy */
            *output_pointer = *input_pointer;
        }
        else
        {
            /* character needs to be escaped */
            *output_pointer++ = '\\';
            switch (*input_pointer)
            {
                case '\\':
                    *output_pointer = '\\';
                    break;
                case '\"':
                    *output_pointer = '\"';
                    break;
                case '\b':
                    *output_pointer = 'b';
                    break;
                case '\f':
                    *output_pointer = 'f';
                    break;
                case '\n':
                    *output_pointer = 'n';
                    break;
                case '\r':
                    *output_pointer = 'r';
                    break;
                case '\t':
                    *output_pointer = 't';
                    break;
                default:
                    /* escape and print as unicode codepoint */
                    sprintf((char*)output_pointer, "u%04x", *input_pointer);
                    output_pointer += 4;
                    break;
            }
        }
    }
    output[output_length + 1] = '\"';
    output[output_length + 2] = '\0';

    return true;
}

/* Invoke print_string_ptr (which is useful) on an item. */
static cJSON_bool print_string(const cJSON * const item, printbuffer * const p)
{
    return print_string_ptr((unsigned char*)item->valuestring, p);
}

/* Predeclare these prototypes. */
static cJSON_bool parse_value(cJSON * const item, parse_buffer * const input_buffer);
static cJSON_bool print_value(const cJSON * const item, printbuffer * const output_buffer);
static cJSON_bool parse_array(cJSON * const item, parse_buffer * const input_buffer);
static cJSON_bool print_array(const cJSON * const item, printbuffer * const output_buffer);
static cJSON_bool parse_object(cJSON * const item, parse_buffer * const input_buffer);
static cJSON_bool print_object(const cJSON * const item, printbuffer * const output_buffer);

/* Utility to jump whitespace and cr/lf */
static parse_buffer *buffer_skip_whitespace(parse_buffer * const buffer)
{
    if ((buffer == NULL) || (buffer->content == NULL))
    {
        return NULL;
    }

    if (cannot_access_at_index(buffer, 0))
    {
        return buffer;
    }

    while (can_access_at_index(buffer, 0) && (buffer_at_offset(buffer)[0] <= 32))
    {
       buffer->offset++;
    }

    if (buffer->offset == buffer->length)
    {
        buffer->offset--;
    }

    return buffer;
}

/* skip the UTF-8 BOM (byte order mark) if it is at the beginning of a buffer */
static parse_buffer *skip_utf8_bom(parse_buffer * const buffer)
{
    if ((buffer == NULL) || (buffer->content == NULL) || (buffer->offset != 0))
    {
        return NULL;
    }

    if (can_access_at_index(buffer, 4) && (strncmp((const char*)buffer_at_offset(buffer), "\xEF\xBB\xBF", 3) == 0))
    {
        buffer->offset += 3;
    }

    return buffer;
}

/* 简单介绍：带选项的JSON解析入口函数，自动计算输入长度并调用底层解析函数 
   参数值：指向待解析的JSON字符串（以'\0'结尾）
   return_parse_end是输出参数，返回解析结束位置的指针，可用于检查后续垃圾数据
   require_null_terminated表示是否要求输入必须严格以'\0'终止
   cJSON *指的是return cJSON*——成功则只想解析出的根节点，失败则返回NULL 
   主要流程：①空指针检查，防止非法输入
            ②计算缓冲区长度，包含'\0'终止符，确保严格模式下的边界检查
            ③调用cJSON_ParseWithLengthOpts，委托底层实现完成解析
    存储器内存管理：无直接分配：仅计算长度并转发调用，内存分配由底层函数负责
                  生命周期：返回的cJSON节点由调用者负责释放（用cJSON_Delete） */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated)
{
    /* 声明一个名为buffer_length的变量，其数据类型为size_t。
    size_t是一种无符号整数类型。它后续会用于存储相关缓冲区的长度等计算结果 */
    size_t buffer_length; 

    if (NULL == value)
    {
        return NULL; /* 输入字符串为空指针，直接返回失败 */
    }

    /* 计算缓冲区长度，strlen(value)是有效字符数，+1包含'\0'终止符 
       严格模式下需要检查'\0'，所以必须包含终止符的长度 */
    /* Adding null character size due to require_null_terminated. */
    buffer_length = strlen(value) + sizeof("");

    return cJSON_ParseWithLengthOpts(value, buffer_length, return_parse_end, require_null_terminated);
}

/* 简单介绍：底层核心cJSON解析函数，支持指定缓冲区长度、解析结束位置和严格模式
   参数值：指向待解析的JSON字符串（不一定以'\0'结尾）
   buffer_length：缓冲区总长度，包含所有待解析字符，不含'\0'时需要注意
   return_parse_end：输出参数，返回解析结束位置的指针，可用于检查后续垃圾数据
   require_null_terminated表示是否要求输入必须严格以'\0'终止
   cJSON *指的是return cJSON*——成功则只想解析出的根节点，失败则返回NULL 
   主要流程：①初始化解析缓冲区，绑定输入字符串、长度、偏移量、内存钩子
            ②空输入检查，防止空字符串或零长度缓冲区
            ③分配根节点，作为解析结果的容器
            ④调用parse_value，递归解析所有JSON类型（包括对象/数组/字符串/数字等）
            ⑤严格模式检查，验证'\0'终止符，过滤垃圾数据
            ⑥设置解析结束位置，输出给调用者
            ⑦失败处理，释放已分配内存，设置全局错误信息
    存储器内存管理：分配：cJSON_New_Item 分配根节点（sizeof(cJSON)），parse_value 递归分配子节点
                  释放：解析失败时通过 cJSON_Delete 释放整个节点树，避免内存泄漏
                  生命周期：返回的根节点由调用者负责释放（cJSON_Delete），失败时函数内部完成清理*/
/* Parse an object - create a new root, and populate. */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated)
{
    parse_buffer buffer = { 0, 0, 0, 0, { 0, 0, 0 } }; /* 初始化解析缓冲区，清零所有字段 */
    cJSON *item = NULL; /* 根节点指针，最终返回解析结果 */

    /* 重置全局错误状态，每次解析前清空之前的错误信息 */
    /* reset error position */
    global_error.json = NULL;
    global_error.position = 0;

    /* 空输入检查：输入字符串为空或缓冲区长度为0，直接返回失败 */
    if (value == NULL || 0 == buffer_length)
    {
        goto fail; /* 跳转到失败处理标签 */
    }

    /* 初始化解析缓冲区，绑定输入字符串、长度、偏移量和内存钩子 */
    buffer.content = (const unsigned char*)value; /* 输入字符串转为无符号字符指针（处理UTF-8） */
    buffer.length = buffer_length; /* 缓冲区总长度 */
    buffer.offset = 0; /* 初始解析偏移量，从字符串开头开始 */
    buffer.hooks = global_hooks; /* 使用全局内存分配钩子，默认malloc//free */

    /* 分配根节点，作为解析结果的容器 */
    item = cJSON_New_Item(&global_hooks);
    if (item == NULL) /* memory fail   内存分配失败（堆耗尽），直接返回失败 */
    {
        goto fail;
    }

    /* 核心解析逻辑：调用 parse_value 递归解析所有 JSON 类型 */
    /* parse_value 会根据第一个字符判断类型（'{'指对象，'['指数组，'"'指字符串等） */
    /* skip_utf8_bom：跳过UTF-8 BOM头（\xEF\xBB\xBF），兼容带BOM的JSON文件 */
    if (!parse_value(item, buffer_skip_whitespace(skip_utf8_bom(&buffer))))
    {
        /* parse_value返回false：解析失败，错误信息已通过global_error记录 */
        /* parse failure. ep is set. */
        goto fail;
    }

    /* 严格模式检查：如果要求'\0'终止，跳过空白后检查是否为'\0' */
    /* 过滤JSON后的垃圾数据（如 "{...} garbage"），确保解析结果纯净 */
    /* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
    if (require_null_terminated)
    {
        buffer_skip_whitespace(&buffer); /* 跳过解析结束后的所有空白字符 */
        /* 检查是否越界或当前字符不是'\0'，存在垃圾数据，解析失败 */
        if ((buffer.offset >= buffer.length) || buffer_at_offset(&buffer)[0] != '\0')
        {
            goto fail;
        }
    }
    
    /* 设置解析结束位置：输出的调用者，用于检查解析范围 */
    if (return_parse_end)
    {
        *return_parse_end = (const char*)buffer_at_offset(&buffer); /* 返回当前偏移量对应的字符指针 */
    }

    return item; /* 解析成功，返回根节点指针 */

/* 失败处理标签：集中释放内存，设置错误信息，避免重复代码 */
fail:
    /* 释放已分配的根节点：cJSON_Delete会递归释放所有子节点，防止内存泄漏 */
    if (item != NULL)
    {
        cJSON_Delete(item);
    }

    /* 设置局部错误信息：用于返回给调用者，替代全局错误，线程更安全 */
    if (value != NULL)
    {
        /* 声明了一个local_error变量，其类型error类型 ，用于存储与局部错误相关的信息*/
        error local_error;
        /* 复制错误信息到局部变量，避免全局状态被覆盖 */
        local_error.json = (const unsigned char*)value;
        local_error.position = 0;

        /* 计算错误位置：如果解析过程中偏移量未到缓冲区末尾，错误位置为当前偏移量 */
        if (buffer.offset < buffer.length)
        {
            local_error.position = buffer.offset;
        }
        /* 如果缓冲区长度大于0，错误位置为最后一个字符（边界错误） */
        else if (buffer.length > 0)
        {
            local_error.position = buffer.length - 1;
        }

        /* 如果需要返回解析结束位置，设置为错误位置 */
        if (return_parse_end != NULL)
        {
            *return_parse_end = (const char*)local_error.json + local_error.position;
        }

        /* 更新全局错误信息：供上层调用者检查 */
        global_error = local_error;
    }

    return NULL; /* 解析失败，返回NULL */
}

/* 简单介绍：默认选项的JSON解析函数，对外暴露的简化接口 
   参数值：指向待解析的JSON字符串（以'\0'结尾）
   cJSON *指的是return cJSON*——成功则只想解析出的根节点，失败则返回NULL 
   主要流程：①调用cJSON_ParseWithOpts，使用默认参数，不返回解析结束位置，不要求严格终止
   存储器内存管理：无直接分配：委托底层函数处理
                 生命周期：返回的cJSON节点有调用者负责释放（用cJSON_Delete） */
/* Default options for cJSON_Parse */
CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value)
{
    return cJSON_ParseWithOpts(value, 0, 0);  /* 默认参数：return_parse_end=NULL, require_null_terminated=0 */ 
}

/* 简单介绍：指定长度的JSON解析函数，对外暴露的简化接口
   参数值：指向待解析的JSON字符串（不一定以'\0'结尾）
   buffer_length：缓冲区总长度，包含所有的待解析字符
   cJSON *指的是return cJSON*——成功则只想解析出的根节点，失败则返回NULL 
   主要流程：①调用cJSON_ParseWithOpts，使用默认参数，不返回解析结束位置，不要求严格终止
   存储器内存管理：无直接分配：委托底层函数处理
                 生命周期：返回的cJSON节点有调用者负责释放（用cJSON_Delete） */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length)
{
    return cJSON_ParseWithLengthOpts(value, buffer_length, 0, 0); /* 默认参数：return_parse_end=NULL, require_null_terminated=0 */
}

#define cjson_min(a, b) (((a) < (b)) ? (a) : (b))

/* 简单介绍：底层核心JSON渲染函数，负责分配缓冲区并调用print_value生成JSON文本
   参数值：指向待渲染的cJSON根节点
   format表示是否启用格式化（true表带缩进换行，false表紧凑无空白）
   hooks是内存操作钩子（allocate/reallocate/deallocate)，用于管理缓冲区内存
   unsigned char *表示：成功则指向生成的JSON字符串，失败则返回NULL
   主要流程：①初始化缓冲区上下文，清零缓冲区结构体，设置默认大小256字节
            ②分配初始缓存区，使用hooks->allocate分配默认大小的内存
            ③调用print_value，递归渲染所有节点到缓冲区
            ④处理缓冲区扩容，支持reallocate时直接扩容，否则复制到新缓冲区
            ⑤终止符处理，手动添加'\0'，确保生成合法C字符串
            ⑥失败处理，释放所有已分配内存，避免泄露
    存储器内存管理：初始分配 256字节缓冲区，不足时通过reallocate或新分配扩容
                  释放：失败时释放所有已分配内存；成功后返回的字符串由调用者释放
                  边界安全：确保缓冲区大小至少为offset + 1，为'\0'终止符预留空间 */
static unsigned char *print(const cJSON * const item, cJSON_bool format, const internal_hooks * const hooks)
{
    /* 默认缓冲区大小256字节，适合大多数小型JSON，减小重分配次数 */
    static const size_t default_buffer_size = 256;
    /* 打进缓冲区上下文，存储缓冲区指针、长度、偏移量、格式化和内存钩子 */
    printbuffer buffer[1];
    /* 最终返回的JSON字符串指针 */
    unsigned char *printed = NULL;

    /* 清零缓冲区上下文，确保所有字段初始为0，避免未初始化值导致的错误 */
    memset(buffer, 0, sizeof(buffer));

    /* 分配初始缓存区，使用内存钩子分配默认大小的内存 */
    /* create buffer */
    buffer->buffer = (unsigned char*) hooks->allocate(default_buffer_size);
    buffer->length = default_buffer_size; /* 缓冲区总长度 */
    buffer->format = format; /* 格式化标志 */
    buffer->hooks = *hooks; /* 绑定内存钩子 */
    
    /* 检查分配是否成功，内存不足时直接跳转到失败处理 */
    if (buffer->buffer == NULL)
    {
        goto fail;
    }

    /* 核心渲染逻辑：递归渲染所有cJSON节点到缓冲区 */
    /* print the value */
    if (!print_value(item, buffer))
    {
        goto fail; /* 渲染失败，跳转到失败处理 */
    }
    
    /* 更新缓冲区偏移量，确保偏移量正确反映已写入的字节数 */
    update_offset(buffer);

    /* 检查是否支持reallocate，支持则直接扩容，避免内存复制 */
    /* check if reallocate is available */
    if (hooks->reallocate != NULL)
    {
        /* 扩容缓冲区，大小为当前偏移量+1（为'\0'预留空间） */
        printed = (unsigned char*) hooks->reallocate(buffer->buffer, buffer->offset + 1);
        if (printed == NULL) {
            goto fail; /* 扩容失败，跳转到失败处理 */
        }
        buffer->buffer = NULL;
    }
    else /* otherwise copy the JSON over to a new buffer   不支持reallocate，复制到新缓存区 */
    {
        /* 分配新缓冲区，大小为当前偏移量+1（为'\0'预留空间） */
        printed = (unsigned char*) hooks->allocate(buffer->offset + 1);
        if (printed == NULL)
        {
            goto fail; /* 分配失败，跳转到失败处理 */
        }
        /* 复制数据，仅复制已使用的部分（buffer->offset字节），避免浪费空间 */
        memcpy(printed, buffer->buffer, cjson_min(buffer->length, buffer->offset + 1));
        /* 手动添加'\0'，确保生成的字符串时合法的C字符串 */
        printed[buffer->offset] = '\0'; /* just to be sure */

        /* free the buffer   释放旧缓存区，避免内存泄漏 */
        hooks->deallocate(buffer->buffer);
        buffer->buffer = NULL;
    }

    /* 渲染成功，返回生成的JSON字符串 */
    return printed;

/* 失败处理标签：集中释放所有已分配的内存，避免内存泄漏 */
fail:
    /* 释放初始缓冲区，如果已分配且未移交 */
    if (buffer->buffer != NULL)
    {
        hooks->deallocate(buffer->buffer);
        buffer->buffer = NULL;
    }

    /* 释放最终字符串，如果已分配 */
    if (printed != NULL)
    {
        hooks->deallocate(printed);
        printed = NULL;
    }

    /* 渲染失败，返回NULL */
    return NULL;
}

/* 简单介绍：将cJSON结构渲染为格式化的JSON文本，带缩进和换行，便于阅读 
   参数值：指向待渲染的cJSON根节点
   char*表示成功则指向格式化JSON字符串的指针；失败则返回NULL
   主要流程：①调用底层print函数，启用格式化（fmt=true），使用全局内存钩子
            ②返回转换后的字符串指针
    存储器内存管理：分配：底层print函数通过global_hooks.allocate动态分配输出缓冲区
                  释放：返回的字符串由调用者负责释放（使用cJSON_free或对应钩子的deallocate）
                  生命周期：返回的字符串与cJSON节点无关，需单独释放 */
/* Render a cJSON item/entity/structure to text. */
CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item)
{
    /* 委托底层print函数，启用格式化，使用全局内存钩子 */
    return (char*)print(item, true, &global_hooks);
}

/* 简单介绍：将ccJSON结构渲染为无格式的JSON文本，紧凑无空白，节省空间 
   item指向待渲染的cJSON根节点
   char*表示成功则指向格式化JSON字符串的指针；失败则返回NULL
   主要流程：①调用底层print函数，禁用格式化（fmt=true），使用全局内存钩子
            ②返回转换后的字符串指针
    存储器内存管理：分配：底层print函数通过global_hooks.allocate动态分配输出缓冲区
                  释放：返回的字符串由调用者负责释放（使用cJSON_free或对应钩子的deallocate）
                  生命周期：返回的字符串与cJSON节点无关，需单独释放 */
CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item)
{
    /* 委托底层print函数，禁用格式化，使用全局内存钩子 */
    return (char*)print(item, false, &global_hooks);
}

/* 简单介绍：适用预分配缓冲区的JSON渲染函数，可控制初始缓冲区大小，减少重分配
   item指向待渲染的cJSON根节点
   prebuffer表示初始缓冲区大小（字节），小于0时直接返回失败
   fmt表示是否启用格式化，true表带缩进，false表紧凑
   char*表示成功则指向格式化JSON字符串的指针；失败则返回NULL
   主要流程：①初始化printbuffer结构体，清零所有字段，准备渲染上下文
            ②预分配缓冲区，使用global_hooks.allocate分配prebuffer大小的内存
            ③调用print_value，递归渲染所有节点到缓冲区
            ④失败处理，释放预分配缓存区，返回NULL
    存储器内存管理：分配：使用global_hooks.allocate分配prebuffer大小的初始缓冲区
                  重分配：print_value 程中若缓冲区不足，会通过钩子reallocate扩容
                  释放：返回的字符串由调用者负责释放（使用cJSON_free或对应钩子的deallocate）
                  失败时：函数内部释放已分配的缓冲区，避免内存泄漏 */
CJSON_PUBLIC(char *) cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt)
{
    /* 初始化打印缓冲区，清零所有字段，构建渲染上下文 */
    printbuffer p = { 0, 0, 0, 0, 0, 0, { 0, 0, 0 } };

    /* 合法性检查，预分配缓冲区大小不能为负数 */
    if (prebuffer < 0)
    {
        return NULL; /* 非法缓冲区大小，直接返回失败 */
    }

    /* 预分配输出缓冲区，使用全局钩子分配prebuffer字节的内存 */
    p.buffer = (unsigned char*)global_hooks.allocate((size_t)prebuffer);
    if (!p.buffer)
    {
        return NULL; /* 内存分配失败，直接返回失败 */
    }

    /* 初始化缓冲区参数 */
    p.length = (size_t)prebuffer; /* 缓冲区总长度 */
    p.offset = 0; /* 初始写入偏移量，从缓冲区开头开始 */
    p.noalloc = false; /* 允许重分配，缓冲区不足时自动扩容 */
    p.format = fmt; /* 格式化标志，控制是否带缩进和换行 */
    p.hooks = global_hooks; /* 使用全局内存钩子 */

    /* 核心渲染逻辑：递归渲染所有节点到缓冲区 */
    if (!print_value(item, &p))
    {
        /* 渲染失败，释放预分配的缓冲区，避免内存泄漏 */
        global_hooks.deallocate(p.buffer);
        p.buffer = NULL;
        return NULL;
    }

    /* 渲染成功，返回缓冲区指针（转为char*兼容C字符串） */
    return (char*)p.buffer;
}

/* 简单介绍：使用用户预分配缓冲区的JSON渲染函数（完全由调用者管理内存，无内部分配）
   item指向待渲染的cJSON根节点
   buffer指向用户预分配的输出缓冲区（不能为空）
   length是预分配缓冲区的总长度（字节，必须大于0）
   format表示是否启用格式化，true表带缩进，false表紧凑
   cJSON_bool表示true：渲染成功；false：渲染失败（缓冲区不足/空输入）
   主要流程：①初始化printbuffer结构体，绑定用户提供的缓冲区和长度
            ②合法性检查，缓冲区为空或长度<=0时直接返回失败
            ③调用print_value，递归渲染所有节点到用户缓冲区
            ④返回渲染结果，无需释放内存，缓冲区由调用者管理
   存储器内存管理：无内部分配：完全使用用户提供的缓冲区，不调用任何allocate/reallocate
                 无内部释放：缓冲区由调用者负责分配和释放，函数仅写入数据
                 边界安全：print_value会严格检查缓冲区边界，避免越界写入 */
CJSON_PUBLIC(cJSON_bool) cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const cJSON_bool format)
{
    /* 初始化打印缓冲区，清零所有字段，构建渲染上下文 */
    printbuffer p = { 0, 0, 0, 0, 0, 0, { 0, 0, 0 } };

    /* 合法性检查：用户缓冲区不能为空，长度必须大于0 */
    if ((length < 0) || (buffer == NULL))
    {
        return false; /* 非法缓冲区，直接返回失败 */
    }

    /* 绑定用户提供的缓冲区 */
    p.buffer = (unsigned char*)buffer; /* 绑定用户缓冲区，转为无符号字符指针 */
    p.length = (size_t)length; /* 缓冲区总长度 */
    p.offset = 0; /* 初始写入偏移量，从缓冲区开头开始 */
    p.noalloc = true; /* 禁止重分配，完全使用用户缓冲区，不足时直接失败 */
    p.format = format; /* 格式化标志，控制是否带缩进和换行 */
    p.hooks = global_hooks; /* 使用全局内存钩子，仅用于边界检查，不分配内存 */

    /* 核心渲染逻辑：递归渲染所有节点到用户缓冲区 */
    return print_value(item, &p);
}

/* Parser core - when encountering text, process appropriately. */
static cJSON_bool parse_value(cJSON * const item, parse_buffer * const input_buffer)
{
    if ((input_buffer == NULL) || (input_buffer->content == NULL))
    {
        return false; /* no input */
    }

    /* parse the different types of values */
    /* null */
    if (can_read(input_buffer, 4) && (strncmp((const char*)buffer_at_offset(input_buffer), "null", 4) == 0))
    {
        item->type = cJSON_NULL;
        input_buffer->offset += 4;
        return true;
    }
    /* false */
    if (can_read(input_buffer, 5) && (strncmp((const char*)buffer_at_offset(input_buffer), "false", 5) == 0))
    {
        item->type = cJSON_False;
        input_buffer->offset += 5;
        return true;
    }
    /* true */
    if (can_read(input_buffer, 4) && (strncmp((const char*)buffer_at_offset(input_buffer), "true", 4) == 0))
    {
        item->type = cJSON_True;
        item->valueint = 1;
        input_buffer->offset += 4;
        return true;
    }
    /* string */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '\"'))
    {
        return parse_string(item, input_buffer);
    }
    /* number */
    if (can_access_at_index(input_buffer, 0) && ((buffer_at_offset(input_buffer)[0] == '-') || ((buffer_at_offset(input_buffer)[0] >= '0') && (buffer_at_offset(input_buffer)[0] <= '9'))))
    {
        return parse_number(item, input_buffer);
    }
    /* array */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '['))
    {
        return parse_array(item, input_buffer);
    }
    /* object */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '{'))
    {
        return parse_object(item, input_buffer);
    }

    return false;
}

/* Render a value to text. */
static cJSON_bool print_value(const cJSON * const item, printbuffer * const output_buffer)
{
    unsigned char *output = NULL;

    if ((item == NULL) || (output_buffer == NULL))
    {
        return false;
    }

    switch ((item->type) & 0xFF)
    {
        case cJSON_NULL:
            output = ensure(output_buffer, 5);
            if (output == NULL)
            {
                return false;
            }
            strcpy((char*)output, "null");
            return true;

        case cJSON_False:
            output = ensure(output_buffer, 6);
            if (output == NULL)
            {
                return false;
            }
            strcpy((char*)output, "false");
            return true;

        case cJSON_True:
            output = ensure(output_buffer, 5);
            if (output == NULL)
            {
                return false;
            }
            strcpy((char*)output, "true");
            return true;

        case cJSON_Number:
            return print_number(item, output_buffer);

        case cJSON_Raw:
        {
            size_t raw_length = 0;
            if (item->valuestring == NULL)
            {
                return false;
            }

            raw_length = strlen(item->valuestring) + sizeof("");
            output = ensure(output_buffer, raw_length);
            if (output == NULL)
            {
                return false;
            }
            memcpy(output, item->valuestring, raw_length);
            return true;
        }

        case cJSON_String:
            return print_string(item, output_buffer);

        case cJSON_Array:
            return print_array(item, output_buffer);

        case cJSON_Object:
            return print_object(item, output_buffer);

        default:
            return false;
    }
}

/* 简单介绍：从文本中解析JSON数组（由[]包裹的逗号分隔元素），并构建为cJSON链表结构
   item指向待填充的cJSON根节点，即输出参数，函数会将其标记为数组类型并关联子节点
   input_buffer解析缓冲区，即输入输出参数，包含待解析字符数组、当前偏移量、嵌套深度、内存钩子等
   cJSON_bool表示返回true是解析成功，返回false时解析失败（语法错误/内存不足/嵌套超限）
   主要流程：①嵌套深度检查，防止深层嵌套导致栈溢出
            ②校验数组起始符'['，确认是合法数组开头
            ③处理空数组（[]），直接标记类型并返回
            ④循环解析数组元素：
              a.分配新节点，存储单个数组元素
              b.解析元素值（支持所有JSON类型），填充节点对应字段
              c.逗号分隔符检查，决定是否继续循环
            ⑤校验数组结束符'['，确认数组闭合
            ⑥构建双向循环链表，关联所有数组元素节点 
    存储器内存管理：分配：通过cJSON_New_Item调用input_buffer->hooks->malloc分配单个cJSON节点（大小为sizeof(cJSON)）
                  释放：解析失败时通过cJSON_Delete递归释放已分配的元素链表，避免内存泄漏
                  生命周期：解析成功后，元素链表由外部调用者负责释放；失败时函数内部完成清理 */
/* Build an array from input text. */
static cJSON_bool parse_array(cJSON * const item, parse_buffer * const input_buffer)
{
    /* 数组元素链表头指针，最终赋值给item->child */
    cJSON *head = NULL; /* head of the linked list */
    /* 遍历链表的当前节点指针，始终指向链表尾部 */
    cJSON *current_item = NULL;

    /* 嵌套深度检查：防止恶意深层嵌套JSON导致栈/内存溢出 */
    /* CJSON_NESTING_LIMIT时cJSON定义的最大嵌套层级，默认1000 */
    if (input_buffer->depth >= CJSON_NESTING_LIMIT)
    {
        return false; /* to deeply nested   嵌套层级超限，直接返回失败 */
    }
    input_buffer->depth++; /* 进入数组层级，深度+1（与success处的depth--成对） */

    /* 校验JSON数组起始符'['：必须以'['开头才是合法数组 */
    /* buffer_at_offset：获取管缓冲区当前offset位置的字符 */
    if (buffer_at_offset(input_buffer)[0] != '[')
    {
        /* not an array */
        goto fail; /* 首字母非'['，不是合法数组，跳转到失败处理 */
    }

    input_buffer->offset++; /* 指针偏移，跳过'['，指向后续内容 */
    buffer_skip_whitespace(input_buffer); /* 跳过所有空白字符（空格/制表符/换行等），JSON语法允许任意空白 */
    
    /* 处理空数组（[]）：跳过空白后直接遇到'['，无需解析元素 */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ']'))
    {
        /* empty array */
        goto success; /* 空数组解析完成，跳转到成功处理 */
    }

    /* 边界校验：跳过空白后缓冲区已结束，不完整的数组（如'['），解析失败 */
    /* check if we skipped to the end of the buffer */
    if (cannot_access_at_index(input_buffer, 0))
    {
        input_buffer->offset--; /* 指针回退，恢复到跳过空白之前的位置，保证后续解析上下文正确 */
        goto fail; /* 缓冲区越界，跳转到失败处理 */
    }

    /* 指针回退，为do-while循环做准备，循环会先执行offset++，避免跳过第一个元素 */
    /* step back to character in front of the first element */
    input_buffer->offset--;
    
    /* 循环解析数组元素：do-while保证执行一次，非空数组至少有一个元素 */
    /* 循环终止条件：当前字符非逗号，或缓冲区越界 */
    /* loop through the comma separated array elements */
    do
    {
        /* 分配新cJSON节点，存储单个数组元素 */
        /* cJSON_New_Item：调用input_buffer->hooks->malloc分配sizeof(cJSON)大下的内存。
           内存分配失败会返回NULL，是常见的异常场景 */
        /* allocate next item */
        cJSON *new_item = cJSON_New_Item(&(input_buffer->hooks));
        if (new_item == NULL)
        {
            goto fail; /* allocation failure   内存分配失败，跳转到失败处理，释放已分配节点 */
        }

        /* 将新节点添加到双向链表，维护链表头和尾指针 */
        /* attach next item to list */
        if (head == NULL)
        {
            /* start the linked list   第一个节点，链表头和当前节点都指向新节点 */
            current_item = head = new_item;
        }
        else
        {
            /* 非第一个节点，尾部追加，更新双向链表的prev/next指针 */
            /* add to the end and advance */
            current_item->next = new_item; /* 原尾节点next指向新节点 */
            new_item->prev = current_item; /* 新节点prev指向原尾节点 */
            current_item = new_item; /* 当前节点移动到新尾部 */
        }

        
        /* 解析数组元素值：支持字符串/数字/布尔/数组/对象等所有JSON类型 */
        /* parse next value */
        input_buffer->offset++; /* 指针偏移，跳过逗号，或数组起始后的第一个字符 */
        buffer_skip_whitespace(input_buffer); /* 跳过元素前的空白字符 */
        /* parse_value：万能值解析函数，填充new_item的类型和值字段 */
        if (!parse_value(current_item, input_buffer))
        {
            goto fail; /* failed to parse value   元素值解析失败，跳转到失败处理 */
        }
        buffer_skip_whitespace(input_buffer); /* 跳过元素后的空白字符（准备检查逗号） */
    }
    while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));
    /* 循环终止逻辑：条件一，can_access_at_index，缓冲区未越界
                    条件二，当前字符是逗号，还有下一个数组元素
                    两个条件都满足则继续循环，否则终止（应遇到'['） */

    /* 校验数组结束符'['：循环结束后必须以']'闭合 */
    if (cannot_access_at_index(input_buffer, 0) || buffer_at_offset(input_buffer)[0] != ']')
    {
        goto fail; /* expected end of array   无字符/非']'，数组未闭合，跳转到失败处理 */
    }

/* 解析成功处理标签：集中处理成功逻辑，避免重复代码 */
success:
    input_buffer->depth--; /* 嵌套深度回退，推出数组层级，与开头的depth++成对 */

    /* 构建双向链表，链表头的prev指向链表尾，实现闭环，方便反向通行 */
    if (head != NULL) {
        head->prev = current_item;
    }

    /* 填充根节点，标记类型为数组，关联元素链表 */
    item->type = cJSON_Array; /* 标记根节点为JSON数组类型 */
    item->child = head; /* 根节点child指向数组元素链表头 */

    input_buffer->offset++; /* 指针偏移，跳过']'，准备解析后续内容 */

    return true; /* 解析成功，返回true */

/* 解析失败处理标签：集中释放内存，避免内存泄漏 */
fail:
/* 释放已分配的数组元素链表：cJSON——Delete会递归释放整个链表，包括所有子节点 
   释放时机：解析失败时立即释放，防止malloc的内存呢未free */    
if (head != NULL)
    {
        cJSON_Delete(head); /* 释放链表内存，head时链表头指针 */
    }

    return false; /* 解析失败，返回false */
}

/* Render an array to text */
static cJSON_bool print_array(const cJSON * const item, printbuffer * const output_buffer)
{
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    cJSON *current_element = item->child;

    if (output_buffer == NULL)
    {
        return false;
    }

    /* Compose the output array. */
    /* opening square bracket */
    output_pointer = ensure(output_buffer, 1);
    if (output_pointer == NULL)
    {
        return false;
    }

    *output_pointer = '[';
    output_buffer->offset++;
    output_buffer->depth++;

    while (current_element != NULL)
    {
        if (!print_value(current_element, output_buffer))
        {
            return false;
        }
        update_offset(output_buffer);
        if (current_element->next)
        {
            length = (size_t) (output_buffer->format ? 2 : 1);
            output_pointer = ensure(output_buffer, length + 1);
            if (output_pointer == NULL)
            {
                return false;
            }
            *output_pointer++ = ',';
            if(output_buffer->format)
            {
                *output_pointer++ = ' ';
            }
            *output_pointer = '\0';
            output_buffer->offset += length;
        }
        current_element = current_element->next;
    }

    output_pointer = ensure(output_buffer, 2);
    if (output_pointer == NULL)
    {
        return false;
    }
    *output_pointer++ = ']';
    *output_pointer = '\0';
    output_buffer->depth--;

    return true;
}

/* Build an object from the text. */
/* 简单介绍：解析JSON文本中的对象（{}包裹的键值对集合），构建cJSON链表 结构
   参数项：待填充的cJSON根节点，即输出参数，函数会将其标记为对象类型并关联子节点
   参数input_buffer是输入输出参数，解析缓存区，包含待解析字符数组、当前偏移量、嵌套深度和内存分配钩子等
   cJSON_bool指会返回true表解析成功，返回false表解析失败，可能是因为语法错误或内存不足或嵌套超限等原因*/
/* 主要流程：1.嵌套深度检查（防止深层嵌套导致栈溢出）
            2.校验对象起始符'{'，确认是合法对象开头
            3.处理空对象（{}），直接标记类型并返回
            4.循环解析键值对：
              ①分配新节点，来存储单个键值对
              ②解析键名，并存入节点string字段
              ③校验键值分隔符':'是否符合JSON语法
              ④解析值（支持所有JSON类型），填充节点对应字段
              ⑤逗号分隔符检查，决定是否继续循环
            5.校验对象结束符'}'，来确认对象闭合
            6.构建双向循环列表，关联所有键值对节点*/
/* 存储器内存管理：分配：通过cJSON_New_Item调用input_buffer->hooks中的malloc分配单个cJSON节点,大小为sizeof(cJSON)
                 释放：解析失败时通过 cJSON_Delete 递归释放已分配的键值对链表，避免内存泄漏
                 生命周期：解析成功后，节点链表由外部调用者负责释放；失败时函数内部完成清理*/
static cJSON_bool parse_object(cJSON * const item, parse_buffer * const input_buffer)
{
    cJSON *head = NULL; /* linked list head   键值对链表头指针，最终赋值给item->child */
    cJSON *current_item = NULL; /* 遍历链表的当前节点指针，始终指向链表尾部 */

    /* 嵌套深度检查：防止恶意深层嵌套JSON导致栈/内存溢出    CJSON_NESTING_LIMIT默认为1000，是cJSON定义的最大嵌套层级 */
    if (input_buffer->depth >= CJSON_NESTING_LIMIT)
    {
        return false; /* to deeply nested     嵌套层级超限，直接返回失败 */
    }
    input_buffer->depth++; /* 进入对象层级，深度+1（与success处的depth--成对） */

    /* 校验JSON对象起始符'{'：必须以'{'为开头才是合法对象 
       cannot_access_at_index检查缓冲区当前offset是否越界，避免内存呢访问错误
       buffer_at_offset获取缓存区offset位置的字符 */
    if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '{'))
    {
        goto fail; /* not an object     无字符/首字符非'{'，不是合法对象，按失败处理 */
    }

    input_buffer->offset++; /* 指针偏移，跳过'{'，指向后续内容 */
    buffer_skip_whitespace(input_buffer); /* 跳过所有空白字符（包括空格/制表符/换行等），JSON语法允许任意空白 */
    
    /* 处理空对象{}：跳过空白后直接遇到'}'，无需解析键值对 
       can_access_at_index反向检查，确认offset位置有可访问字符 */
    if (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == '}'))
    {
        goto success; /* empty object     空对象解析完成，跳转到成功处理 */
    }

    /* 边界校验：跳过空白后缓冲区一结束，不完整的对象（如'{'），解析失败 */
    /* check if we skipped to the end of the buffer */
    if (cannot_access_at_index(input_buffer, 0))
    {
        input_buffer->offset--; /* 指针回退，恢复到跳过空白前的位置，保证后续解析上下文正确 */
        goto fail; /* 缓冲区越界，跳转到失败处理 */
    }

    /* 指针回退，为do-while循环做准备，循环会先执行offset++，避免跳过第一个键 */
    /* step back to character in front of the first element */
    input_buffer->offset--;
    
    /* 循环解析键值对：do-while保证至少执行一次（非空对象至少有一个键值对） 
       循环终止条件：当前字符非逗号或缓冲区越界 */
    /* loop through the comma separated array elements */
    do
    {
        /* 分配新cJSON节点，存储单个键值对*/
        /* cJSON_New_Item：调用input_buffer->hooks->malloc分配sizeof(cJSON)大小的内容*/
        /* allocate next item */
        cJSON *new_item = cJSON_New_Item(&(input_buffer->hooks));
        if (new_item == NULL)
        {
            goto fail; /* allocation failure    内存分配失败，跳转到失败处理（释放已分配节点） */
        }

        /* 将新节点添加到双向链表，维护链表头和尾指针 */
        /* attach next item to list */
        if (head == NULL)
        {
            /* 第一个节点：维护链表头和尾指针 */
            /* start the linked list */
            current_item = head = new_item;
        }
        else
        {
            /* 非第一个节点：尾部追加，更新双向链表的prev/next指针 */
            /* add to the end and advance */
            current_item->next = new_item; /* 原尾节点next指向新节点 */
            new_item->prev = current_item; /* 新节点prev指向原尾节点 */
            current_item = new_item; /* 当前节点移动到新尾部 */
        }

        /* 边界校验：逗号后必须有内容 */
        /* 检查offset+1的位置，避免逗号后直接结束 */
        if (cannot_access_at_index(input_buffer, 1))
        {
            goto fail; /* nothing comes after the comma   逗号后无内容，JSON格式非法，跳转到失败处理 */
        }

        /* 解析键名：JSON对象的键必须是双引号包裹的字符串 */
        /* parse the name of the child */
        input_buffer->offset++; /* 指针偏移，跳过逗号或对象起始后的第一个字符 */
        buffer_skip_whitespace(input_buffer); /* 跳过键名前的空白字符 */
        /* parse_strinf：解析字符串并将结果存入new_item->valuestring */
        if (!parse_string(current_item, input_buffer))
        {
            goto fail; /* failed to parse name   域名解析失败（非字符串），跳转到失败处理 */
        }
        buffer_skip_whitespace(input_buffer); /* 跳过键名后的空白字符，准备解析冒号 */

        /* 调整键名存储位置：cJSON节点的string字段存键名，valuestring存字符串值 */
        /* parse_string将键名存入valuestring，需交换到string字段 */
        /* swap valuestring and string, because we parsed the name */
        current_item->string = current_item->valuestring; /* 键名移到string字段 */
        current_item->valuestring = NULL; /* 清空valuestring，为解析值做准备 */

        /* 校验键值分隔符':'：JSON语法要求键和值直接按必须有冒号 */
        if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != ':'))
        {
            goto fail; /* invalid object   无字符/非冒号，格式非法，跳转到失败处理 */
        }

        /* 解析键对应的值，支持字符串/数字/布尔/数组/对象等所有的JSON类型 */
        /* parse the value */
        input_buffer->offset++; /* 指针偏移，跳过冒号 */
        buffer_skip_whitespace(input_buffer); /* 跳过冒号后的空白字符 */
        /* parse_value：万能值解析函数，填充current_item的类型和值字段 */
        if (!parse_value(current_item, input_buffer))
        {
            goto fail; /* failed to parse value   值解析失败，跳转到失败处理 */
        }
        buffer_skip_whitespace(input_buffer); /* 跳过值后的空白字符，准备检查逗号 */
    }
    while (can_access_at_index(input_buffer, 0) && (buffer_at_offset(input_buffer)[0] == ','));
    /* 循环终止逻辑：ccan_access_at_index判断缓冲区未越界 && 当前字符是逗号，还有下一个键值对 
       只有两个条件都满足才继续循环，否则终止循环 */

    /* 校验对象结束符'}'：循环结束后必须一'}'闭合 */
       if (cannot_access_at_index(input_buffer, 0) || (buffer_at_offset(input_buffer)[0] != '}'))
    {
        goto fail; /* expected end of object   无字符/非'}'，对象未闭合，跳转到失败处理 */
    }

/* 域名解析成功处理标签：集中处理成功逻辑，避免重复代码 */
success:
    input_buffer->depth--; /* 嵌套深度回退，退出对象层级，与开头的depth++成对 */

    /* 构建双向循环链表：链表头的prev指向链表尾，实现闭环，方便反向遍历 */
    if (head != NULL) {
        head->prev = current_item; /* 链表头prev指向尾节点 */
    }

    /* 填充根节点，标记类型为对象，关联键值对链表 */
    item->type = cJSON_Object; /* 标记根节点为JSON对象类型 */
    item->child = head; /* 根节点child指向键值对链表头 */

    input_buffer->offset++; /* 指针偏移，跳过'}'字符，准备解析后续内容 */
    return true; /* 解析成功，返回true */

/* 解析失败处理标签：集中释放内存，避免内存泄露 */
fail:
    /* 释放已分配的键值对链表：cJSON_Delete会递归释放整个链表 */
    /* 释放时机：解析失败时立即释放，防止malloc的内存未free */    
    if (head != NULL)
    {
        cJSON_Delete(head); /* 释放链表内存，head是链表头指针 */
    }

    return false; /* 解析失败，返回false */
}

/* Render an object to text. */
static cJSON_bool print_object(const cJSON * const item, printbuffer * const output_buffer)
{
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    cJSON *current_item = item->child;

    if (output_buffer == NULL)
    {
        return false;
    }

    /* Compose the output: */
    length = (size_t) (output_buffer->format ? 2 : 1); /* fmt: {\n */
    output_pointer = ensure(output_buffer, length + 1);
    if (output_pointer == NULL)
    {
        return false;
    }

    *output_pointer++ = '{';
    output_buffer->depth++;
    if (output_buffer->format)
    {
        *output_pointer++ = '\n';
    }
    output_buffer->offset += length;

    while (current_item)
    {
        if (output_buffer->format)
        {
            size_t i;
            output_pointer = ensure(output_buffer, output_buffer->depth);
            if (output_pointer == NULL)
            {
                return false;
            }
            for (i = 0; i < output_buffer->depth; i++)
            {
                *output_pointer++ = '\t';
            }
            output_buffer->offset += output_buffer->depth;
        }

        /* print key */
        if (!print_string_ptr((unsigned char*)current_item->string, output_buffer))
        {
            return false;
        }
        update_offset(output_buffer);

        length = (size_t) (output_buffer->format ? 2 : 1);
        output_pointer = ensure(output_buffer, length);
        if (output_pointer == NULL)
        {
            return false;
        }
        *output_pointer++ = ':';
        if (output_buffer->format)
        {
            *output_pointer++ = '\t';
        }
        output_buffer->offset += length;

        /* print value */
        if (!print_value(current_item, output_buffer))
        {
            return false;
        }
        update_offset(output_buffer);

        /* print comma if not last */
        length = ((size_t)(output_buffer->format ? 1 : 0) + (size_t)(current_item->next ? 1 : 0));
        output_pointer = ensure(output_buffer, length + 1);
        if (output_pointer == NULL)
        {
            return false;
        }
        if (current_item->next)
        {
            *output_pointer++ = ',';
        }

        if (output_buffer->format)
        {
            *output_pointer++ = '\n';
        }
        *output_pointer = '\0';
        output_buffer->offset += length;

        current_item = current_item->next;
    }

    output_pointer = ensure(output_buffer, output_buffer->format ? (output_buffer->depth + 1) : 2);
    if (output_pointer == NULL)
    {
        return false;
    }
    if (output_buffer->format)
    {
        size_t i;
        for (i = 0; i < (output_buffer->depth - 1); i++)
        {
            *output_pointer++ = '\t';
        }
    }
    *output_pointer++ = '}';
    *output_pointer = '\0';
    output_buffer->depth--;

    return true;
}

/* Get Array size/item / object item. */
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array)
{
    cJSON *child = NULL;
    size_t size = 0;

    if (array == NULL)
    {
        return 0;
    }

    child = array->child;

    while(child != NULL)
    {
        size++;
        child = child->next;
    }

    /* FIXME: Can overflow here. Cannot be fixed without breaking the API */

    return (int)size;
}

static cJSON* get_array_item(const cJSON *array, size_t index)
{
    cJSON *current_child = NULL;

    if (array == NULL)
    {
        return NULL;
    }

    current_child = array->child;
    while ((current_child != NULL) && (index > 0))
    {
        index--;
        current_child = current_child->next;
    }

    return current_child;
}

CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index)
{
    if (index < 0)
    {
        return NULL;
    }

    return get_array_item(array, (size_t)index);
}

static cJSON *get_object_item(const cJSON * const object, const char * const name, const cJSON_bool case_sensitive)
{
    cJSON *current_element = NULL;

    if ((object == NULL) || (name == NULL))
    {
        return NULL;
    }

    current_element = object->child;
    if (case_sensitive)
    {
        while ((current_element != NULL) && (current_element->string != NULL) && (strcmp(name, current_element->string) != 0))
        {
            current_element = current_element->next;
        }
    }
    else
    {
        while ((current_element != NULL) && (case_insensitive_strcmp((const unsigned char*)name, (const unsigned char*)(current_element->string)) != 0))
        {
            current_element = current_element->next;
        }
    }

    if ((current_element == NULL) || (current_element->string == NULL)) {
        return NULL;
    }

    return current_element;
}

CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string)
{
    return get_object_item(object, string, false);
}

CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string)
{
    return get_object_item(object, string, true);
}

CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string)
{
    return cJSON_GetObjectItem(object, string) ? 1 : 0;
}

/* Utility for array list handling. */
static void suffix_object(cJSON *prev, cJSON *item)
{
    prev->next = item;
    item->prev = prev;
}

/* Utility for handling references. */
static cJSON *create_reference(const cJSON *item, const internal_hooks * const hooks)
{
    cJSON *reference = NULL;
    if (item == NULL)
    {
        return NULL;
    }

    reference = cJSON_New_Item(hooks);
    if (reference == NULL)
    {
        return NULL;
    }

    memcpy(reference, item, sizeof(cJSON));
    reference->string = NULL;
    reference->type |= cJSON_IsReference;
    reference->next = reference->prev = NULL;
    return reference;
}

static cJSON_bool add_item_to_array(cJSON *array, cJSON *item)
{
    cJSON *child = NULL;

    if ((item == NULL) || (array == NULL) || (array == item))
    {
        return false;
    }

    child = array->child;
    /*
     * To find the last item in array quickly, we use prev in array
     */
    if (child == NULL)
    {
        /* list is empty, start new one */
        array->child = item;
        item->prev = item;
        item->next = NULL;
    }
    else
    {
        /* append to the end */
        if (child->prev)
        {
            suffix_object(child->prev, item);
            array->child->prev = item;
        }
    }

    return true;
}

/* Add item to array/object. */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    return add_item_to_array(array, item);
}

#if defined(__clang__) || (defined(__GNUC__)  && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 5))))
    #pragma GCC diagnostic push
#endif
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
/* helper function to cast away const */
static void* cast_away_const(const void* string)
{
    return (void*)string;
}
#if defined(__clang__) || (defined(__GNUC__)  && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ > 5))))
    #pragma GCC diagnostic pop
#endif


static cJSON_bool add_item_to_object(cJSON * const object, const char * const string, cJSON * const item, const internal_hooks * const hooks, const cJSON_bool constant_key)
{
    char *new_key = NULL;
    int new_type = cJSON_Invalid;

    if ((object == NULL) || (string == NULL) || (item == NULL) || (object == item))
    {
        return false;
    }

    if (constant_key)
    {
        new_key = (char*)cast_away_const(string);
        new_type = item->type | cJSON_StringIsConst;
    }
    else
    {
        new_key = (char*)cJSON_strdup((const unsigned char*)string, hooks);
        if (new_key == NULL)
        {
            return false;
        }

        new_type = item->type & ~cJSON_StringIsConst;
    }

    if (!(item->type & cJSON_StringIsConst) && (item->string != NULL))
    {
        hooks->deallocate(item->string);
    }

    item->string = new_key;
    item->type = new_type;

    return add_item_to_array(object, item);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    return add_item_to_object(object, string, item, &global_hooks, false);
}

/* Add an item to an object with constant string as key */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item)
{
    return add_item_to_object(object, string, item, &global_hooks, true);
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item)
{
    if (array == NULL)
    {
        return false;
    }

    return add_item_to_array(array, create_reference(item, &global_hooks));
}

CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item)
{
    if ((object == NULL) || (string == NULL))
    {
        return false;
    }

    return add_item_to_object(object, string, create_reference(item, &global_hooks), &global_hooks, false);
}

CJSON_PUBLIC(cJSON*) cJSON_AddNullToObject(cJSON * const object, const char * const name)
{
    cJSON *null = cJSON_CreateNull();
    if (add_item_to_object(object, name, null, &global_hooks, false))
    {
        return null;
    }

    cJSON_Delete(null);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddTrueToObject(cJSON * const object, const char * const name)
{
    cJSON *true_item = cJSON_CreateTrue();
    if (add_item_to_object(object, name, true_item, &global_hooks, false))
    {
        return true_item;
    }

    cJSON_Delete(true_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddFalseToObject(cJSON * const object, const char * const name)
{
    cJSON *false_item = cJSON_CreateFalse();
    if (add_item_to_object(object, name, false_item, &global_hooks, false))
    {
        return false_item;
    }

    cJSON_Delete(false_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean)
{
    cJSON *bool_item = cJSON_CreateBool(boolean);
    if (add_item_to_object(object, name, bool_item, &global_hooks, false))
    {
        return bool_item;
    }

    cJSON_Delete(bool_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number)
{
    cJSON *number_item = cJSON_CreateNumber(number);
    if (add_item_to_object(object, name, number_item, &global_hooks, false))
    {
        return number_item;
    }

    cJSON_Delete(number_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string)
{
    cJSON *string_item = cJSON_CreateString(string);
    if (add_item_to_object(object, name, string_item, &global_hooks, false))
    {
        return string_item;
    }

    cJSON_Delete(string_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddRawToObject(cJSON * const object, const char * const name, const char * const raw)
{
    cJSON *raw_item = cJSON_CreateRaw(raw);
    if (add_item_to_object(object, name, raw_item, &global_hooks, false))
    {
        return raw_item;
    }

    cJSON_Delete(raw_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddObjectToObject(cJSON * const object, const char * const name)
{
    cJSON *object_item = cJSON_CreateObject();
    if (add_item_to_object(object, name, object_item, &global_hooks, false))
    {
        return object_item;
    }

    cJSON_Delete(object_item);
    return NULL;
}

CJSON_PUBLIC(cJSON*) cJSON_AddArrayToObject(cJSON * const object, const char * const name)
{
    cJSON *array = cJSON_CreateArray();
    if (add_item_to_object(object, name, array, &global_hooks, false))
    {
        return array;
    }

    cJSON_Delete(array);
    return NULL;
}

CJSON_PUBLIC(cJSON *) cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item)
{
    if ((parent == NULL) || (item == NULL) || (item != parent->child && item->prev == NULL))
    {
        return NULL;
    }

    if (item != parent->child)
    {
        /* not the first element */
        item->prev->next = item->next;
    }
    if (item->next != NULL)
    {
        /* not the last element */
        item->next->prev = item->prev;
    }

    if (item == parent->child)
    {
        /* first element */
        parent->child = item->next;
    }
    else if (item->next == NULL)
    {
        /* last element */
        parent->child->prev = item->prev;
    }

    /* make sure the detached item doesn't point anywhere anymore */
    item->prev = NULL;
    item->next = NULL;

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromArray(cJSON *array, int which)
{
    if (which < 0)
    {
        return NULL;
    }

    return cJSON_DetachItemViaPointer(array, get_array_item(array, (size_t)which));
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *array, int which)
{
    cJSON_Delete(cJSON_DetachItemFromArray(array, which));
}

CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObject(cJSON *object, const char *string)
{
    cJSON *to_detach = cJSON_GetObjectItem(object, string);

    return cJSON_DetachItemViaPointer(object, to_detach);
}

CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string)
{
    cJSON *to_detach = cJSON_GetObjectItemCaseSensitive(object, string);

    return cJSON_DetachItemViaPointer(object, to_detach);
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *object, const char *string)
{
    cJSON_Delete(cJSON_DetachItemFromObject(object, string));
}

CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string)
{
    cJSON_Delete(cJSON_DetachItemFromObjectCaseSensitive(object, string));
}

/* Replace array/object items with new ones. */
CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem)
{
    cJSON *after_inserted = NULL;

    if (which < 0 || newitem == NULL)
    {
        return false;
    }

    after_inserted = get_array_item(array, (size_t)which);
    if (after_inserted == NULL)
    {
        return add_item_to_array(array, newitem);
    }

    if (after_inserted != array->child && after_inserted->prev == NULL) {
        /* return false if after_inserted is a corrupted array item */
        return false;
    }

    newitem->next = after_inserted;
    newitem->prev = after_inserted->prev;
    after_inserted->prev = newitem;
    if (after_inserted == array->child)
    {
        array->child = newitem;
    }
    else
    {
        newitem->prev->next = newitem;
    }
    return true;
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON * replacement)
{
    if ((parent == NULL) || (parent->child == NULL) || (replacement == NULL) || (item == NULL))
    {
        return false;
    }

    if (replacement == item)
    {
        return true;
    }

    replacement->next = item->next;
    replacement->prev = item->prev;

    if (replacement->next != NULL)
    {
        replacement->next->prev = replacement;
    }
    if (parent->child == item)
    {
        if (parent->child->prev == parent->child)
        {
            replacement->prev = replacement;
        }
        parent->child = replacement;
    }
    else
    {   /*
         * To find the last item in array quickly, we use prev in array.
         * We can't modify the last item's next pointer where this item was the parent's child
         */
        if (replacement->prev != NULL)
        {
            replacement->prev->next = replacement;
        }
        if (replacement->next == NULL)
        {
            parent->child->prev = replacement;
        }
    }

    item->next = NULL;
    item->prev = NULL;
    cJSON_Delete(item);

    return true;
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem)
{
    if (which < 0)
    {
        return false;
    }

    return cJSON_ReplaceItemViaPointer(array, get_array_item(array, (size_t)which), newitem);
}

static cJSON_bool replace_item_in_object(cJSON *object, const char *string, cJSON *replacement, cJSON_bool case_sensitive)
{
    if ((replacement == NULL) || (string == NULL))
    {
        return false;
    }

    /* replace the name in the replacement */
    if (!(replacement->type & cJSON_StringIsConst) && (replacement->string != NULL))
    {
        cJSON_free(replacement->string);
    }
    replacement->string = (char*)cJSON_strdup((const unsigned char*)string, &global_hooks);
    if (replacement->string == NULL)
    {
        return false;
    }

    replacement->type &= ~cJSON_StringIsConst;

    return cJSON_ReplaceItemViaPointer(object, get_object_item(object, string, case_sensitive), replacement);
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *object, const char *string, cJSON *newitem)
{
    return replace_item_in_object(object, string, newitem, false);
}

CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object, const char *string, cJSON *newitem)
{
    return replace_item_in_object(object, string, newitem, true);
}

/* Create basic types: */
CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_NULL;
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_True;
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_False;
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = boolean ? cJSON_True : cJSON_False;
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_Number;
        item->valuedouble = num;

        /* use saturation in case of overflow */
        if (num >= INT_MAX)
        {
            item->valueint = INT_MAX;
        }
        else if (num <= (double)INT_MIN)
        {
            item->valueint = INT_MIN;
        }
        else
        {
            item->valueint = (int)num;
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_String;
        item->valuestring = (char*)cJSON_strdup((const unsigned char*)string, &global_hooks);
        if(!item->valuestring)
        {
            cJSON_Delete(item);
            return NULL;
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateStringReference(const char *string)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if (item != NULL)
    {
        item->type = cJSON_String | cJSON_IsReference;
        item->valuestring = (char*)cast_away_const(string);
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateObjectReference(const cJSON *child)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if (item != NULL) {
        item->type = cJSON_Object | cJSON_IsReference;
        item->child = (cJSON*)cast_away_const(child);
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateArrayReference(const cJSON *child) {
    cJSON *item = cJSON_New_Item(&global_hooks);
    if (item != NULL) {
        item->type = cJSON_Array | cJSON_IsReference;
        item->child = (cJSON*)cast_away_const(child);
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateRaw(const char *raw)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type = cJSON_Raw;
        item->valuestring = (char*)cJSON_strdup((const unsigned char*)raw, &global_hooks);
        if(!item->valuestring)
        {
            cJSON_Delete(item);
            return NULL;
        }
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if(item)
    {
        item->type=cJSON_Array;
    }

    return item;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void)
{
    cJSON *item = cJSON_New_Item(&global_hooks);
    if (item)
    {
        item->type = cJSON_Object;
    }

    return item;
}

/* Create Arrays: */
CJSON_PUBLIC(cJSON *) cJSON_CreateIntArray(const int *numbers, int count)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for(i = 0; a && (i < (size_t)count); i++)
    {
        n = cJSON_CreateNumber(numbers[i]);
        if (!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    if (a && a->child) {
        a->child->prev = n;
    }

    return a;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateFloatArray(const float *numbers, int count)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for(i = 0; a && (i < (size_t)count); i++)
    {
        n = cJSON_CreateNumber((double)numbers[i]);
        if(!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    if (a && a->child) {
        a->child->prev = n;
    }

    return a;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateDoubleArray(const double *numbers, int count)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    if ((count < 0) || (numbers == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for(i = 0; a && (i < (size_t)count); i++)
    {
        n = cJSON_CreateNumber(numbers[i]);
        if(!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p, n);
        }
        p = n;
    }

    if (a && a->child) {
        a->child->prev = n;
    }

    return a;
}

CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char *const *strings, int count)
{
    size_t i = 0;
    cJSON *n = NULL;
    cJSON *p = NULL;
    cJSON *a = NULL;

    if ((count < 0) || (strings == NULL))
    {
        return NULL;
    }

    a = cJSON_CreateArray();

    for (i = 0; a && (i < (size_t)count); i++)
    {
        n = cJSON_CreateString(strings[i]);
        if(!n)
        {
            cJSON_Delete(a);
            return NULL;
        }
        if(!i)
        {
            a->child = n;
        }
        else
        {
            suffix_object(p,n);
        }
        p = n;
    }

    if (a && a->child) {
        a->child->prev = n;
    }

    return a;
}

/* Duplication */
cJSON * cJSON_Duplicate_rec(const cJSON *item, size_t depth, cJSON_bool recurse);

CJSON_PUBLIC(cJSON *) cJSON_Duplicate(const cJSON *item, cJSON_bool recurse)
{
    return cJSON_Duplicate_rec(item, 0, recurse );
}

cJSON * cJSON_Duplicate_rec(const cJSON *item, size_t depth, cJSON_bool recurse)
{
    cJSON *newitem = NULL;
    cJSON *child = NULL;
    cJSON *next = NULL;
    cJSON *newchild = NULL;

    /* Bail on bad ptr */
    if (!item)
    {
        goto fail;
    }
    /* Create new item */
    newitem = cJSON_New_Item(&global_hooks);
    if (!newitem)
    {
        goto fail;
    }
    /* Copy over all vars */
    newitem->type = item->type & (~cJSON_IsReference);
    newitem->valueint = item->valueint;
    newitem->valuedouble = item->valuedouble;
    if (item->valuestring)
    {
        newitem->valuestring = (char*)cJSON_strdup((unsigned char*)item->valuestring, &global_hooks);
        if (!newitem->valuestring)
        {
            goto fail;
        }
    }
    if (item->string)
    {
        newitem->string = (item->type&cJSON_StringIsConst) ? item->string : (char*)cJSON_strdup((unsigned char*)item->string, &global_hooks);
        if (!newitem->string)
        {
            goto fail;
        }
    }
    /* If non-recursive, then we're done! */
    if (!recurse)
    {
        return newitem;
    }
    /* Walk the ->next chain for the child. */
    child = item->child;
    while (child != NULL)
    {
        if(depth >= CJSON_CIRCULAR_LIMIT) {
            goto fail;
        }
        newchild = cJSON_Duplicate_rec(child, depth + 1, true); /* Duplicate (with recurse) each item in the ->next chain */
        if (!newchild)
        {
            goto fail;
        }
        if (next != NULL)
        {
            /* If newitem->child already set, then crosswire ->prev and ->next and move on */
            next->next = newchild;
            newchild->prev = next;
            next = newchild;
        }
        else
        {
            /* Set newitem->child and move to it */
            newitem->child = newchild;
            next = newchild;
        }
        child = child->next;
    }
    if (newitem && newitem->child)
    {
        newitem->child->prev = newchild;
    }

    return newitem;

fail:
    if (newitem != NULL)
    {
        cJSON_Delete(newitem);
    }

    return NULL;
}

static void skip_oneline_comment(char **input)
{
    *input += static_strlen("//");

    for (; (*input)[0] != '\0'; ++(*input))
    {
        if ((*input)[0] == '\n') {
            *input += static_strlen("\n");
            return;
        }
    }
}

static void skip_multiline_comment(char **input)
{
    *input += static_strlen("/*");

    for (; (*input)[0] != '\0'; ++(*input))
    {
        if (((*input)[0] == '*') && ((*input)[1] == '/'))
        {
            *input += static_strlen("*/");
            return;
        }
    }
}

static void minify_string(char **input, char **output) {
    (*output)[0] = (*input)[0];
    *input += static_strlen("\"");
    *output += static_strlen("\"");


    for (; (*input)[0] != '\0'; (void)++(*input), ++(*output)) {
        (*output)[0] = (*input)[0];

        if ((*input)[0] == '\"') {
            (*output)[0] = '\"';
            *input += static_strlen("\"");
            *output += static_strlen("\"");
            return;
        } else if (((*input)[0] == '\\') && ((*input)[1] == '\"')) {
            (*output)[1] = (*input)[1];
            *input += static_strlen("\"");
            *output += static_strlen("\"");
        }
    }
}

CJSON_PUBLIC(void) cJSON_Minify(char *json)
{
    char *into = json;

    if (json == NULL)
    {
        return;
    }

    while (json[0] != '\0')
    {
        switch (json[0])
        {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                json++;
                break;

            case '/':
                if (json[1] == '/')
                {
                    skip_oneline_comment(&json);
                }
                else if (json[1] == '*')
                {
                    skip_multiline_comment(&json);
                } else {
                    json++;
                }
                break;

            case '\"':
                minify_string(&json, (char**)&into);
                break;

            default:
                into[0] = json[0];
                json++;
                into++;
        }
    }

    /* and null-terminate. */
    *into = '\0';
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Invalid;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_False;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xff) == cJSON_True;
}


CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & (cJSON_True | cJSON_False)) != 0;
}
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_NULL;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Number;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_String;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Array;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Object;
}

CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item)
{
    if (item == NULL)
    {
        return false;
    }

    return (item->type & 0xFF) == cJSON_Raw;
}

CJSON_PUBLIC(cJSON_bool) cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive)
{
    if ((a == NULL) || (b == NULL) || ((a->type & 0xFF) != (b->type & 0xFF)))
    {
        return false;
    }

    /* check if type is valid */
    switch (a->type & 0xFF)
    {
        case cJSON_False:
        case cJSON_True:
        case cJSON_NULL:
        case cJSON_Number:
        case cJSON_String:
        case cJSON_Raw:
        case cJSON_Array:
        case cJSON_Object:
            break;

        default:
            return false;
    }

    /* identical objects are equal */
    if (a == b)
    {
        return true;
    }

    switch (a->type & 0xFF)
    {
        /* in these cases and equal type is enough */
        case cJSON_False:
        case cJSON_True:
        case cJSON_NULL:
            return true;

        case cJSON_Number:
            if (compare_double(a->valuedouble, b->valuedouble))
            {
                return true;
            }
            return false;

        case cJSON_String:
        case cJSON_Raw:
            if ((a->valuestring == NULL) || (b->valuestring == NULL))
            {
                return false;
            }
            if (strcmp(a->valuestring, b->valuestring) == 0)
            {
                return true;
            }

            return false;

        case cJSON_Array:
        {
            cJSON *a_element = a->child;
            cJSON *b_element = b->child;

            for (; (a_element != NULL) && (b_element != NULL);)
            {
                if (!cJSON_Compare(a_element, b_element, case_sensitive))
                {
                    return false;
                }

                a_element = a_element->next;
                b_element = b_element->next;
            }

            /* one of the arrays is longer than the other */
            if (a_element != b_element) {
                return false;
            }

            return true;
        }

        case cJSON_Object:
        {
            cJSON *a_element = NULL;
            cJSON *b_element = NULL;
            cJSON_ArrayForEach(a_element, a)
            {
                /* TODO This has O(n^2) runtime, which is horrible! */
                b_element = get_object_item(b, a_element->string, case_sensitive);
                if (b_element == NULL)
                {
                    return false;
                }

                if (!cJSON_Compare(a_element, b_element, case_sensitive))
                {
                    return false;
                }
            }

            /* doing this twice, once on a and b to prevent true comparison if a subset of b
             * TODO: Do this the proper way, this is just a fix for now */
            cJSON_ArrayForEach(b_element, b)
            {
                a_element = get_object_item(a, b_element->string, case_sensitive);
                if (a_element == NULL)
                {
                    return false;
                }

                if (!cJSON_Compare(b_element, a_element, case_sensitive))
                {
                    return false;
                }
            }

            return true;
        }

        default:
            return false;
    }
}

CJSON_PUBLIC(void *) cJSON_malloc(size_t size)
{
    return global_hooks.allocate(size);
}

CJSON_PUBLIC(void) cJSON_free(void *object)
{
    global_hooks.deallocate(object);
    object = NULL;
}
