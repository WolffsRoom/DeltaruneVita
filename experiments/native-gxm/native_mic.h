#ifndef DELTARUNEVITA_NATIVE_MIC_H
#define DELTARUNEVITA_NATIVE_MIC_H
void VitaMic_start(void);
void VitaMic_request(int enabled);
int VitaMic_isAvailable(void);
int VitaMic_getLevel(void);
void VitaMic_stop(void);
#endif
