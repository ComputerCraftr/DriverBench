#include "db_process_environment.h"

#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char **environ;
#endif

char *const *db_process_environment(void) {
#ifdef __APPLE__
    return *_NSGetEnviron();
#else
    return environ;
#endif
}
