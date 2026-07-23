#ifndef DELTARUNE_VITA_BORDERS_H
#define DELTARUNE_VITA_BORDERS_H

void VitaBorders_init(int chapter);
void VitaBorders_updateRoom(const char* roomName);
void VitaBorders_cycleCurrent(int direction);
void VitaBorders_draw(int windowW, int windowH);
int VitaBorders_filesAvailable(void);
void VitaBorders_shutdown(void);

#endif
