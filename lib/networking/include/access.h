#ifdef _WIN32
#include <io.h>
// allow windows compatibility for access
#define access _access
#define F_OK 0
#define W_OK 2
#define R_OK 4
#define sleep _sleep
#else
#include <unistd.h>
#endif