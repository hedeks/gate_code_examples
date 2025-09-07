#define QE_NO_RESULT      0
#define QE_ZERO_ROOTS     1
#define QE_ONE_ROOT       2
#define QE_TWO_ROOTS      3

typedef struct {
   int flag; /* 0 when no result, 1 when 0 roots, 2 when 1 root, 3 when 2 roots */
   float d;
   float x1;
   float x2;
} qe_result; /* quadratic equation result */

typedef struct {
   float a;
   float b;
   float c;
} qe_args; /* quadratic equation arguments */

		/* solve quadratic equation */
void solve_qe (qe_args *args, qe_result *result);
float calc_d (qe_args *args);

