#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <libxml/parser.h>
#include <regex.h>
#include <time.h>
#include "url_engine.h"
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>

pattern_t config_pattern[SET_MAX_SIZE];
int num_sets=0;
bool debug_enabled=false;
bool is_sighandler_rcvd=false;
bool is_thread_finished = false;
bool fileRead_end = false;
char *configFile;


pthread_mutex_t lock;

struct thread_info { 
	pthread_t thread_id;
	int       thread_num;
	MATCH_TYPE algo;
};


#define NUM_BUFFERS 100
sem_t empty_sem, full_sem;
char buffer[NUM_BUFFERS][BUFF_SIZE];
int buffer_index;
pthread_mutex_t buffer_lock;

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to read the pattern from config.xml and save in pattern structure 
 *
 * @Param doc
 * @Param a_node
 */
/* ----------------------------------------------------------------------------*/
static void construct_pattern_from_xml(xmlDocPtr doc, xmlNodePtr  a_node)
{
    xmlNodePtr temp, cur_node = NULL;
    xmlAttrPtr attr; 
    xmlChar * key;

    int set=0, i=0;
    memset(config_pattern, 0, sizeof(config_pattern));

    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE) {
            TM_PRINTF("node type: Element, name: %s\n", cur_node->name);
            if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"set"))) {
                    attr = cur_node->properties;
                    i =0;
                    if (attr)
                    {
                        TM_PRINTF( "Attribute name: %s value: %d\n", attr->name ,set);
                        config_pattern[set].key = atoi((char*)attr->children->content);;
                    }

                    temp = cur_node;
                    cur_node = cur_node->xmlChildrenNode;
                    while(cur_node){
                        if ((!xmlStrcmp(cur_node->name, (const xmlChar *)"pattern"))) {
                            key = xmlNodeListGetString(doc, cur_node->xmlChildrenNode, 1);
                            TM_PRINTF("name %s: keyword: %s\n", cur_node->name, key);
                            config_pattern[set].pattern[i] = (char*) calloc(PATTERN_STRING_MAX_LENGTH, sizeof(char));
                            strncpy(&config_pattern[set].pattern[i][0],(char*)key,PATTERN_STRING_MAX_LENGTH);
                            i++;
                            xmlFree(key);
                        }
                        cur_node = cur_node->next;
                    }
                    config_pattern[set].num_patterns = i; 
                    cur_node = temp; 
                    set++;
            } 
        }
    }

    num_sets = set;
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to free the memory allocated for each pattern in config.xml
 */
/* ----------------------------------------------------------------------------*/
static inline void free_pattern_allocated_memory()
{
    int i,j;
    
    for (i = 0;i<num_sets;i++) {
        for (j=0;j<config_pattern[i].num_patterns; j++) {
            free(config_pattern[i].pattern[j]);
        }
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to print the saved xml config
 */
/* ----------------------------------------------------------------------------*/
static void print_xml_pattern()
{
    int i,j;
    
    TM_PRINTF("print_xml_pattern\n");
    for (i=0;i<num_sets; i++){
        TM_PRINTF("set %d: ", config_pattern[i].key);
        for(j=0;j<config_pattern[i].num_patterns;j++){
            TM_PRINTF("%s ", config_pattern[i].pattern[j]);
        }
    }
    TM_PRINTF("\n");
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to create a new pattern from the pattern read from config.xml
 *  1. Removes consecutive wildcard characters
 *  2. In the case of POSIX algorithm add ^ and $ to do the fullmatch with url
 * @Param pattern
 * @Param new_pattern
 * @Param match_type
 */
/* ----------------------------------------------------------------------------*/
static void create_new_pattern(const char * pattern, char * new_pattern, MATCH_TYPE match_type)
{
    int i=0, writeIndex=0, pattern_len = strlen(pattern);
    bool isFirst = true;

    memset(new_pattern,0 , PATTERN_STRING_MAX_LENGTH);
    
    if (POSIX == match_type){
        new_pattern[writeIndex++] = '^';
    }

    for ( i = 0 ; i < pattern_len; i++) {
        if (pattern[i] == '*') {
            if (isFirst) {
                new_pattern[writeIndex++] = pattern[i]; 
                isFirst = false;
            } 
        } else {
                new_pattern[writeIndex++] = pattern[i];
                isFirst = true;
        }
    }

    if (POSIX == match_type) {
        new_pattern[writeIndex++] = '$';
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to print the url match pattern
 *
 * @Param url
 * @Param match_pattern
 * @Param set
 * @Param is_first_pattern_match
 */
/* ----------------------------------------------------------------------------*/
static inline void print_url_match_pattern(const char *url, const char * match_pattern, int set, bool *is_first_pattern_match)
{
    pthread_mutex_lock(&lock);
    if (*is_first_pattern_match){
        printf("url: %s,", url);
        *is_first_pattern_match = false;
    }
    printf(" pattern: %s, set: %d", match_pattern, config_pattern[set].key);
    pthread_mutex_unlock(&lock);
}


/* --------------------------------------------------------------------------*/
/**
 * @Synopsis Function used to check if the pattern needs to be changed for regex
 * matching. Also the wildcard index before the /delimiter is returned.
 * Case POSIX: return true 
 * case SELF: return true if wildcard present else no change needed
 * @Param pattern
 * @Param wildcard_index
 * @Param match_type
 *
 * @Returns  true or false 
 */
/* ----------------------------------------------------------------------------*/
static bool match_needs_pattern_change(const char * pattern, int * wildcard_index, MATCH_TYPE match_type)
{
    char * wildcard_ch, *last_wildcard_ch ,* delim_ch;

    delim_ch = strchr(pattern,'/');
    wildcard_ch = strchr(pattern,'*');

    if ((0 != wildcard_ch) && 
            ((0==delim_ch) || ((wildcard_ch - delim_ch) <0))) {
        while(wildcard_ch && (!delim_ch ||(wildcard_ch<delim_ch))) {
            last_wildcard_ch = wildcard_ch;
            wildcard_ch = strchr(wildcard_ch+1, '*');
        }
        *wildcard_index = last_wildcard_ch - pattern;
    }

    return (POSIX==match_type) ? true: (*wildcard_index != -1) ;
}



/*-----------------------------------------------------------------------------
 |                          POSIX ALGO FUNCTIONS                            |
 |                                                                          |
 |                                                                          |
 |--------------------------------------------------------------------------|
*/

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to identify special characters 
 *
 * @Param ch
 *
 * @Returns   true or false
 */
/* ----------------------------------------------------------------------------*/
static inline bool regex_special_characters(char ch)
{
    return ( ('.'==ch) || ('?'==ch) || ('\\'==ch) ||
            ('|'==ch) || ('+'==ch) ||  ('('==ch) || (')'==ch) ); // || ('^'==ch) || ('$'==ch));
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to modify the POSIX pattern for according to regex matching
 *
 * @Param old_pattern
 * @Param new_pattern
 * @Param wildcard_index
 */
/* ----------------------------------------------------------------------------*/
static void modify_posix_pattern_string(const char * old_pattern, char * new_pattern, int wildcard_index)
{
    int old_index=0, new_index=0, old_len = strlen(old_pattern);

    while(old_index<old_len) {
        /* if special character */
        if (regex_special_characters(old_pattern[old_index]) ){
            new_pattern[new_index++] = '\\';
            new_pattern[new_index++] = old_pattern[old_index];
        } /* wildcard after delimiter */
        else if (old_pattern[old_index]=='*' && old_index > wildcard_index){
            new_pattern[new_index++] = '.';
            new_pattern[new_index++] = '*';
        } /* wildcard before delimiter */ 
        else if (old_pattern[old_index]=='*' && old_index <= wildcard_index) {
            strncpy(&new_pattern[new_index], "[^\\/]*",6);
            new_index += 6;
        } else {
            new_pattern[new_index++] = old_pattern[old_index];
        }
        old_index++;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to do the regex match given the url and modified pattern
 *
 * @Param url
 * @Param pattern
 *
 * @Returns  true or false 
 */
/* ----------------------------------------------------------------------------*/
static bool regex_match(const char * url, const char * pattern)
{
    regex_t regex;
    int reti;
    char msgbuf[100];

    /* Compile regular expression */
    reti = regcomp(&regex, pattern, 0);
    if (reti) {
        fprintf(stderr, "Could not compile regex\n");
        exit(1);
    }

    /* Execute regular expression */
    reti = regexec(&regex, url, 0, NULL, 0);
    if (!reti) {
        TM_PRINTF("Match\n");
    }
    else if (reti == REG_NOMATCH) {
        TM_PRINTF("No match\n");
    }
    else {
        regerror(reti, &regex, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
        exit(1);
    }

    /* Free memory allocated to the pattern buffer by regcomp() */
    regfree(&regex);

    return (!reti)?true: false;
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function that does URL pattern match based on POSIX algorithm
 *
 * @Param fp
 */
/* ----------------------------------------------------------------------------*/
static void posix_pattern_match(char* url, int thread_num)
{
    int i,j, wildcard_index=-1;
    char new_pattern[PATTERN_STRING_MAX_LENGTH] = {'\0'}, temp_pattern[PATTERN_STRING_MAX_LENGTH] = {'\0'}; 
    bool is_first_pattern_match = true;

    /* Read each URL from file */
    if (url != NULL)
    {
         TM_PRINTF("Enter thread: %d\n", thread_num);
         //Added for testing purpose
         //usleep(10000);
         url[strlen(url) - 1] = '\0';
         is_first_pattern_match = true;
         /* Read each set */
         for (i=0;i<num_sets;i++){
         /* Read each pattern */
           for (j=0;j<config_pattern[i].num_patterns;j++){
                wildcard_index = -1;
                memset(new_pattern, 0, PATTERN_STRING_MAX_LENGTH);
                create_new_pattern(config_pattern[i].pattern[j],temp_pattern, POSIX);

                if (true == match_needs_pattern_change(temp_pattern, &wildcard_index, POSIX)) {
                    modify_posix_pattern_string(temp_pattern, new_pattern, wildcard_index);
                }

                if (regex_match(url,new_pattern)) {
                    print_url_match_pattern(url, config_pattern[i].pattern[j], i, &is_first_pattern_match);
                }
           }
        }
        if (false==is_first_pattern_match) {
            printf("\n");
        }
    } else {
        fprintf(stderr, "URL NULL, threadid %d\n", thread_num);
    }

}


/*
 ----------------------------------------------------------------------------
|                                                                           |
|                               SELF ALGORITHM FUNCTIONS                    |
|                                                                           |
|---------------------------------------------------------------------------|
*/

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function to modify the SELF pattern string
 * if wildcard comes before delimiter replacing it with '|' character to
 * differentiate in the SELF algorithm
 *
 * @Param old_pattern
 * @Param new_pattern
 * @Param wildcard_index
 */
/* ----------------------------------------------------------------------------*/
static inline void modify_self_pattern_string(const char * old_pattern, char * new_pattern, int wildcard_index)
{
    int old_index=0, old_len = strlen(old_pattern);

    while(old_index<old_len) {
        if (old_pattern[old_index]=='*' && old_index <= wildcard_index) {
            new_pattern[old_index] = '|';
        } else {
            new_pattern[old_index] = old_pattern[old_index];
        }
        old_index++;
    }
}
/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function that implementes the SELF algorithm
 *
 * @Param url
 * @Param pattern
 *
 * @Returns  true or false 
 */
/* ----------------------------------------------------------------------------*/
static bool self_match(const char * url, char * pattern)
{
    if (!url && !pattern){
        TM_PRINTF("Both URL and pattern empty/n");
        return true;
    } 

    if (!url) {
         TM_PRINTF("URL empty\n");
         return false;
    }

    if (!pattern) {
         TM_PRINTF("Pattern empty\n");
         return false;
    }

    const int url_len = strlen(url), pattern_len = strlen(pattern);
    int i,j, writeIndex=pattern_len;
    bool dp[url_len+1][writeIndex+1];

    /* Initialize the dp array */
    for (i=0;i<url_len+1;i++) {
        for (j=0;j<writeIndex+1;j++){
            dp[i][j] = false;
        }
    }
    if (writeIndex > 0 && (pattern[0] == '*' || pattern[0]=='|')) {
        dp[0][1] = true;
    } 

    dp[0][0] = true;

    /* Core logic */
    for (i = 1; i < url_len+1; i++) {
        for (j = 1; j < writeIndex+1; j++) {
            if (url[i-1] == pattern[j-1]) {
                dp[i][j] = dp[i-1][j-1];
            } else if (pattern[j-1] == '*'){
                dp[i][j] = dp[i-1][j] || dp[i][j-1];
            } else if (pattern[j-1]=='|') {
                dp[i][j] = dp[i][j-1] || ((url[i-1]!='/')?(dp[i-1][j]):false);
            }
        }
    }

    /* Print the dp array in case debugging */
    for (i=0;i<url_len+1;i++) 
    {
        TM_PRINTF("\n");
        for (j=0;j<writeIndex+1;j++){
            TM_PRINTF("%d ",dp[i][j]);
        }
    }
 
    if (dp[url_len][writeIndex]){
        TM_PRINTF("Match\n");
    } else {
        TM_PRINTF("No Match\n");
    }

    return dp[url_len][writeIndex]; 
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Function that does the URL pattern match based on SELF algorithm
 *
 * @Param fp
 */
/* ----------------------------------------------------------------------------*/
static void self_pattern_match(char * url, int thread_num)
{
    int i,j, wildcard_index=-1;
    char new_pattern[PATTERN_STRING_MAX_LENGTH] = {'\0'}, temp_pattern[PATTERN_STRING_MAX_LENGTH] = {'\0'}; 
    bool is_first_pattern_match = true;

    /* Read URL from the file */
    if (url != NULL){
         TM_PRINTF("Enter thread: %d\n", thread_num);
         // Added for testing
         //usleep(10000);
         url[strlen(url) - 1] = '\0';
         is_first_pattern_match = true;
         /* Iterate through each set */
         for (i=0;i<num_sets;i++){
             /* Iterate through each pattern */ 
           for (j=0;j<config_pattern[i].num_patterns;j++){
                wildcard_index = -1;
                memset(new_pattern, 0, PATTERN_STRING_MAX_LENGTH);
                create_new_pattern(config_pattern[i].pattern[j],temp_pattern, SELF);
                strncpy(new_pattern, temp_pattern, PATTERN_STRING_MAX_LENGTH);

                if (true == match_needs_pattern_change(temp_pattern, &wildcard_index, SELF)) {
                    modify_self_pattern_string(temp_pattern, new_pattern, wildcard_index);
                }
                
                if (self_match(url,new_pattern)) {
                    print_url_match_pattern(url, config_pattern[i].pattern[j], i, &is_first_pattern_match);
                }
           }
        }
        if (false==is_first_pattern_match) {
            printf("\n");
        }
    } else {
        fprintf(stderr, "URL NULL, threadid %d\n", thread_num);
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Wrapper that is called from main to do the URL pattern match
 *
 * @Param fp
 * @Param type
 */
/* ----------------------------------------------------------------------------*/
static void pattern_match(char * url, MATCH_TYPE type, int thread_num)
{
    switch(type) {
        case POSIX:
            posix_pattern_match(url, thread_num); 
            break;

        case SELF:
            self_pattern_match(url, thread_num);
            break;

        default:
            break;
    }
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Add to the buffer
 *
 * @Param url
 */
/* ----------------------------------------------------------------------------*/
void insertbuffer(char * url) {
    if (buffer_index < NUM_BUFFERS) {
        strncpy(&buffer[buffer_index++][0],url,BUFF_SIZE);
    } else {
        printf("Buffer overflow\n");
    }
}
 
/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Dequeue the value from buffer
 *
 * @Returns   
 */
/* ----------------------------------------------------------------------------*/
char * dequeuebuffer() {
    if (buffer_index > 0) {
        return buffer[--buffer_index]; // buffer_index-- would be error!
    } else {
        printf("Buffer underflow\n");
    }
    return NULL;
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  worker thread that does the pattern match after deque the URL from buffer
 * There is a busy wait added when the sig handler is received. This will wait till
 * the recompilation is done.
 * @Param arg
 *
 * @Returns   
 */
/* ----------------------------------------------------------------------------*/
void *worker_thread(void * arg){
    struct thread_info * tinfo = arg;
    int sval;
    char * url; 

    printf("Worker Thread num: %d Thread algo %d\n", tinfo->thread_num, tinfo->algo);
    while(!fileRead_end || (!sem_getvalue(&full_sem, &sval) && sval>0)) {
        sem_wait(&full_sem);
        pthread_mutex_lock(&buffer_lock); 
        url = dequeuebuffer();        
        pthread_mutex_unlock(&buffer_lock);
        while(is_sighandler_rcvd){
            printf("thread id : %d, is sleeping\n", tinfo->thread_num);
            sleep(1);
            if (!is_sighandler_rcvd) {
                printf("thread id : %d, is awake\n", tinfo->thread_num);
                break;
            }
        }
        //printf("Read next url %s\n ", url);
        pattern_match(url, tinfo->algo, tinfo->thread_num);	
        sem_post(&empty_sem);
    }

    printf("exit thread: %d\n ", tinfo->thread_num); 
    pthread_exit(0);
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  fileRead thread that reads the file and adds url to the buffer.
 *      This is the producer thread
 * @Param arg
 *
 * @Returns   
 */
/* ----------------------------------------------------------------------------*/
void *fileRead_thread(void * arg){
    FILE *fp = (FILE*)arg;
    char url[BUFF_SIZE];

    printf("fileRead_thread \n");
    while(fgets(url, BUFF_SIZE, fp) != NULL) {
        sem_wait(&empty_sem);
        pthread_mutex_lock(&buffer_lock); 
        insertbuffer(url);        
        pthread_mutex_unlock(&buffer_lock);
        sem_post(&full_sem);
    }
    fileRead_end = true;
    pthread_exit(0);
}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  signal thread invoked to recompile the pattern
 *
 * @Param arg
 *
 * @Returns   
 */
/* ----------------------------------------------------------------------------*/
void *signal_thread(void *arg){
    xmlDocPtr       document;
    xmlNodePtr      root, first_child;

    printf("Enter signal_thread\n");

    printf("Enter signal_thread recompute\n");

    free_pattern_allocated_memory();
    document = xmlReadFile(configFile, NULL, 0);
    root = xmlDocGetRootElement(document);
    first_child = root->xmlChildrenNode;

    construct_pattern_from_xml(document, first_child);
    if (debug_enabled) {
        print_xml_pattern();
    }

    pthread_exit(0);

}

/* --------------------------------------------------------------------------*/
/**
 * @Synopsis  Sig handler for SIGUSR1
 * sleep of 5 sec added to wait for the current pattern match to finish.
 *
 * @Param signum
 */
/* ----------------------------------------------------------------------------*/
void my_handler(int signum)
{
    pthread_t sig_handler_thread_id;

    if (signum == SIGUSR1)
    {
        printf("Received SIGUSR1!\n");
        printf("Recompile the pattern\n");
        is_sighandler_rcvd = true; 
        sleep(5);

        if (pthread_create(&sig_handler_thread_id, NULL, signal_thread, NULL) != 0) {
            fprintf(stderr, "pthread_create failed!\n");
        }
        pthread_join(sig_handler_thread_id, NULL);
 
        is_sighandler_rcvd = false;
        printf("sig handler done\n");
    }
}

int main(int argc, char **argv)
{
    xmlDocPtr       document;
    xmlNodePtr      root, first_child;
    char            *urlFile;
    MATCH_TYPE algo;
    FILE * fp;
    clock_t start_time, end_time; 
    double time_taken;
    bool measure_time = false;
    int num_threads=1;

    if (argc < 4) {
        fprintf(stderr, "Usage: url-engine <posix|self> config.xml urlFile.txt thread 3\n");
        return 1;
    }
    
    if (!strcmp(argv[1],"posix")) {
        algo = POSIX;
    } else if (!strcmp(argv[1],"self")){ 
        algo = SELF;
    } else {
        fprintf(stderr, "posix|self\n");    
        return 1;
    }

    configFile = argv[2];
    urlFile = argv[3];
    if (argc >4 && !strcmp(argv[4], "thread")) {
       num_threads = atoi(argv[5]); 
    }

    if (argc>6 && !strcmp(argv[6],"debug_enable")){
        debug_enabled = true;
    }
    
    if (argc>6 && !strcmp(argv[6],"calc_time")){
        measure_time = true;
    }
    
    fp = fopen(urlFile, "r");
    if (fp == NULL){
        fprintf(stderr,"Could not open file %s",urlFile);
        return 1;
    }

    document = xmlReadFile(configFile, NULL, 0);
    root = xmlDocGetRootElement(document);
    first_child = root->xmlChildrenNode;

    construct_pattern_from_xml(document, first_child);
    if (debug_enabled) {
        print_xml_pattern();
    }

	int i, s;
    struct thread_info *tinfo;	
    signal(SIGUSR1, my_handler);
    pthread_t fileRead_threadid;

    if (1 < num_threads) {
        tinfo = calloc(num_threads, sizeof(struct thread_info));
        if (NULL == tinfo) {
                fprintf(stderr,"calloc error\n");
                return EXIT_FAILURE;
        }
        pthread_mutex_init (&lock, NULL);
        pthread_mutex_init(&buffer_lock, NULL);

        sem_init(&empty_sem, 0, NUM_BUFFERS);
        sem_init(&full_sem, 0, 0);

        //file read thread
        if (pthread_create(&fileRead_threadid, NULL, fileRead_thread, fp) != 0) {
            fprintf(stderr, "pthread_create failed fieRead thread!\n");
            return EXIT_FAILURE;
        }
       
        for (i = 0; i < num_threads; i++) {
            tinfo[i].thread_num = i+1;
            tinfo[i].algo = algo;
            if (pthread_create(&tinfo[i].thread_id, NULL, worker_thread, &tinfo[i]) != 0) {
                    fprintf(stderr, "pthread_create failed!\n");
                    return EXIT_FAILURE;
            }
        }

        for (i = 0; i < num_threads; i++) {
            if ((s=pthread_join(tinfo[i].thread_id, NULL)) != 0) {
                    fprintf(stderr,"pthread_join failed\n");
                    return EXIT_FAILURE;
            }
        }
        free(tinfo);
    }  else {
        start_time = clock();
        char url[BUFF_SIZE];
        while(fgets(url, BUFF_SIZE, fp) != NULL) {
            pattern_match(url, algo,1);
        }
        end_time = clock();
    }

    if (measure_time) {
        time_taken = ((double)(end_time - start_time))/CLOCKS_PER_SEC; // in seconds 
        printf("Time taken is %f sec\n", time_taken);
    }

    printf("\n");
    sem_destroy(&empty_sem);
    sem_destroy(&full_sem);
    pthread_mutex_destroy(&buffer_lock);
    pthread_mutex_destroy(&lock);

    free_pattern_allocated_memory();
    fclose(fp);

    return 0;
}    
