#ifndef __PERCEPTRON_H__
#define __PERCEPTRON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "bp/bp.h"

/*************Interface to Scarab***************/
void bp_perceptron_init(void);
void bp_perceptron_timestamp(Op*);
uns8 bp_perceptron_pred(Op*);
void bp_perceptron_spec_update(Op*);
void bp_perceptron_update(Op*);
void bp_perceptron_retire(Op*);
void bp_perceptron_recover(Recovery_Info*);
uns8 bp_perceptron_full(uns);

#ifdef __cplusplus
}
#endif


#endif  // __PERCEPTRON_H__