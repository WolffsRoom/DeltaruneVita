#include <assert.h>
#include <stdio.h>
#include "../src/butterscotch/src/board_screen_cache.h"
int main(void) {
    BoardScreenCache c[24]={0};
    for(unsigned frame=0;frame<20;frame++) {
        unsigned copies=0;
        for(unsigned i=0;i<24;i++)
            copies+=!BoardScreenCache_reuse(&c[i],frame,i,true,0,i+1,20,40,128,96);
        assert(copies==(frame==0 ? 24 : 12));
    }
    BoardScreenCache s={0};
    assert(!BoardScreenCache_reuse(&s,0,0,true,0,1,20,40,128,96));
    assert(BoardScreenCache_reuse(&s,1,0,true,0,1,20,40,128,96));
    assert(!BoardScreenCache_reuse(&s,3,0,true,0,1,20,40,128,96)); // hidden
    assert(!BoardScreenCache_reuse(&s,4,0,true,0,1,20,40,128,96));
    assert(!BoardScreenCache_reuse(&s,5,0,true,0,1,21,40,128,96)); // camera moved
    assert(!BoardScreenCache_reuse(&s,6,0,true,0,1,21,40,128,96));
    assert(!BoardScreenCache_reuse(&s,7,0,false,0,1,21,40,128,96)); // lost target
    assert(!BoardScreenCache_reuse(&s,8,0,true,0,1,21,40,128,96));
    assert(!BoardScreenCache_reuse(&s,9,0,true,0,2,21,40,128,96)); // replacement
    assert(!BoardScreenCache_reuse(&s,10,0,true,0,2,21,40,128,96));
    assert(!BoardScreenCache_reuse(&s,11,0,true,0,2,21,40,256,96)); // resized
    puts("board_screen_cache: PASS");
    return 0;
}
