#include "guest_image.h"
#include "broken_bond_init.h"

extern "C" const rex::PPCImageInfo* broken_bond_GetImageInfo() {
  return &PPCImageConfig;
}
