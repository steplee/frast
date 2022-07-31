#include "displayImage.h"
#include "../image.h"

#include <unordered_map>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#define XK_LATIN1
#define XK_MISCELLANY
#include <X11/keysymdef.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_image.h>

#include <cassert>
#include <fmt/color.h>
#include <unistd.h>

static xcb_format_t *find_format_by_depth(const xcb_setup_t *setup,
                                          uint8_t depth) {
  xcb_format_t *fmt = xcb_setup_pixmap_formats(setup);
  xcb_format_t *fmtend = fmt + xcb_setup_pixmap_formats_length(setup);
  for (; fmt != fmtend; ++fmt)
    if (fmt->depth == depth)
      return fmt;
  return 0;
}

struct WindowX {

  inline WindowX(const std::string &name, int w, int h, int c)
      : name(name), windowWidth(w), windowHeight(h), channels(c) {}

  std::string name;
  xcb_connection_t *xcbConn = nullptr;
  xcb_window_t xcbWindow;
  xcb_image_t *xcbImg = nullptr;
  xcb_key_symbols_t *keySyms = nullptr;
  xcb_gcontext_t bgcolor;
  xcb_format_t *fmt;

  int depth;
  xcb_drawable_t rect;

  int windowWidth, windowHeight, channels;

  bool is_paused = false;
  bool is_quit = false;
  bool did_quit = false;

  inline void createWindow() {
    // Create window.
    // https://www.x.org/releases/X11R7.7/doc/libxcb/tutorial/index.html
    xcb_screen_t *screen;
    xcbConn = xcb_connect(NULL, NULL);
    screen = xcb_setup_roots_iterator(xcb_get_setup(xcbConn)).data;
    xcbWindow = xcb_generate_id(xcbConn);
    // See docs, this is very confusing.
    uint32_t mask = XCB_CW_EVENT_MASK;
    uint32_t values[1];
    values[0] =
        // XCB_EVENT_MASK_EXPOSURE       | XCB_EVENT_MASK_BUTTON_PRESS   |
        // XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
        // XCB_EVENT_MASK_ENTER_WINDOW   | XCB_EVENT_MASK_LEAVE_WINDOW   |
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

    xcb_create_window(xcbConn,                       /* Connection          */
                      XCB_COPY_FROM_PARENT,          /* depth (same as root)*/
                      xcbWindow,                     /* window Id           */
                      screen->root,                  /* parent window       */
                      0, 0,                          /* x, y                */
                      windowWidth, windowHeight,     /* width, height       */
                      10,                            /* border_width        */
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class               */
                      screen->root_visual,           /* visual              */
                      mask, values);                 /* masks, not used yet */
    xcb_map_window(xcbConn, xcbWindow);
    xcb_flush(xcbConn);

    keySyms = xcb_key_symbols_alloc(xcbConn);

    depth = xcb_aux_get_depth(xcbConn, screen);
    bgcolor = xcb_generate_id(xcbConn);
    mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    uint32_t valgc[3];
    valgc[0] = screen->white_pixel;
    valgc[1] = screen->black_pixel;
    // valgc[0] = 0xab'ab'3a'ef;
    valgc[2] = 0; /* no graphics exposures */
    xcb_create_gc(xcbConn, bgcolor, xcbWindow, mask, valgc);
    // fmt::print(" - depth {}\n", depth);

    // Allocate image
    rect = xcb_generate_id(xcbConn);
    xcb_create_pixmap(xcbConn, depth, rect, screen->root, windowWidth,
                      windowHeight);
    // xcbImg = xcb_image_get(xcbConn, rect, 0,0, windowWidth, windowHeight,
    // 0xff'ff'ff'ff, XCB_IMAGE_FORMAT_XY_PIXMAP); xcb_rectangle_t rect_coord =
    // { 0, 0, (uint16_t)windowWidth, (uint16_t)windowHeight};
    // xcb_poly_fill_rectangle(xcbConn, rect, bgcolor, 1, &rect_coord);

    xcb_flush(xcbConn);

    xcb_change_property(xcbConn, XCB_PROP_MODE_REPLACE, xcbWindow,
                        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, name.length(),
                        name.c_str());

    auto setup = xcb_get_setup(xcbConn);
    fmt = find_format_by_depth(setup, depth);
  }

  inline void update(const Image &img) {
    // xcbImg = xcb_image_get(xcbConn, rect, 0,0, windowWidth, windowHeight,
    // 0xff'ff'ff'ff, XCB_IMAGE_FORMAT_XY_PIXMAP);
    xcb_rectangle_t rect_coord = {0, 0, (uint16_t)windowWidth,
                                  (uint16_t)windowHeight};
    // xcb_poly_fill_rectangle(xcbConn, rect, bgcolor, 1, &rect_coord);

    int w = windowWidth;
    int h = windowHeight;
    int C = img.channels();

    int nr = h;
    // int nc = (w*fmt->bits_per_pixel+7)/8;
    int nc = w * 4;
    uint8_t *tmp = (uint8_t *)malloc(nr * nc);

    // clang-format off
		if (C == 1)
			for (int y=0;y<img.h;y++) for (int x=0;x<img.w;x++) {
				tmp[y*w*4 + x*4 + 0] = img.buffer[y*img.w*C+x*C+0];
				tmp[y*w*4 + x*4 + 1] = img.buffer[y*img.w*C+x*C+1];
				tmp[y*w*4 + x*4 + 2] = img.buffer[y*img.w*C+x*C+2];
				tmp[y*w*4 + x*4 + 3] = 255;
			}

		else if (C == 3)
			for (int y=0;y<img.h;y++) for (int x=0;x<img.w;x++) {
				for (int c=0;c<C;c++)
					tmp[y*w*4 + x*4 + c] = img.buffer[y*img.w*C+x*C+c];
				tmp[y*w*4 + x*4 + 3] = 255;
			}

		else if (C == 4)
			for (int y=0;y<img.h;y++) for (int x=0;x<img.w;x++) for (int c=0;c<C;c++)
					tmp[y*w*4 + x*4 + c] = img.buffer[y*img.w*C+x*C+c];

    // clang-format on
    xcb_pixmap_t pixmap = xcb_generate_id(xcbConn);
    xcb_gcontext_t gc = xcb_generate_id(xcbConn);
    xcb_create_pixmap(xcbConn, depth, pixmap, xcbWindow, w, h);
    xcb_create_gc(xcbConn, gc, pixmap, 0, NULL);
    auto image = xcb_image_create_native(
        xcbConn, w, h, XCB_IMAGE_FORMAT_Z_PIXMAP, depth, tmp, w * h * 4, tmp);
    xcb_image_put(xcbConn, pixmap, gc, image, 0, 0, 0);
    xcb_image_destroy(image);
    xcb_copy_area(xcbConn, pixmap, xcbWindow, gc, 0, 0, 0, 0, w, h);
    xcb_free_pixmap(xcbConn, pixmap);
    xcb_flush(xcbConn);
    // Looks like xcb_image_destroy actually frees.
    // free(tmp);
  }

  inline void updateEvents() {
    xcb_generic_event_t *e;

    int nProcessed = 0;

    // while ((e = xcb_wait_for_event (xcbConn))) {
    while ((e = xcb_poll_for_event(xcbConn))) {
      switch (e->response_type & ~0x80) {
      case XCB_KEY_PRESS:
        break;
      case XCB_KEY_RELEASE: {
        nProcessed++;
        xcb_key_press_event_t *ev = (xcb_key_press_event_t *)e;
        // print_modifiers(ev->state);

        uint32_t key0 = xcb_key_symbols_get_keysym(keySyms, ev->detail, 0);
        // uint8_t key = xk2vkk(key0);
        // uint8_t mod = xk2mod(ev->state);

        // bool isPressed = (e->response_type & ~0x80) == XCB_KEY_PRESS;
        // if (isPressed) printf(" - key0 (pressed ) %u %d at %u\n", key0, XK_q,
        // ev->time); else printf(" - key0 (released) %u %d at %u\n", key0,
        // XK_q, ev->time);

        if (key0 == XK_p)
          is_paused = not is_paused;
        if (key0 == XK_q)
          is_quit = true;

        break;
      }
      default:
        /* Unknown event type, ignore it */
        // printf("Unknown event: %d\n", e->response_type);
        break;
      }
    }
    free(e);
  }

  inline void destroyWindow() {
    did_quit = true;
    if (rect) {
      // xcb_image_free(xcbImg);
      xcb_free_pixmap(xcbConn, rect);
      xcbImg = nullptr;
    }
    if (keySyms) {
      xcb_key_symbols_free(keySyms);
      keySyms = nullptr;
    }
    if (xcbConn) {
      xcb_disconnect(xcbConn);
      xcbConn = nullptr;
    }
  }

  inline ~WindowX() { destroyWindow(); }
};

static std::unordered_map<std::string, std::unique_ptr<WindowX>> windows;

void imshow(const std::string &name, const Image &img) {

  auto kv = windows.find(name);
  if (windows.find(name) == windows.end()) {
    windows[name] =
        std::make_unique<WindowX>(name, img.w, img.h, img.channels());
    kv = windows.find(name);

    kv->second.get()->windowWidth = img.w;
    kv->second.get()->windowHeight = img.h;
    kv->second.get()->createWindow();
  }

  WindowX *win = kv->second.get();

  if (win->did_quit)
    return;

  win->updateEvents();

  if (win->is_quit and not win->did_quit) {
    win->destroyWindow();
    return;
  }

  if (win->is_paused)
    return;

  win->update(img);
}
