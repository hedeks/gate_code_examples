#include "qe_nddi.h"
#include <math.h>

void solve_qe (qe_args *args, qe_result *result){
   float d = calc_d(args);

   if (d < 0){
      result->flag = QE_ZERO_ROOTS;   
   } else if (d == 0) {
      result->x1 = -args->b / (2 * args->a);
      result->flag = QE_ONE_ROOT;
   } else if (d > 0){
      result->x1 = (-args->b - sqrt(d))/(2*args->a);
      result->x2 = (-args->b + sqrt(d)) / (2*args->a);
      result->flag = QE_TWO_ROOTS;
   }
}
