/* NUKLEAR KLEE begin (entire file) */
#define XLIB_ILLEGAL_ACCESS
#include <X11/Xlib.h>
#include <X11/X.h>
#include <stdlib.h>
#include <string.h>


#define LIMIT(val, lo, hi)      ( val=(val)>(hi)?(hi):((val)<(lo)?(lo):(val)) )
#define MAX_EVENT_COUNT 2
#define MIN_EVENT_COUNT 0
#define X_LIST_MAX_NAME_SIZE 128
typedef struct _KList {
  void *data;
  char name[X_LIST_MAX_NAME_SIZE];
  struct _KList *next;
  struct _KList *prev;
} KList;

KList  *g_display_list = NULL;
KList  *g_font_list    = NULL;
Pixmap  g_last_pixmap  = 0;
GC      g_gc           = NULL;
int     g_event_count = 0;

KList *getXListByName(KList* xlist_, char* name) {
  KList *xlist = xlist_;
  KList *prev_xlist = NULL;
  while (xlist != NULL) {
    if (strcmp(xlist->name, name)) {
      prev_xlist = xlist;
      xlist = xlist->next;
    } else {
      return xlist;
    }
  }
  return NULL;
}

KList *getXListByData(KList* xlist_, void* data) {
  KList *xlist = xlist_;
  KList *prev_xlist = NULL;
  while (xlist != NULL) {
    if (xlist->data == data) {
      prev_xlist = xlist;
      xlist = xlist->next;
    } else {
      return xlist;
    }
  }
  return NULL;
}

Display *getOrCreateDisplay(char* name) {
  KList *xlist = getXListByName(g_display_list, name);
  if (xlist)
    return (Display*)xlist->data;

  xlist = (KList*) malloc(sizeof(KList));
  xlist->data = (void*) malloc(sizeof(Display));
  Display *d = (Display*) xlist->data;
  memset(d, 0, sizeof(Display));
  strncpy(xlist->name, name, X_LIST_MAX_NAME_SIZE);
  xlist->next = g_display_list;
  xlist->prev = NULL;
  if (g_display_list)
    g_display_list->prev = xlist;
  else 
    g_display_list = xlist;

  d->fd = 29;
  d->default_screen = 0;
  Screen *screen = (Screen*) malloc(sizeof(Screen));
  memset(screen, 0, sizeof(Screen));
  screen->width = 800;
  screen->height = 600;
  screen->root = 0;
  Visual *visual = (Visual*) malloc(sizeof(Visual));
  screen->root_visual = visual;
  d->screens = screen;

  return d;
}

int destroyDisplayList(Display *display) {
  KList *xlist = getXListByData(g_display_list, display);
  if (xlist == NULL)
    return 0;

  if (xlist->prev == NULL) {
    g_display_list = xlist->next;
  } else {
    xlist->prev->next = xlist->next;
  }

  free(xlist->data);
  free(xlist);
  return 0;
}

XFontStruct *getOrCreateXFont(char* name) {
  KList *xlist = getXListByName(g_font_list, name);
  if (xlist)
    return (XFontStruct*)xlist->data;

  xlist = (KList*) malloc(sizeof(KList));
  xlist->data = (void*) malloc(sizeof(XFontStruct));
  XFontStruct *xf = (XFontStruct*) xlist->data;
  memset(xf, 0, sizeof(XFontStruct));
  strncpy(xlist->name, name, X_LIST_MAX_NAME_SIZE);
  xlist->next = g_font_list;
  xlist->prev = NULL;
  if (g_font_list)
    g_font_list->prev = xlist;
  else 
    g_font_list = xlist;

  return xf;
}

Display *nuklear_XOpenDisplay(char *display_name) {
  return getOrCreateDisplay(display_name);
}

int nuklear_XCloseDisplay(Display *display) {
  return destroyDisplayList(display);
}

KeySym nuklear_XStringToKeysym(char *string) {
  return XStringToKeysym(string);
  //return 12;
}

KeySym nuklear_XLookupKeysym(XKeyEvent *key_event, int index) {
  return key_event->keycode;
  //return XLookupKeysym(key_event, index);
}

XFontStruct* nuklear_XQueryFont(Display* display, GContext gc) {
  return getOrCreateXFont("default");
}

XFontStruct* nuklear_XLoadQueryFont(Display* display, char *string) {
  return getOrCreateXFont(string);
}

int nuklear_XParseGeometry(char *parsestring, 
                           int *x_return, int *y_return, 
                           unsigned int *width_return, 
                           unsigned int *height_return) {
  int res = XParseGeometry(parsestring, x_return, y_return, 
                           width_return, height_return);
  return res;
}

Window nuklear_XCreateWindow(Display *display, Window parent, 
                             int x, int y, 
                             unsigned int width, unsigned int height, 
                             unsigned int border_width, int depth, 
                             unsigned int class, Visual *visual, 
                             unsigned long valuemask, 
                             XSetWindowAttributes *attributes) {
  return parent+1;
}

Window nuklear_XCreateSimpleWindow(Display *display, Window parent, 
                                   int x, int y, 
                                   unsigned int width, unsigned int height, 
                                   unsigned int border_width, 
                                   unsigned long border, 
                                   unsigned long background) {
  return parent+1;
}

Pixmap nuklear_XCreateBitmapFromData(Display *display, Drawable d, char *data, 
                                     unsigned int width, unsigned int height) {
  g_last_pixmap += d;
  return g_last_pixmap;
}

Pixmap nuklear_XCreatePixmap(Display *display, Drawable d, 
                              unsigned int width, unsigned int height, 
                              unsigned int depth) {
  g_last_pixmap += d;
  return g_last_pixmap;
}

GC nuklear_XCreateGC(Display *display, Drawable d, 
                     unsigned long valuemask, XGCValues *values) {
  if (g_gc == NULL) 
    g_gc = (GC) malloc(sizeof(*g_gc));
  return g_gc;
}

Atom nuklear_XInternAtom(Display *display, char *atom_name, Bool only_if_exists) {
  return 1;
}



Cursor nuklear_XCreateFontCursor(Display *display, unsigned int shape) {
  return 1;
}

Cursor nuklear_XCreatePixmapCursor(Display *display, 
                           Pixmap source, Pixmap mask, 
                           XColor *foreground_color, 
                           XColor *background_color, 
                           unsigned int x, 
                           unsigned int y) {
  return 1;
}

Cursor nuklear_XCreateGlyphCursor(Display *display, Font source_font, 
                          Font mask_font, unsigned int source_char, 
                          unsigned int mask_char, 
                          XColor *foreground_color, XColor *background_color) {
  return 1;
}


int nuklear_XFlush(Display *display) {
  return 0;
}

int nuklear_XSync(Display *display, Bool discard) {
  return 0;
}


int nuklear_XEventsQueued(Display *display, int mode) {
  //int event_count;
  //klee_nuklear_make_symbolic(&event_count, "XEventsQueued_count");
  //LIMIT(event_count, MIN_EVENT_COUNT, MAX_EVENT_COUNT);
  //g_event_count = event_count;
  //return g_event_count;
  //g_event_count = MAX_EVENT_COUNT;
  
  //g_event_count = klee_nuklear_XEventsQueued();
  //return g_event_count;

  int event_count;
  klee_make_symbolic(&event_count, sizeof(int), "XEventsQueued_count");
  g_event_count = event_count;
  return g_event_count;
}

int nuklear_XPilotHash(unsigned char* v, int len) {
  int result = klee_nuklear_XPilotHash(v, len);
  return result;
}

int nuklear_XNextEvent(Display *display, XEvent *event_return) {
  if (g_event_count) {
    g_event_count--;
    XEvent event;
    klee_make_symbolic(&event, sizeof(XEvent), "XNextEvent_event");
    memcpy(event_return, &event, sizeof(XEvent));
  }
  return 0;
}

int nuklear_XPending(Display *display) {
  return 0;
}


/* NUKLEAR KLEE end (entire file) */
