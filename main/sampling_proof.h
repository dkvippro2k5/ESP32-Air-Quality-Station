#ifndef SAMPLING_PROOF_H
#define SAMPLING_PROOF_H

#include <stdint.h>

void sampling_proof_init(uint32_t expected_period_us, uint32_t measurement_window_us);
void sampling_proof_record_sample(void);
void sampling_proof_print_summary(void);

#endif
