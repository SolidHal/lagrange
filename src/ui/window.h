#pragma once

#include "widget.h"

#include <the_Foundation/defs.h>
#include <SDL_events.h>
#include <SDL_render.h>
#include <SDL_video.h>

iDeclareType(Window)
iDeclareTypeConstruction(Window)

struct Impl_Window {
    SDL_Window *  win;
    SDL_Renderer *render;
    iWidget *     root;
    float         pixelRatio;
    float         uiScale;
    uint32_t      frameTime;
    double        presentTime;
};

iBool       processEvent_Window     (iWindow *, const SDL_Event *);
void        draw_Window             (iWindow *);
void        resize_Window           (iWindow *, int w, int h);
void        setUiScale_Window       (iWindow *, float uiScale);

iInt2       rootSize_Window         (const iWindow *);
float       uiScale_Window          (const iWindow *);
iInt2       coord_Window            (const iWindow *, int x, int y);
iInt2       mouseCoord_Window       (const iWindow *);
uint32_t    frameTime_Window        (const iWindow *);

iWindow *   get_Window              (void);
