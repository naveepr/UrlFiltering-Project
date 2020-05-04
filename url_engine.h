#ifndef _URL_ENGINE_H_
#define _URL_ENGINE_H_

#define SET_MAX_SIZE    1000
#define PATTERN_STRING_MAX_LENGTH 100
#define BUFF_SIZE 1024

/*! \struct _pattern_t 
 *  Used to hold each set from config.xml
 *  key - set id
 *  array of patterns
 */
typedef struct _pattern_t {
    int key; 
    int num_patterns; 
    char *pattern[PATTERN_STRING_MAX_LENGTH]; 
} pattern_t;

typedef enum match_type{
    POSIX=0,
    SELF
}MATCH_TYPE;

#define TM_PRINTF(f_, ...)  \
    if (debug_enabled)  \
        printf((f_), ##__VA_ARGS__) \

#endif /* ifndef _URL_ENGINE_H_ */
