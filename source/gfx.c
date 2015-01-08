#ifdef _3DS
#include <3ds.h>
#endif

#include <string.h>

/* Function to draw sprite, from smea/3ds_hb_menu */
void gfxDrawSprite(gfxScreen_t screen, gfx3dSide_t side, u8* spriteData, u16 width, u16 height, s16 x, s16 y)
{
    if(!spriteData)return;

    u16 fbWidth, fbHeight;
    u8* fbAdr=gfxGetFramebuffer(screen, side, &fbWidth, &fbHeight);

    if(x+width<0 || x>=fbWidth)return;
    if(y+height<0 || y>=fbHeight)return;

    u16 xOffset=0, yOffset=0;
    u16 widthDrawn=width, heightDrawn=height;

    if(x<0)xOffset=-x;
    if(y<0)yOffset=-y;
    if(x+width>=fbWidth)widthDrawn=fbWidth-x;
    if(y+height>=fbHeight)heightDrawn=fbHeight-y;
    widthDrawn-=xOffset;
    heightDrawn-=yOffset;

    int j;
    for(j=yOffset; j<yOffset+heightDrawn; j++)
    {
        memcpy(&fbAdr[((x+xOffset)+(y+j)*fbWidth)*3], &spriteData[((xOffset)+(j)*width)*3], widthDrawn*3);
    }
}