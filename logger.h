#include "param.h"
void log_print(char* log_file, char* filename, int line, char *fmt,...);
#define LOG_PRINT(...) log_print(LOG_FILE,__FILE__, __LINE__, ##__VA_ARGS__)
