#ifndef TYPES_H
#define TYPES_H

typedef enum request_type
{
    INSTALL = 0,  
    REMOVE  = 1,  
    LIST    = 2,  
    INFO    = 3,  
} request_type_t;

typedef enum package_type
{
    EXECUTABLE = 0,
    SERVICE    = 1,
} package_type_t;

typedef struct package
{
    char*          name;           
    char*          location;       
    char*          related_files;  
    char*          install_spec;   
    int            int_ver;        
    char*          str_ver;        
    package_type_t type;
} package_t;
typedef struct request
{
    request_type_t t;
    char           data[124]; 
} request_t;


#define RESPONSE_MSG_SIZE 240

typedef struct response
{
    int  status;                     
    int  data_len;                   
    char message[RESPONSE_MSG_SIZE]; 
} response_t;

#endif 