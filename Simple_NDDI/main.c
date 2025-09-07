#include <stdio.h>
#include "qe_nddi.h"

qe_args args = {0, 0, 0};
qe_result __attribute__((section(".v_component"))) result = {QE_NO_RESULT, 0, 0, 0};


int main (){   
  
   while (1){

      printf("Enter quadratic equation parameters: a, b, c: \n");
      scanf("%f%f%f", &args.a, &args.b, &args.c );

      solve_qe(&args, &result);
      printf("Struct adress: %p\n", (void*)&result);
      printf("Flag: %p\n", (void*)&result.flag);
      printf("Discriminant: %p\n", (void*)&result.d);

      switch(result.flag){
         case QE_NO_RESULT:
            printf("Something wrong\n");
            break;
         case QE_ZERO_ROOTS:
	    printf("Zero roots\n");
            break;
         case QE_ONE_ROOT:
	    printf("One root: x1=%f with adress=%p\n", result.x1, (void*)&result.x1);
	    break;
	 case QE_TWO_ROOTS:
	    printf("Two roots: x1=%f with adress=%p, x2=%f with adress %p\n", result.x1, (void*)&result.x1, result.x2, (void*)&result.x2);
	    break;

	 default:
            printf("Something wrong\n");
	    break; 
      }	
   }
}
