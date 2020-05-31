#ifndef __SNA_RELOCS_H__
#define __SNA_RELOCS_H__

#include <inttypes.h>

#ifdef __cplusplus
extern "C"  {
#endif

void sna_apply_relocs(const uint8_t *sna, uint8_t *rom);

#ifdef __cplusplus
}
#endif

#endif
