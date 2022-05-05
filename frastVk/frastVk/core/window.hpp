#pragma once

#include <unistd.h>
#include <cstdio>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include <fmt/core.h>

#define XK_LATIN1
#define XK_MISCELLANY
#include <X11/keysymdef.h>


namespace VKMOD {
	constexpr uint8_t SHIFT = 1;
	constexpr uint8_t ALT = 2;
	constexpr uint8_t CTRL = 4;
	constexpr uint8_t BUTTON1 = 8;
	constexpr uint8_t BUTTON2 = 16;
	constexpr uint8_t BUTTON3 = 32;
};
enum VKK : uint8_t {
	VKK_UNKNOWN = 0,
	VKK_MINUS,
	VKK_EQUALS,
	VKK_BACKSPACE,
	VKK_LBRACKET,
	VKK_RBRACKET,
	VKK_BACKSLASH,
	VKK_TAB,
	VKK_ESCAPE,
	VKK_SLASH,
	VKK_COMMA,
	VKK_DOT,
	VKK_QUOTE,
	VKK_SEMICOLON,

	VKK_UP, VKK_RIGHT, VKK_DOWN, VKK_LEFT,
	
	VKK_SPACE,

	VKK_0, VKK_1, VKK_2, VKK_3, VKK_4,
	VKK_5, VKK_6, VKK_7, VKK_8, VKK_9,
	
	VKK_A, VKK_B, VKK_C, VKK_D, VKK_E, VKK_F,
	VKK_G, VKK_H, VKK_I, VKK_J, VKK_K, VKK_L,
	VKK_M, VKK_N, VKK_O, VKK_P, VKK_Q, VKK_R,
	VKK_S, VKK_T, VKK_U, VKK_V, VKK_W, VKK_X,
	VKK_Y, VKK_Z,

	VKK_LSHIFT, VKK_RSHIFT, VKK_LCTRL, VKK_RCTRL,
	VKK_LALT, VKK_RALT
};
inline VKK ascii2vkk(uint8_t c) {
	if (c >= 'a' and c <= 'z')
		return (VKK) (c - 'a' + VKK_A);
	if (c >= '0' and c <= '9') return (VKK) (c - '0' + VKK_0);
	if (c == ' ') return VKK_SPACE;
	if (c == '-') return VKK_MINUS;
	if (c == '=') return VKK_EQUALS;
	if (c == '[') return VKK_LBRACKET;
	if (c == ']') return VKK_RBRACKET;
	if (c == '\\') return VKK_BACKSLASH;
	if (c == '/') return VKK_SLASH;
	if (c == '.') return VKK_COMMA;
	if (c == '.') return VKK_DOT;
	if (c == '\'') return VKK_QUOTE;
	if (c == ';') return VKK_SEMICOLON;
	return VKK_UNKNOWN;
}
inline constexpr int vkkLength() {
	return VKK_RALT - VKK_MINUS;
}

inline constexpr uint8_t xk2vkk(uint32_t key0) {
		uint8_t key = 0;
		switch (key0) {
			case XK_a: {key = VKK_A; break;}
			case XK_b: {key = VKK_B; break;}
			case XK_c: {key = VKK_C; break;}
			case XK_d: {key = VKK_D; break;}
			case XK_e: {key = VKK_E; break;}
			case XK_f: {key = VKK_F; break;}
			case XK_g: {key = VKK_G; break;}
			case XK_h: {key = VKK_H; break;}
			case XK_i: {key = VKK_I; break;}
			case XK_j: {key = VKK_J; break;}
			case XK_k: {key = VKK_K; break;}
			case XK_l: {key = VKK_L; break;}
			case XK_m: {key = VKK_M; break;}
			case XK_n: {key = VKK_N; break;}
			case XK_o: {key = VKK_O; break;}
			case XK_p: {key = VKK_P; break;}
			case XK_q: {key = VKK_Q; break;}
			case XK_r: {key = VKK_R; break;}
			case XK_s: {key = VKK_S; break;}
			case XK_t: {key = VKK_T; break;}
			case XK_u: {key = VKK_U; break;}
			case XK_v: {key = VKK_V; break;}
			case XK_w: {key = VKK_W; break;}
			case XK_x: {key = VKK_X; break;}
			case XK_y: {key = VKK_Y; break;}
			case XK_z: {key = VKK_Z; break;}
			case XK_0: {key = VKK_0; break;}
			case XK_1: {key = VKK_1; break;}
			case XK_2: {key = VKK_2; break;}
			case XK_3: {key = VKK_3; break;}
			case XK_4: {key = VKK_4; break;}
			case XK_5: {key = VKK_5; break;}
			case XK_6: {key = VKK_6; break;}
			case XK_7: {key = VKK_7; break;}
			case XK_8: {key = VKK_8; break;}
			case XK_9: {key = VKK_9; break;}
			case XK_space: {key = VKK_SPACE; break;}
			case XK_Shift_L: {key = VKK_LSHIFT; break;}
			case XK_Shift_R: {key = VKK_RSHIFT; break;}
			case XK_Control_L: {key = VKK_LCTRL; break;}
			case XK_Control_R: {key = VKK_RCTRL; break;}
			case XK_Alt_L: {key = VKK_LALT; break;}
			case XK_Alt_R: {key = VKK_RALT; break;}
			case XK_Escape: {key = VKK_ESCAPE; break;}
			case XK_BackSpace: {key = VKK_BACKSPACE; break;}
			case XK_minus: {key = VKK_MINUS; break;}
			case XK_equal: {key = VKK_EQUALS; break;}
			case XK_slash: {key = VKK_SLASH; break;}
			case XK_comma: {key = VKK_COMMA; break;}
			case XK_semicolon: {key = VKK_SEMICOLON; break;}
			case XK_period: {key = VKK_DOT; break;}
			case XK_quoteleft: {key = VKK_QUOTE; break;}
			case XK_Tab: {key = VKK_TAB; break;}
		}
		return key;
}
inline constexpr uint8_t xk2mod(uint32_t mask) {
	uint8_t out = 0;
	if (mask & 1) out |= VKMOD::SHIFT;
	if (mask & 2) out |= VKMOD::CTRL;

	if (mask & 8) out |= VKMOD::ALT;
	if (mask & 16) out |= VKMOD::BUTTON1;
	if (mask & 32) out |= VKMOD::BUTTON2;
	if (mask & 64) out |= VKMOD::BUTTON3;
	return out;
}

struct UsesIO {
	inline virtual void handleKey(uint8_t key, uint8_t mod, bool isDown) {}
	inline virtual void handleMousePress(uint8_t button, uint8_t mod, uint8_t x, uint8_t y, bool isPressing) {}
	inline virtual void handleMouseMotion(int x, int y, uint8_t mod) {}
	inline virtual void handleMouseNotify(int x, int y, bool isEntering) {}
};


/*
 *
 * X Window, using xcb.
 *
 * Has any number of IO Listeners, which can be added to @ioUsers.
 * ADDITIONALLY, the window itself is a listener (but it isn't added to the list, because 'this' is
 * not a shared_ptr).
 *
 *
 */
struct Window : public UsesIO {
	xcb_connection_t* xcbConn = nullptr;
	xcb_window_t xcbWindow;

	void setupWindow();
	bool pollWindowEvents();
	void destroyWindow();

	~Window();

	uint32_t windowWidth = 0, windowHeight = 0;

	/*inline virtual void handleKey(uint8_t key, uint8_t mod, bool isDown) {}
	inline virtual void handleMousePress(uint8_t button, uint8_t mod, uint8_t x, uint8_t y, bool isPressing) {}
	inline virtual void handleMouseMotion(int x, int y, uint8_t mod) {}
	inline virtual void handleMouseNotify(int x, int y, bool isEntering) {}*/

	std::vector<std::shared_ptr<UsesIO>> ioUsers;


	private:
	xcb_key_symbols_t* keySyms = nullptr;
};

inline Window::~Window() {
	destroyWindow();
}

inline void Window::destroyWindow() {
	if (keySyms) {
		xcb_key_symbols_free(keySyms);
		keySyms = nullptr;
	}
	if (xcbConn) {
		xcb_disconnect (xcbConn);
		xcbConn = nullptr;
	}
}

inline void print_modifiers (uint32_t mask)
{
	const char **mod, *mods[] = {
		"Shift", "Lock", "Ctrl", "Alt",
		"Mod2", "Mod3", "Mod4", "Mod5",
		"Button1", "Button2", "Button3", "Button4", "Button5"
	};
	printf ("Modifier mask: ");
	for (mod = mods ; mask; mask >>= 1, mod++)
		if (mask & 1)
			printf("%s", *mod);
	putchar ('\n');
}

inline void Window::setupWindow() {
	// Create window.
	// https://www.x.org/releases/X11R7.7/doc/libxcb/tutorial/index.html
	xcb_screen_t         *screen;
	xcbConn = xcb_connect(NULL, NULL);
	screen = xcb_setup_roots_iterator (xcb_get_setup (xcbConn)).data;
	xcbWindow = xcb_generate_id(xcbConn);
	// See docs, this is very confusing.
	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t values[1];
	values[0] = XCB_EVENT_MASK_EXPOSURE       | XCB_EVENT_MASK_BUTTON_PRESS   |
		XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW   | XCB_EVENT_MASK_LEAVE_WINDOW   |
		XCB_EVENT_MASK_KEY_PRESS      | XCB_EVENT_MASK_KEY_RELEASE;

	// windowWidth = 1280;
	// windowHeight = 960;

	xcb_create_window (xcbConn,                             /* Connection          */
			XCB_COPY_FROM_PARENT,          /* depth (same as root)*/
			xcbWindow,                           /* window Id           */
			screen->root,                  /* parent window       */
			0, 0,                          /* x, y                */
			windowWidth, windowHeight,                      /* width, height       */
			10,                            /* border_width        */
			XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class               */
			screen->root_visual,           /* visual              */
			mask, values);                      /* masks, not used yet */
	xcb_map_window (xcbConn, xcbWindow);
	xcb_flush (xcbConn);

	keySyms = xcb_key_symbols_alloc(xcbConn);
}

inline bool Window::pollWindowEvents() {
	xcb_generic_event_t *e;

	int nProcessed = 0;

		//xcb_generic_event_t* (*f)(xcb_connection_t*) =
		//block ? xcb_wait_for_event : xcb_poll_for_event;

		//while ((e = xcb_wait_for_event (xcbConn))) {
		while ((e = xcb_poll_for_event (xcbConn))) {
		//while ((e = f (xcbConn))) {


		switch (e->response_type & ~0x80) {
			case XCB_EXPOSE: {
								 break;
							 }

			case XCB_BUTTON_RELEASE:
			case XCB_BUTTON_PRESS: {
									nProcessed++;
									   xcb_button_press_event_t *ev = (xcb_button_press_event_t *)e;
									   print_modifiers(ev->state);

									bool isPress = (e->response_type & ~0x80) == XCB_BUTTON_PRESS;

									   switch (ev->detail) {
										   case 4:
											   //printf ("Wheel Button up in window %ld, at coordinates (%d,%d)\n", ev->event, ev->event_x, ev->event_y);
											   break;
										   case 5:
											   //printf ("Wheel Button down in window %ld, at coordinates (%d,%d)\n", ev->event, ev->event_x, ev->event_y);
											   break;
										   //default: printf ("Button %d pressed in window %ld, at coordinates (%d,%d)\n", ev->detail, ev->event, ev->event_x, ev->event_y);
									   }

									uint8_t mod = xk2mod(ev->state);
									(this)->handleMousePress(ev->detail, mod, ev->event_x, ev->event_y, isPress);
									for (auto &user : ioUsers) user->handleMousePress(ev->detail, mod, ev->event_x, ev->event_y, isPress);

									   break;
								   }

			case XCB_MOTION_NOTIFY: {
									nProcessed++;
										xcb_motion_notify_event_t *ev = (xcb_motion_notify_event_t *)e;

										uint8_t mod = xk2mod(ev->state);
										(this)->handleMouseMotion(ev->event_x, ev->event_y, mod);
										for (auto &user : ioUsers) user->handleMouseMotion(ev->event_x, ev->event_y, mod);
										//printf ("Mouse moved in window %ld, at coordinates (%d,%d)\n", ev->event, ev->event_x, ev->event_y);
										break;
									}

			case XCB_LEAVE_NOTIFY:
			case XCB_ENTER_NOTIFY: {
									   xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t *)e;

									bool isEnter = (e->response_type & ~0x80) == XCB_ENTER_NOTIFY;

									//if (isEnter) printf ("Mouse entered window %ld, at coordinates (%d,%d)\n", ev->event, ev->event_x, ev->event_y);
									//else printf ("Mouse left    window %ld, at coordinates (%d,%d)\n", ev->event, ev->event_x, ev->event_y);

									(this)->handleMouseNotify(ev->event_x, ev->event_y, isEnter);
									for (auto &user : ioUsers) user->handleMouseNotify(ev->event_x, ev->event_y, isEnter);


									   break;
								   }

			case XCB_KEY_RELEASE:
			case XCB_KEY_PRESS: {
									nProcessed++;
									xcb_key_press_event_t *ev = (xcb_key_press_event_t *)e;
									//print_modifiers(ev->state);

									uint32_t key0 = xcb_key_symbols_get_keysym(keySyms, ev->detail, 0);
									uint8_t key = xk2vkk(key0);
									uint8_t mod = xk2mod(ev->state);

									bool isPressed = (e->response_type & ~0x80) == XCB_KEY_PRESS;

									//if (isPressed) printf(" - key0 (pressed ) %u %d at %u\n", key0, XK_q, ev->time);
									//else printf(" - key0 (released) %u %d at %u\n", key0, XK_q, ev->time);
									(this)->handleKey(key, mod, isPressed);
									for (auto &user : ioUsers) user->handleKey(key, mod, isPressed);

									break;
								}
			default:
								  /* Unknown event type, ignore it */
								  printf("Unknown event: %d\n", e->response_type);
								  break;
		}
		/* Free the Generic Event */
		free (e);
	}

	return nProcessed > 0;
	}

