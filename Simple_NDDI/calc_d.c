#include "qe_nddi.h"


float calc_d(qe_args *args){
   return (args->b*args->b - (4 * args->a * args->c)); 
}
