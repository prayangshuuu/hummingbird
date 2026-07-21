/* tensor_internal.h — private to the `tensor` module.
 *
 * Nothing here is visible to other modules; only tensor.c includes it. The
 * tensor data model keeps its helpers file-static in tensor.c (overflow-checked
 * multiply, strided element copy, view seeding, layout refresh), so this header
 * currently adds no internal surface beyond re-exporting the public contract.
 * Declare shared internal structs / helper prototypes here if the module later
 * splits its implementation across more than one .c file.
 */
#ifndef HB_TENSOR_INTERNAL_H
#define HB_TENSOR_INTERNAL_H

#include "tensor/tensor.h"

/* (no cross-file internal declarations yet — helpers are static in tensor.c) */

#endif /* HB_TENSOR_INTERNAL_H */
