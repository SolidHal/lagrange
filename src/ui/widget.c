#include "widget.h"

#include "app.h"
#include "command.h"
#include "paint.h"
#include "util.h"
#include "window.h"

#include <the_Foundation/ptrset.h>
#include <SDL_mouse.h>
#include <stdarg.h>

iDeclareType(RootData)

struct Impl_RootData {
    iWidget *hover;
    iWidget *mouseGrab;
    iWidget *focus;
    iPtrSet *onTop;
    iPtrSet *pendingDestruction;
};

static iRootData rootData_;

iPtrSet *onTop_RootData_(void) {
    if (!rootData_.onTop) {
        rootData_.onTop = new_PtrSet();
    }
    return rootData_.onTop;
}

void destroyPending_Widget(void) {
    iForEach(PtrSet, i, rootData_.pendingDestruction) {
        iWidget *widget = *i.value;
        remove_PtrSet(onTop_RootData_(), widget);
        iRelease(removeChild_Widget(widget->parent, widget));
        remove_PtrSetIterator(&i);
    }
}

iDefineObjectConstruction(Widget)

void init_Widget(iWidget *d) {
    init_String(&d->id);
    d->flags          = 0;
    d->rect           = zero_Rect();
    d->bgColor        = none_ColorId;
    d->frameColor     = none_ColorId;
    d->children       = NULL;
    d->parent         = NULL;
    d->commandHandler = NULL;
}

void deinit_Widget(iWidget *d) {
    iReleasePtr(&d->children);
    deinit_String(&d->id);
}

static void aboutToBeDestroyed_Widget_(iWidget *d) {
    if (isFocused_Widget(d)) {
        setFocus_Widget(NULL);
        return;
    }
    if (isHover_Widget(d)) {
        rootData_.hover = NULL;
    }
    iForEach(ObjectList, i, d->children) {
        aboutToBeDestroyed_Widget_(as_Widget(i.object));
    }
}

void destroy_Widget(iWidget *d) {
    aboutToBeDestroyed_Widget_(d);
    if (!rootData_.pendingDestruction) {
        rootData_.pendingDestruction = new_PtrSet();
    }
    insert_PtrSet(rootData_.pendingDestruction, d);
    postRefresh_App();
}

void setId_Widget(iWidget *d, const char *id) {
    setCStr_String(&d->id, id);
}

const iString *id_Widget(const iWidget *d) {
    return &d->id;
}

int flags_Widget(const iWidget *d) {
    return d->flags;
}

void setFlags_Widget(iWidget *d, int flags, iBool set) {
    iChangeFlags(d->flags, flags, set);
    if (flags & keepOnTop_WidgetFlag) {
        if (set) {
            insert_PtrSet(onTop_RootData_(), d);
        }
        else {
            remove_PtrSet(onTop_RootData_(), d);
        }
    }
}

void setPos_Widget(iWidget *d, iInt2 pos) {
    d->rect.pos = pos;
}

void setSize_Widget(iWidget *d, iInt2 size) {    
    d->rect.size = size;
    setFlags_Widget(d, fixedSize_WidgetFlag, iTrue);
}

void setBackgroundColor_Widget(iWidget *d, int bgColor) {
    d->bgColor = bgColor;
}

void setFrameColor_Widget(iWidget *d, int frameColor) {
    d->frameColor = frameColor;
}

void setCommandHandler_Widget(iWidget *d, iBool (*handler)(iWidget *, const char *)) {
    d->commandHandler = handler;
}

static int numExpandingChildren_Widget_(const iWidget *d) {
    int count = 0;
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        if (flags_Widget(child) & expand_WidgetFlag) {
            count++;
        }
    }
    return count;
}

static int widestChild_Widget_(const iWidget *d) {
    int width = 0;
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        width = iMax(width, child->rect.size.x);
    }
    return width;
}

static void setWidth_Widget_(iWidget *d, int width) {
    if (~d->flags & fixedWidth_WidgetFlag) {
        d->rect.size.x = width;
    }
}

static void setHeight_Widget_(iWidget *d, int height) {
    if (~d->flags & fixedHeight_WidgetFlag) {
        d->rect.size.y = height;
    }
}

iLocalDef iBool isCollapsed_Widget_(const iWidget *d) {
    return (d->flags & (hidden_WidgetFlag | collapse_WidgetFlag)) ==
           (hidden_WidgetFlag | collapse_WidgetFlag);
}

void arrange_Widget(iWidget *d) {
    if (isCollapsed_Widget_(d)) {
        setFlags_Widget(d, wasCollapsed_WidgetFlag, iTrue);
        return;
    }
    if (d->flags & moveToParentRightEdge_WidgetFlag) {
        d->rect.pos.x = width_Rect(d->parent->rect) - width_Rect(d->rect);
    }
    if (d->flags & resizeToParentWidth_WidgetFlag) {
        setWidth_Widget_(d, d->parent->rect.size.x);
    }
    if (d->flags & resizeToParentHeight_WidgetFlag) {
        setHeight_Widget_(d, d->parent->rect.size.y);
    }
    /* The rest of the arrangement depends on child widgets. */
    if (!d->children) {
        return;
    }
    /* Resize children to fill the parent widget. */
    const size_t childCount = size_ObjectList(d->children);
    if (d->flags & resizeChildren_WidgetFlag) {
        /* Collapse hidden children. */
        iForEach(ObjectList, c, d->children) {
            iWidget *child = as_Widget(c.object);
            if (isCollapsed_Widget_(child)) {
                if (d->flags & arrangeHorizontal_WidgetFlag) {
                    setWidth_Widget_(child, 0);
                }
                if (d->flags & arrangeVertical_WidgetFlag) {
                    setHeight_Widget_(child, 0);
                }
            }
            else if (child->flags & wasCollapsed_WidgetFlag) {
                setFlags_Widget(child, wasCollapsed_WidgetFlag, iFalse);
                /* Undo collapse and determine the normal size again. */
                if (child->flags & arrangeSize_WidgetFlag) {
                    arrange_Widget(child);
                }
            }
        }
        const int expCount = numExpandingChildren_Widget_(d);
        /* Only resize the expanding children, not touching the others. */
        if (expCount > 0) {
            iInt2 avail = d->rect.size;
            iConstForEach(ObjectList, i, d->children) {
                const iWidget *child = constAs_Widget(i.object);
                if (~child->flags & expand_WidgetFlag) {
                    subv_I2(&avail, child->rect.size);
                }
            }
            avail = divi_I2(avail, expCount);
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (isCollapsed_Widget_(child)) continue;
                if (child->flags & expand_WidgetFlag) {
                    if (d->flags & arrangeHorizontal_WidgetFlag) {
                        setWidth_Widget_(child, avail.x);
                        setHeight_Widget_(child, d->rect.size.y);
                    }
                    else if (d->flags & arrangeVertical_WidgetFlag) {
                        setWidth_Widget_(child, d->rect.size.x);
                        setHeight_Widget_(child, avail.y);
                    }
                }
                else {
                    /* Fill the off axis, though. */
                    if (d->flags & arrangeHorizontal_WidgetFlag) {
                        setHeight_Widget_(child, d->rect.size.y);
                    }
                    else if (d->flags & arrangeVertical_WidgetFlag) {
                        setWidth_Widget_(child, d->rect.size.x);
                    }
                }
            }
        }
        else {
            /* Evenly size all children. */
            iInt2 childSize = d->rect.size;
            if (d->flags & arrangeHorizontal_WidgetFlag) {
                childSize.x /= childCount;
            }
            else if (d->flags & arrangeVertical_WidgetFlag) {
                childSize.y /= childCount;
            }
            iForEach(ObjectList, i, d->children) {
                iWidget *child = as_Widget(i.object);
                if (!isCollapsed_Widget_(child)) {
                    setWidth_Widget_(child, childSize.x);
                    setHeight_Widget_(child, childSize.y);
                }
            }
        }
    }
    if (d->flags & resizeChildrenToWidestChild_WidgetFlag) {
        const int widest = widestChild_Widget_(d);
        iForEach(ObjectList, i, d->children) {
            setWidth_Widget_(as_Widget(i.object), widest);
        }
    }
    iInt2 pos = zero_I2();
    iForEach(ObjectList, i, d->children) {
        iWidget *child = as_Widget(i.object);
        arrange_Widget(child);
        if (d->flags & (arrangeHorizontal_WidgetFlag | arrangeVertical_WidgetFlag)) {
            child->rect.pos = pos;
            if (d->flags & arrangeHorizontal_WidgetFlag) {
                pos.x += child->rect.size.x;
            }
            else {
                pos.y += child->rect.size.y;
            }
        }
    }
    /* Update the size of the widget according to the arrangement. */
    if (d->flags & arrangeSize_WidgetFlag) {
        iRect bounds = zero_Rect();
        iConstForEach(ObjectList, i, d->children) {
            const iWidget *child = constAs_Widget(i.object);
            if (isEmpty_Rect(bounds)) {
                bounds = child->rect;
            }
            else {
                bounds = union_Rect(bounds, child->rect);
            }
        }
        if (d->flags & arrangeWidth_WidgetFlag) {
            setWidth_Widget_(d, bounds.size.x);
            /* Parent size changed, must update the children.*/
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (child->flags &
                    (resizeToParentWidth_WidgetFlag | moveToParentRightEdge_WidgetFlag)) {
                    arrange_Widget(child);
                }
            }
        }
        if (d->flags & arrangeHeight_WidgetFlag) {
            setHeight_Widget_(d, bounds.size.y);
            /* Parent size changed, must update the children.*/
            iForEach(ObjectList, j, d->children) {
                iWidget *child = as_Widget(j.object);
                if (child->flags & resizeToParentHeight_WidgetFlag) {
                    arrange_Widget(child);
                }
            }
        }
    }
}

iRect bounds_Widget(const iWidget *d) {
    iRect bounds = d->rect;
    for (const iWidget *w = d->parent; w; w = w->parent) {
        addv_I2(&bounds.pos, w->rect.pos);
    }
    return bounds;
}

iInt2 localCoord_Widget(const iWidget *d, iInt2 coord) {
    for (const iWidget *w = d; w; w = w->parent) {
        subv_I2(&coord, w->rect.pos);
    }
    return coord;
}

iBool contains_Widget(const iWidget *d, iInt2 coord) {
    const iRect bounds = { zero_I2(), d->rect.size };
    return contains_Rect(bounds, localCoord_Widget(d, coord));
}

iLocalDef iBool isKeyboardEvent_(const SDL_Event *ev) {
    return (ev->type == SDL_KEYUP || ev->type == SDL_KEYDOWN || ev->type == SDL_TEXTINPUT);
}

iLocalDef iBool isMouseEvent_(const SDL_Event *ev) {
    return (ev->type == SDL_MOUSEWHEEL || ev->type == SDL_MOUSEMOTION ||
            ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEBUTTONDOWN);
}

static iBool filterEvent_Widget_(const iWidget *d, const SDL_Event *ev) {
    const iBool isKey   = isKeyboardEvent_(ev);
    const iBool isMouse = isMouseEvent_(ev);
    if (d->flags & disabled_WidgetFlag) {
        if (isKey || isMouse) return iFalse;
    }
    if (d->flags & hidden_WidgetFlag) {
        if (isMouse) return iFalse;
    }
    return iTrue;
}

void unhover_Widget(void) {
    rootData_.hover = NULL;
}

iBool dispatchEvent_Widget(iWidget *d, const SDL_Event *ev) {
    if (!d->parent) {
        if (ev->type == SDL_MOUSEMOTION) {
            /* Hover widget may change. */
            rootData_.hover = NULL;
        }
        if (rootData_.focus && isKeyboardEvent_(ev)) {
            /* Root dispatches keyboard events directly to the focused widget. */
            if (dispatchEvent_Widget(rootData_.focus, ev)) {
                return iTrue;
            }
        }
        /* Root offers events first to widgets on top. */
        iForEach(PtrSet, i, rootData_.onTop) {
            iWidget *widget = *i.value;
            if (isVisible_Widget(widget) && dispatchEvent_Widget(widget, ev)) {
                return iTrue;
            }
        }
    }
    else if (ev->type == SDL_MOUSEMOTION && !rootData_.hover &&
             flags_Widget(d) & hover_WidgetFlag && ~flags_Widget(d) & hidden_WidgetFlag &&
             ~flags_Widget(d) & disabled_WidgetFlag) {
        if (contains_Widget(d, init_I2(ev->motion.x, ev->motion.y))) {
            rootData_.hover = d;
        }
    }
    if (filterEvent_Widget_(d, ev)) {
        /* Children may handle it first. Done in reverse so children drawn on top get to
           handle the events first. */
        iReverseForEach(ObjectList, i, d->children) {
            iWidget *child = as_Widget(i.object);
            if (child == rootData_.focus && isKeyboardEvent_(ev)) {
                continue; /* Already dispatched. */
            }
            if (isVisible_Widget(child) && child->flags & keepOnTop_WidgetFlag) {
                /* Already dispatched. */
                continue;
            }
            if (dispatchEvent_Widget(child, ev)) {
                return iTrue;
            }
        }
        if (class_Widget(d)->processEvent(d, ev)) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool processEvent_Widget(iWidget *d, const SDL_Event *ev) {
    if (ev->type == SDL_KEYDOWN) {
        if (ev->key.keysym.sym == SDLK_TAB) {
            setFocus_Widget(findFocusable_Widget(focus_Widget(),
                                                 ev->key.keysym.mod & KMOD_SHIFT
                                                     ? backward_WidgetFocusDir
                                                     : forward_WidgetFocusDir));
            return iTrue;
        }
    }
    switch (ev->type) {
        case SDL_USEREVENT: {
            if (ev->user.code == command_UserEventCode && d->commandHandler &&
                d->commandHandler(d, ev->user.data1)) {
                return iTrue;
            }
            break;
        }
    }
    return iFalse;
}

void draw_Widget(const iWidget *d) {
    if (d->flags & hidden_WidgetFlag) return;
    if (d->bgColor >= 0 || d->frameColor >= 0) {
        const iRect rect = bounds_Widget(d);
        iPaint p;
        init_Paint(&p);
        if (d->bgColor >= 0) {
            fillRect_Paint(&p, rect, d->bgColor);
        }
        if (d->frameColor >= 0) {
            drawRectThickness_Paint(&p, rect, gap_UI / 4, d->frameColor);
        }
    }
    iConstForEach(ObjectList, i, d->children) {
        const iWidget *child = constAs_Widget(i.object);
        if (~child->flags & keepOnTop_WidgetFlag && ~child->flags & hidden_WidgetFlag) {
            class_Widget(child)->draw(child);
        }
    }
    /* Root draws the on-top widgets on top of everything else. */
    if (!d->parent) {
        iConstForEach(PtrSet, i, onTop_RootData_()) {
            draw_Widget(*i.value);
        }
    }
}

iAny *addChild_Widget(iWidget *d, iAnyObject *child) {
    return addChildPos_Widget(d, child, back_WidgetAddPos);
}

iAny *addChildPos_Widget(iWidget *d, iAnyObject *child, enum iWidgetAddPos addPos) {
    iAssert(child);
    iAssert(d != child);
    iWidget *widget = as_Widget(child);
    iAssert(!widget->parent);
    if (!d->children) {
        d->children = new_ObjectList();
    }
    if (addPos == back_WidgetAddPos) {
        pushBack_ObjectList(d->children, widget); /* ref */
    }
    else {
        pushFront_ObjectList(d->children, widget); /* ref */
    }
    widget->parent = d;
    return child;
}

iAny *addChildFlags_Widget(iWidget *d, iAnyObject *child, int childFlags) {
    setFlags_Widget(child, childFlags, iTrue);
    return addChild_Widget(d, child);
}

iAny *removeChild_Widget(iWidget *d, iAnyObject *child) {
    ref_Object(child);
    iBool found = iFalse;
    iForEach(ObjectList, i, d->children) {
        if (i.object == child) {
            remove_ObjectListIterator(&i);
            found = iTrue;
            break;
        }
    }
    iAssert(found);
    ((iWidget *) child)->parent = NULL;
    postRefresh_App();
    return child;
}

iAny *child_Widget(iWidget *d, size_t index) {
    iForEach(ObjectList, i, d->children) {
        if (index-- == 0) {
            return i.object;
        }
    }
    return NULL;
}

iAny *findChild_Widget(const iWidget *d, const char *id) {
    if (cmp_String(id_Widget(d), id) == 0) {
        return iConstCast(iAny *, d);
    }
    iConstForEach(ObjectList, i, d->children) {
        iAny *found = findChild_Widget(constAs_Widget(i.object), id);
        if (found) return found;
    }
    return NULL;
}

size_t childCount_Widget(const iWidget *d) {
    if (!d->children) return 0;
    return size_ObjectList(d->children);
}

iBool isVisible_Widget(const iWidget *d) {
    for (const iWidget *w = d; w; w = w->parent) {
        if (w->flags & hidden_WidgetFlag) {
            return iFalse;
        }
    }
    return iTrue;
}

iBool isDisabled_Widget(const iWidget *d) {
    for (const iWidget *w = d; w; w = w->parent) {
        if (w->flags & disabled_WidgetFlag) {
            return iTrue;
        }
    }
    return iFalse;
}

iBool isFocused_Widget(const iWidget *d) {
    return rootData_.focus == d;
}

iBool isHover_Widget(const iWidget *d) {
    return rootData_.hover == d;
}

iBool isSelected_Widget(const iWidget *d) {
    return (d->flags & selected_WidgetFlag) != 0;
}

iBool isCommand_Widget(const iWidget *d, const SDL_Event *ev, const char *cmd) {
    if (isCommand_UserEvent(ev, cmd)) {
        const iWidget *src = pointer_Command(command_UserEvent(ev));
        iAssert(!src || strstr(ev->user.data1, " ptr:"));
        return src == d || hasParent_Widget(src, d);
    }
    return iFalse;
}

iBool hasParent_Widget(const iWidget *d, const iWidget *someParent) {
    if (d) {
        for (const iWidget *w = d->parent; w; w = w->parent) {
            if (w == someParent) return iTrue;
        }
    }
    return iFalse;
}

void setFocus_Widget(iWidget *d) {
    if (rootData_.focus != d) {
        if (rootData_.focus) {
            iAssert(!contains_PtrSet(rootData_.pendingDestruction, rootData_.focus));
            postCommand_Widget(rootData_.focus, "focus.lost");
        }
        rootData_.focus = d;
        if (d) {
            iAssert(flags_Widget(d) & focusable_WidgetFlag);
            postCommand_Widget(d, "focus.gained");
        }
    }
}

iWidget *focus_Widget(void) {
    return rootData_.focus;
}

iWidget *hover_Widget(void) {
    return rootData_.hover;
}

static const iWidget *findFocusable_Widget_(const iWidget *d, const iWidget *startFrom,
                                            iBool *getNext, enum iWidgetFocusDir focusDir) {
    if (startFrom == d) {
        *getNext = iTrue;
        return NULL;
    }
    if ((d->flags & focusable_WidgetFlag) && isVisible_Widget(d) && !isDisabled_Widget(d) &&
        *getNext) {
        return d;
    }
    if (focusDir == forward_WidgetFocusDir) {
        iConstForEach(ObjectList, i, d->children) {
            const iWidget *found =
                findFocusable_Widget_(constAs_Widget(i.object), startFrom, getNext, focusDir);
            if (found) return found;
        }
    }
    else {
        iReverseConstForEach(ObjectList, i, d->children) {
            const iWidget *found =
                findFocusable_Widget_(constAs_Widget(i.object), startFrom, getNext, focusDir);
            if (found) return found;
        }
    }
    return NULL;
}

iAny *findFocusable_Widget(const iWidget *startFrom, enum iWidgetFocusDir focusDir) {
    iWidget *root = get_Window()->root;
    iBool getNext = (startFrom ? iFalse : iTrue);
    const iWidget *found = findFocusable_Widget_(root, startFrom, &getNext, focusDir);
    if (!found && startFrom) {
        getNext = iTrue;
        found = findFocusable_Widget_(root, NULL, &getNext, focusDir);
    }
    return iConstCast(iWidget *, found);
}

void setMouseGrab_Widget(iWidget *d) {
    if (rootData_.mouseGrab != d) {
        rootData_.mouseGrab = d;
        SDL_CaptureMouse(d != NULL);
    }
}

iWidget *mouseGrab_Widget(void) {
    return rootData_.mouseGrab;
}

void postCommand_Widget(const iWidget *d, const char *cmd, ...) {
    iString str;
    init_String(&str); {
        va_list args;
        va_start(args, cmd);
        vprintf_Block(&str.chars, cmd, args);
        va_end(args);
    }
    iBool isGlobal = iFalse;
    if (*cstr_String(&str) == '!')  {
        isGlobal = iTrue;
        remove_Block(&str.chars, 0, 1);
    }
    if (!isGlobal) {
        appendFormat_String(&str, " ptr:%p", d);
    }
    postCommandString_App(&str);
    deinit_String(&str);
}

void refresh_Widget(const iWidget *d) {
    /* TODO: Could be widget specific, if parts of the tree are cached. */
    iUnused(d);
    postRefresh_App();
}

iBeginDefineClass(Widget)
    .processEvent = processEvent_Widget,
    .draw         = draw_Widget,
iEndDefineClass(Widget)
