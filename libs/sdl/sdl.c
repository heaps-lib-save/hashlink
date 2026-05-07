#define HL_NAME(n) sdl_##n

#include <hl.h>
#include "hlsystem.h"

#include <locale.h>
#include <SDL3/SDL.h>

#if defined (HL_IOS) || defined(HL_TVOS)
#	include <OpenGLES/ES3/gl.h>
#	include <OpenGLES/ES3/glext.h>
#endif

#ifndef SDL_MAJOR_VERSION
#	error "SDL3 SDK not found"
#endif

#define TWIN _ABSTRACT(sdl_window)
#define TGL _ABSTRACT(sdl_gl)
#define _SURF _ABSTRACT(sdl_surface)

typedef struct {
	int x;
	int y;
	int w;
	int h;
	int style;
} wsave_pos;

typedef enum {
	Quit,
	MouseMove,
	MouseLeave,
	MouseDown,
	MouseUp,
	MouseWheel,
	WindowState,
	KeyDown,
	KeyUp,
	TextInput,
	GControllerAdded = 100,
	GControllerRemoved,
	GControllerDown,
	GControllerUp,
	GControllerAxis,
	TouchDown = 200,
	TouchUp,
	TouchMove,
	JoystickAxisMotion = 300,
	JoystickBallMotion,
	JoystickHatMotion,
	JoystickButtonDown,
	JoystickButtonUp,
	JoystickAdded,
	JoystickRemoved,
	DropStart = 400,
	DropFile,
	DropText,
	DropEnd,
	KeyMapChanged = 500,
} event_type;

typedef enum {
	Show,
	Hide,
	Expose,
	Move,
	Resize,
	Minimize,
	Maximize,
	Restore,
	Enter,
	Leave,
	Focus,
	Blur,
	Close
} ws_change;

typedef struct {
	hl_type *t;
	event_type type;
	int mouseX;
	int mouseY;
	int mouseXRel;
	int mouseYRel;
	int button;
	int wheelDelta;
	ws_change state;
	int keyCode;
	int scanCode;
	bool keyRepeat;
	int reference;
	int value;
	int __unused;
	int window;
	vbyte* dropFile;
} event_data;

static bool isGlOptionsSet = false;

HL_PRIM bool HL_NAME(init_once)() {
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if( !SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC) ) {
		hl_error("SDL_Init failed: %s", hl_to_utf16(SDL_GetError()));
		return false;
	}
	setlocale(LC_ALL, "C");
#	ifdef _WIN32
	// Set the internal windows timer period to 1ms (will give accurate sleep for vsync)
	timeBeginPeriod(1);
#	endif
	// default GL parameters — request latest, SDL gives nearest match
	if (!isGlOptionsSet) {
#ifdef HL_MOBILE
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
#endif
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	}

	return true;
}

HL_PRIM void HL_NAME(gl_options)( int major, int minor, int depth, int stencil, int flags, int samples ) {
	isGlOptionsSet = true;
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depth);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, stencil);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, (flags&1));
	if( flags&2 )
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	else if( flags&4 )
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	else if( flags&8 )
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	else {
#ifdef HL_MOBILE
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
	}

	if (samples > 1) {
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
	}
}

HL_PRIM bool HL_NAME(hint_value)( vbyte* name, vbyte* value) {
	return SDL_SetHint((char*)name, (char*)value);
}

HL_PRIM int HL_NAME(event_poll)( SDL_Event *e ) {
	return SDL_PollEvent(e);
}

HL_PRIM bool HL_NAME(event_loop)( event_data *event ) {
	while (true) {
		SDL_Event e;
		if (!SDL_PollEvent(&e)) break;
		switch (e.type) {
		case SDL_EVENT_QUIT:
			event->type = Quit;
			break;
		case SDL_EVENT_MOUSE_MOTION:
			event->type = MouseMove;
			event->window = (int)e.motion.windowID;
			event->mouseX = (int)e.motion.x;
			event->mouseY = (int)e.motion.y;
			event->mouseXRel = (int)e.motion.xrel;
			event->mouseYRel = (int)e.motion.yrel;
			break;
		case SDL_EVENT_KEY_DOWN:
			event->type = KeyDown;
			event->window = (int)e.key.windowID;
			event->keyCode = (int)e.key.key;
			event->scanCode = (int)e.key.scancode;
			event->keyRepeat = e.key.repeat;
			break;
		case SDL_EVENT_KEY_UP:
			event->type = KeyUp;
			event->window = (int)e.key.windowID;
			event->keyCode = (int)e.key.key;
			event->scanCode = (int)e.key.scancode;
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			event->type = MouseDown;
			event->window = (int)e.button.windowID;
			event->button = e.button.button;
			event->mouseX = (int)e.button.x;
			event->mouseY = (int)e.button.y;
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			event->type = MouseUp;
			event->window = (int)e.button.windowID;
			event->button = e.button.button;
			event->mouseX = (int)e.button.x;
			event->mouseY = (int)e.button.y;
			break;
		case SDL_EVENT_FINGER_DOWN:
			event->type = TouchDown;
			event->mouseX = (int)(e.tfinger.x*10000);
			event->mouseY = (int)(e.tfinger.y*10000);
			event->reference = (int)e.tfinger.fingerID;
			break;
		case SDL_EVENT_FINGER_MOTION:
			event->type = TouchMove;
			event->mouseX = (int)(e.tfinger.x*10000);
			event->mouseY = (int)(e.tfinger.y*10000);
			event->reference = (int)e.tfinger.fingerID;
			break;
		case SDL_EVENT_FINGER_UP:
			event->type = TouchUp;
			event->mouseX = (int)(e.tfinger.x*10000);
			event->mouseY = (int)(e.tfinger.y*10000);
			event->reference = (int)e.tfinger.fingerID;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			event->type = MouseWheel;
			event->window = (int)e.wheel.windowID;
			event->wheelDelta = (int)e.wheel.y;
			if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) event->wheelDelta *= -1;
			event->mouseX = (int)e.wheel.x;
			event->mouseY = (int)e.wheel.y;
			break;
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_MOVED:
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_MINIMIZED:
		case SDL_EVENT_WINDOW_MAXIMIZED:
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_MOUSE_ENTER:
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
		case SDL_EVENT_WINDOW_FOCUS_LOST:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		case SDL_EVENT_WINDOW_OCCLUDED:
		case SDL_EVENT_WINDOW_DESTROYED:
			event->type = WindowState;
			event->window = (int)e.window.windowID;
			switch (e.type) {
			case SDL_EVENT_WINDOW_SHOWN:
				event->state = Show;
				break;
			case SDL_EVENT_WINDOW_HIDDEN:
				event->state = Hide;
				break;
			case SDL_EVENT_WINDOW_EXPOSED:
				event->state = Expose;
				break;
			case SDL_EVENT_WINDOW_MOVED:
				event->state = Move;
				break;
			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				event->state = Resize;
				break;
			case SDL_EVENT_WINDOW_MINIMIZED:
				event->state = Minimize;
				break;
			case SDL_EVENT_WINDOW_MAXIMIZED:
				event->state = Maximize;
				break;
			case SDL_EVENT_WINDOW_RESTORED:
				event->state = Restore;
				break;
			case SDL_EVENT_WINDOW_MOUSE_ENTER:
				event->state = Enter;
				break;
			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
				event->state = Leave;
				break;
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				event->state = Focus;
				break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				event->state = Blur;
				break;
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				event->state = Close;
				break;
			default:
				continue;
			}
			break;
		case SDL_EVENT_TEXT_EDITING:
			continue;
		case SDL_EVENT_TEXT_INPUT:
			event->type = TextInput;
			event->window = (int)e.text.windowID;
			event->keyCode = *(int*)e.text.text;
			event->keyCode &= e.text.text[0] ? e.text.text[1] ? e.text.text[2] ? e.text.text[3] ? 0xFFFFFFFF : 0xFFFFFF : 0xFFFF : 0xFF : 0;
			break;
		case SDL_EVENT_GAMEPAD_ADDED:
			event->type = GControllerAdded;
			event->reference = (int)e.gdevice.which;
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			event->type = GControllerRemoved;
			event->reference = (int)e.gdevice.which;
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			event->type = GControllerDown;
			event->reference = (int)e.gbutton.which;
			event->button = e.gbutton.button;
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			event->type = GControllerUp;
			event->reference = (int)e.gbutton.which;
			event->button = e.gbutton.button;
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			event->type = GControllerAxis;
			event->reference = (int)e.gaxis.which;
			event->button = e.gaxis.axis;
			event->value = e.gaxis.value;
			break;
		case SDL_EVENT_JOYSTICK_AXIS_MOTION:
			event->type = JoystickAxisMotion;
			event->reference = (int)e.jaxis.which;
			event->button = e.jaxis.axis;
			event->value = e.jaxis.value;
			break;
		case SDL_EVENT_JOYSTICK_BALL_MOTION:
			event->type = JoystickBallMotion;
			event->reference = (int)e.jball.which;
			event->button = e.jball.ball;
			event->mouseXRel = (int)e.jball.xrel;
			event->mouseYRel = (int)e.jball.yrel;
			break;
		case SDL_EVENT_JOYSTICK_HAT_MOTION:
			event->type = JoystickHatMotion;
			event->reference = (int)e.jhat.which;
			event->button = e.jhat.hat;
			event->value = e.jhat.value;
			break;
		case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
			event->type = JoystickButtonDown;
			event->reference = (int)e.jbutton.which;
			event->button = e.jbutton.button;
			break;
		case SDL_EVENT_JOYSTICK_BUTTON_UP:
			event->type = JoystickButtonUp;
			event->reference = (int)e.jbutton.which;
			event->button = e.jbutton.button;
			break;
		case SDL_EVENT_JOYSTICK_ADDED:
			event->type = JoystickAdded;
			event->reference = (int)e.jdevice.which;
			break;
		case SDL_EVENT_JOYSTICK_REMOVED:
			event->type = JoystickRemoved;
			event->reference = (int)e.jdevice.which;
			break;
		case SDL_EVENT_DROP_BEGIN:
			event->type = DropStart;
			event->window = (int)e.drop.windowID;
			break;
		case SDL_EVENT_DROP_FILE:
		case SDL_EVENT_DROP_TEXT: {
			vbyte* bytes = hl_copy_bytes((const vbyte*)e.drop.data, (int)SDL_strlen(e.drop.data) + 1);
			SDL_free((void*)e.drop.data);
			event->type = e.type == SDL_EVENT_DROP_FILE ? DropFile : DropText;
			event->dropFile = bytes;
			event->window = (int)e.drop.windowID;
			break;
		}
		case SDL_EVENT_DROP_COMPLETE:
			event->type = DropEnd;
			event->window = (int)e.drop.windowID;
			break;
		case SDL_EVENT_KEYMAP_CHANGED:
			event->type = KeyMapChanged;
			break;
		default:
			continue;
		}
		return true;
	}
	return false;
}

HL_PRIM void HL_NAME(quit)() {
	SDL_Quit();
#	ifdef _WIN32
	timeEndPeriod(1);
#	endif
}

HL_PRIM void HL_NAME(delay)( int time ) {
	hl_blocking(true);
	SDL_Delay(time);
	hl_blocking(false);
}

HL_PRIM int HL_NAME(get_screen_width)() {
	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(0);
	if (mode)
		return mode->w;
	return 0;
}

HL_PRIM int HL_NAME(get_screen_height)() {
	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(0);
	if (mode)
		return mode->h;
	return 0;
}

HL_PRIM int HL_NAME(get_screen_width_of_window)(SDL_Window* win) {
	SDL_DisplayID id = win ? SDL_GetDisplayForWindow(win) : 0;
	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(id);
	if (mode)
		return mode->w;
	return 0;
}

HL_PRIM int HL_NAME(get_screen_height_of_window)(SDL_Window* win) {
	SDL_DisplayID id = win ? SDL_GetDisplayForWindow(win) : 0;
	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(id);
	if (mode)
		return mode->h;
	return 0;
}

HL_PRIM int HL_NAME(get_framerate)(SDL_Window* win) {
	SDL_DisplayID id = win ? SDL_GetDisplayForWindow(win) : 0;
	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(id);
	if (mode)
		return (int)mode->refresh_rate;
	return 0;
}

HL_PRIM void HL_NAME(message_box)(vbyte *title, vbyte *text, bool error) {
	hl_blocking(true);
	SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : 0, (char*)title, (char*)text, NULL);
	hl_blocking(false);
}

HL_PRIM void HL_NAME(set_vsync)(bool v) {
	SDL_GL_SetSwapInterval(v ? 1 : 0);
}

HL_PRIM bool HL_NAME(detect_win32)() {
#	ifdef _WIN32
	return true;
#	else
	return false;
#	endif
}

HL_PRIM void HL_NAME(text_input)( SDL_Window *window, bool enable ) {
	if( enable )
		SDL_StartTextInput(window);
	else
		SDL_StopTextInput(window);
}

HL_PRIM bool HL_NAME(set_relative_mouse_mode)(bool enable) {
	return SDL_SetWindowRelativeMouseMode(SDL_GetMouseFocus(), enable);
}

HL_PRIM bool HL_NAME(get_relative_mouse_mode)() {
	SDL_Window *win = SDL_GetMouseFocus();
	if (win) return SDL_GetWindowRelativeMouseMode(win);
	return false;
}

HL_PRIM int HL_NAME(capture_mouse)(bool enable) {
	return SDL_CaptureMouse(enable) ? 0 : -1;
}

HL_PRIM int HL_NAME(warp_mouse_global)(int x, int y) {
	return SDL_WarpMouseGlobal((float)x, (float)y) ? 0 : -1;
}

HL_PRIM void HL_NAME(warp_mouse_in_window)(SDL_Window* window, int x, int y) {
	SDL_WarpMouseInWindow(window, (float)x, (float)y);
}

HL_PRIM void HL_NAME(set_window_grab)(SDL_Window* window, bool grabbed) {
	SDL_SetWindowMouseGrab(window, grabbed);
}

HL_PRIM bool HL_NAME(get_window_grab)(SDL_Window* window) {
	return SDL_GetWindowMouseGrab(window);
}

HL_PRIM int HL_NAME(get_global_mouse_state)(int* x, int* y) {
	float fx, fy;
	SDL_MouseButtonFlags flags = SDL_GetGlobalMouseState(&fx, &fy);
	*x = (int)fx;
	*y = (int)fy;
	return (int)flags;
}

HL_PRIM const char *HL_NAME(detect_keyboard_layout)() {
	char q = (char)SDL_GetKeyFromScancode(SDL_SCANCODE_Q, 0, false);
	char w = (char)SDL_GetKeyFromScancode(SDL_SCANCODE_W, 0, false);
	char y = (char)SDL_GetKeyFromScancode(SDL_SCANCODE_Y, 0, false);

	if (q == 'q' && w == 'w' && y == 'y') return "qwerty";
	if (q == 'a' && w == 'z' && y == 'y') return "azerty";
	if (q == 'q' && w == 'w' && y == 'z') return "qwertz";
	if (q == 'q' && w == 'z' && y == 'y') return "qzerty";
	return "unknown";
}

#define TWIN _ABSTRACT(sdl_window)
DEFINE_PRIM(_BOOL, init_once, _NO_ARG);
DEFINE_PRIM(_VOID, gl_options, _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_BOOL, event_loop, _DYN );
DEFINE_PRIM(_I32, event_poll, _STRUCT );
DEFINE_PRIM(_VOID, quit, _NO_ARG);
DEFINE_PRIM(_VOID, delay, _I32);
DEFINE_PRIM(_I32, get_screen_width, _NO_ARG);
DEFINE_PRIM(_I32, get_screen_height, _NO_ARG);
DEFINE_PRIM(_I32, get_screen_width_of_window, TWIN);
DEFINE_PRIM(_I32, get_screen_height_of_window, TWIN);
DEFINE_PRIM(_I32, get_framerate, TWIN);
DEFINE_PRIM(_VOID, message_box, _BYTES _BYTES _BOOL);
DEFINE_PRIM(_VOID, set_vsync, _BOOL);
DEFINE_PRIM(_BOOL, detect_win32, _NO_ARG);
DEFINE_PRIM(_VOID, text_input, TWIN _BOOL);
DEFINE_PRIM(_BOOL, set_relative_mouse_mode, _BOOL);
DEFINE_PRIM(_BOOL, get_relative_mouse_mode, _NO_ARG);
DEFINE_PRIM(_I32, capture_mouse, _BOOL);
DEFINE_PRIM(_I32, warp_mouse_global, _I32 _I32);
DEFINE_PRIM(_VOID, warp_mouse_in_window, TWIN _I32 _I32);
DEFINE_PRIM(_VOID, set_window_grab, TWIN _BOOL);
DEFINE_PRIM(_BOOL, get_window_grab, TWIN);
DEFINE_PRIM(_I32, get_global_mouse_state, _REF(_I32) _REF(_I32));
DEFINE_PRIM(_BYTES, detect_keyboard_layout, _NO_ARG);
DEFINE_PRIM(_BOOL, hint_value, _BYTES _BYTES);

// Window

HL_PRIM SDL_Window *HL_NAME(win_create_ex)(int x, int y, int width, int height, int sdlFlags) {
	// force window to match device resolution on mobile
	if ((sdlFlags & (
#ifdef HL_MAC
		SDL_WINDOW_METAL |
#endif
		SDL_WINDOW_VULKAN )) == 0) {
		sdlFlags |= SDL_WINDOW_OPENGL;
	}

#ifdef	HL_MOBILE
	const SDL_DisplayMode *displayMode = SDL_GetDesktopDisplayMode(0);
	SDL_Window* win = SDL_CreateWindow("", width, height, SDL_WINDOW_BORDERLESS | sdlFlags);
#else
	SDL_Window* win = SDL_CreateWindow("", width, height, sdlFlags);
#endif
	if (win) {
		SDL_SetWindowPosition(win, x, y);
	}
#	ifdef HL_WIN
	// force window to show even if the debugger force process windows to be hidden
	if( win && (SDL_GetWindowFlags(win) & SDL_WINDOW_MOUSE_CAPTURE) == 0 ) {
		SDL_HideWindow(win);
		SDL_ShowWindow(win);
	}
	if (win) SDL_RaiseWindow(win); // better first focus lost behavior
#	endif
	return win;
}

HL_PRIM SDL_Window *HL_NAME(win_create)(int width, int height) {
	return HL_NAME(win_create_ex)(SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0);
}

HL_PRIM SDL_GLContext HL_NAME(win_get_glcontext)(SDL_Window *win) {
	SDL_GLContext ctx = SDL_GL_CreateContext(win);
	
#ifndef HL_MOBILE
	if (!ctx) {
		int desktop_fallback[][2] = {
			{4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0},
			{3, 3}, {3, 2}
		};
		for (int i = 0; i < sizeof(desktop_fallback)/sizeof(desktop_fallback[0]); i++) {
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, desktop_fallback[i][0]);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, desktop_fallback[i][1]);
			ctx = SDL_GL_CreateContext(win);
			if (ctx) break;
		}
	}
	if (!ctx) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
		ctx = SDL_GL_CreateContext(win);
	}
#else
	if (!ctx) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		ctx = SDL_GL_CreateContext(win);
	}
	if (!ctx) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		ctx = SDL_GL_CreateContext(win);
	}
#endif
	
	return ctx;
}

HL_PRIM bool HL_NAME(win_set_fullscreen)(SDL_Window *win, int mode) {
#	ifdef HL_WIN
	SDL_PropertiesID props = SDL_GetWindowProperties(win);
	wsave_pos *save = (wsave_pos*)SDL_GetPointerProperty(props, "save", NULL);
	HWND wnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
	if( save && mode != 2 ) {
		// exit borderless
		SetWindowLong(wnd,GWL_STYLE,save->style);
		SetWindowPos(wnd,NULL,save->x,save->y,save->w,save->h,0);
		SDL_SetWindowSize(win, save->w, save->h);
		free(save);
		SDL_SetPointerProperty(props, "save", NULL);
		save = NULL;
	}
#	endif
	switch( mode ) {
	case 0: // WINDOWED
		return SDL_SetWindowFullscreen(win, false);
	case 1: // FULLSCREEN
		return SDL_SetWindowFullscreen(win, true);
	case 2: // BORDERLESS
#		ifdef _WIN32
		{
			HMONITOR hmon = MonitorFromWindow(wnd,MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			RECT r;
			if( !GetMonitorInfo(hmon, &mi) )
				return false;
			GetWindowRect(wnd,&r);
			save = (wsave_pos*)malloc(sizeof(wsave_pos));
			save->x = r.left;
			save->y = r.top;
			save->w = r.right - r.left;
			save->h = r.bottom - r.top;
			save->style = GetWindowLong(wnd,GWL_STYLE);
			SDL_SetPointerProperty(props, "save", save);
			SetWindowLong(wnd,GWL_STYLE, WS_POPUP | WS_VISIBLE);
			SetWindowPos(wnd,NULL,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right - mi.rcMonitor.left,mi.rcMonitor.bottom - mi.rcMonitor.top + 2 /* prevent opengl driver to use exclusive mode !*/,0);
			return true;
		}
#	else
		return SDL_SetWindowFullscreen(win, true);
#	endif
	}
	return false;
}

HL_PRIM bool HL_NAME(win_set_display_mode)(SDL_Window *win, int width, int height, int framerate) {
	SDL_DisplayID display_id = SDL_GetDisplayForWindow(win);
	int num_modes = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display_id, &num_modes);
	for (int i = 0; i < num_modes; i++) {
		const SDL_DisplayMode *mode = modes[i];
		if (mode && mode->w == width && mode->h == height && (int)mode->refresh_rate == framerate) {
			SDL_free(modes);
			return SDL_SetWindowFullscreenMode(win, mode);
		}
	}
	SDL_free(modes);
	return false;
 }

HL_PRIM int HL_NAME(win_display_handle)(SDL_Window *win) {
	return (int)SDL_GetDisplayForWindow(win);
}

HL_PRIM void HL_NAME(win_set_title)(SDL_Window *win, vbyte *title) {
	SDL_SetWindowTitle(win, (char*)title);
}

HL_PRIM void HL_NAME(win_set_icon)(SDL_Window *win, SDL_Surface *s) {
	SDL_SetWindowIcon(win, s);
}

HL_PRIM void HL_NAME(win_set_position)(SDL_Window *win, int x, int y) {
	SDL_SetWindowPosition(win, x, y);
}

HL_PRIM void HL_NAME(win_get_position)(SDL_Window *win, int *x, int *y) {
	SDL_GetWindowPosition(win, x, y);
}

HL_PRIM void HL_NAME(win_set_size)(SDL_Window *win, int width, int height) {
	SDL_SetWindowSize(win, width, height);
}

HL_PRIM void HL_NAME(win_set_min_size)(SDL_Window *win, int width, int height) {
	SDL_SetWindowMinimumSize(win, width, height);
}

HL_PRIM void HL_NAME(win_set_max_size)(SDL_Window *win, int width, int height) {
	SDL_SetWindowMaximumSize(win, width, height);
}

HL_PRIM void HL_NAME(win_get_size)(SDL_Window *win, int *width, int *height) {
	SDL_GetWindowSize(win, width, height);
}

HL_PRIM void HL_NAME(win_get_min_size)(SDL_Window *win, int *width, int *height) {
	SDL_GetWindowMinimumSize(win, width, height);
}

HL_PRIM void HL_NAME(win_get_max_size)(SDL_Window *win, int *width, int *height) {
	SDL_GetWindowMaximumSize(win, width, height);
}

HL_PRIM double HL_NAME(win_get_opacity)(SDL_Window *win) {
	return (double)SDL_GetWindowOpacity(win);
}

HL_PRIM bool HL_NAME(win_set_opacity)(SDL_Window *win, float opacity) {
	return SDL_SetWindowOpacity(win, opacity);
}

HL_PRIM void HL_NAME(win_resize)(SDL_Window *win, int mode) {
	switch( mode ) {
	case 0:
		SDL_MaximizeWindow(win);
		break;
	case 1:
		SDL_MinimizeWindow(win);
		break;
	case 2:
		SDL_RestoreWindow(win);
		break;
	case 3:
		SDL_ShowWindow(win);
		break;
	case 4:
		SDL_HideWindow(win);
		break;
	default:
		break;
	}
}

HL_PRIM void HL_NAME(win_raise)(SDL_Window *win) {
	SDL_RaiseWindow(win);
}

HL_PRIM void HL_NAME(win_swap_window)(SDL_Window *win) {
#if defined(HL_IOS) || defined(HL_TVOS)
	SDL_PropertiesID props = SDL_GetWindowProperties(win);
	GLuint framebuffer = (GLuint)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0);
	GLuint colorbuffer = (GLuint)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_UIKIT_OPENGL_RENDERBUFFER_NUMBER, 0);
	if (framebuffer) glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	if (colorbuffer) glBindRenderbuffer(GL_RENDERBUFFER, colorbuffer);
#endif
	SDL_GL_SwapWindow(win);
}

HL_PRIM void HL_NAME(win_render_to)(SDL_Window *win, SDL_GLContext gl) {
	SDL_GL_MakeCurrent(win, gl);
}

HL_PRIM int HL_NAME(win_get_id)(SDL_Window *window) {
	return (int)SDL_GetWindowID(window);
}

HL_PRIM void HL_NAME(win_destroy)(SDL_Window *win, SDL_GLContext gl) {
	SDL_DestroyWindow(win);
	SDL_GL_DestroyContext(gl);
}

#define TGL _ABSTRACT(sdl_gl)
DEFINE_PRIM(TWIN, win_create_ex, _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(TWIN, win_create, _I32 _I32);
DEFINE_PRIM(TGL, win_get_glcontext, TWIN);
DEFINE_PRIM(_BOOL, win_set_fullscreen, TWIN _I32);
DEFINE_PRIM(_BOOL, win_set_display_mode, TWIN _I32 _I32 _I32);
DEFINE_PRIM(_I32, win_display_handle, TWIN);
DEFINE_PRIM(_VOID, win_resize, TWIN _I32);
DEFINE_PRIM(_VOID, win_raise, TWIN);
DEFINE_PRIM(_VOID, win_set_title, TWIN _BYTES);
DEFINE_PRIM(_VOID, win_set_icon, TWIN _SURF);
DEFINE_PRIM(_VOID, win_set_position, TWIN _I32 _I32);
DEFINE_PRIM(_VOID, win_get_position, TWIN _REF(_I32) _REF(_I32));
DEFINE_PRIM(_VOID, win_set_size, TWIN _I32 _I32);
DEFINE_PRIM(_VOID, win_set_min_size, TWIN _I32 _I32);
DEFINE_PRIM(_VOID, win_set_max_size, TWIN _I32 _I32);
DEFINE_PRIM(_VOID, win_get_size, TWIN _REF(_I32) _REF(_I32));
DEFINE_PRIM(_VOID, win_get_min_size, TWIN _REF(_I32) _REF(_I32));
DEFINE_PRIM(_VOID, win_get_max_size, TWIN _REF(_I32) _REF(_I32));
DEFINE_PRIM(_F64, win_get_opacity, TWIN);
DEFINE_PRIM(_BOOL, win_set_opacity, TWIN _F32);
DEFINE_PRIM(_VOID, win_swap_window, TWIN);
DEFINE_PRIM(_VOID, win_render_to, TWIN TGL);
DEFINE_PRIM(_VOID, win_destroy, TWIN TGL);
DEFINE_PRIM(_I32, win_get_id, TWIN);

// game controller (SDL3 uses SDL_Gamepad)

HL_PRIM int HL_NAME(gctrl_count)() {
	int count = 0;
	SDL_JoystickID *joysticks = SDL_GetJoysticks(&count);
	SDL_free(joysticks);
	return count;
}

HL_PRIM SDL_Gamepad *HL_NAME(gctrl_open)(int idx) {
	if (SDL_IsGamepad(idx))
		return SDL_OpenGamepad(idx);
	return NULL;
}

HL_PRIM void HL_NAME(gctrl_close)(SDL_Gamepad *ctrl) {
	SDL_CloseGamepad(ctrl);
}

HL_PRIM SDL_Gamepad *HL_NAME(gctrl_from_id)(int id) {
	return SDL_GetGamepadFromID((SDL_JoystickID)id);
}

HL_PRIM int HL_NAME(gctrl_get_id)(SDL_Gamepad *ctrl) {
	SDL_Joystick *joy = SDL_GetGamepadJoystick(ctrl);
	return (int)SDL_GetJoystickID(joy);
}

HL_PRIM int HL_NAME(gctrl_get_axis)(SDL_Gamepad *ctrl, int axis) {
	return SDL_GetGamepadAxis(ctrl, (SDL_GamepadAxis)axis);
}

HL_PRIM bool HL_NAME(gctrl_get_button)(SDL_Gamepad *ctrl, int button) {
	return SDL_GetGamepadButton(ctrl, (SDL_GamepadButton)button);
}

HL_PRIM vbyte *HL_NAME(gctrl_name)(SDL_Gamepad *ctrl) {
	const char *name = SDL_GetGamepadName(ctrl);
	if (name) return hl_copy_bytes((const vbyte*)name, (int)SDL_strlen(name) + 1);
	return NULL;
}

HL_PRIM int HL_NAME(gctrl_player_index)(SDL_Gamepad *ctrl) {
	return SDL_GetGamepadPlayerIndex(ctrl);
}

HL_PRIM int HL_NAME(gctrl_product)(SDL_Gamepad *ctrl) {
	return (int)SDL_GetGamepadProduct(ctrl);
}

HL_PRIM int HL_NAME(gctrl_vendor)(SDL_Gamepad *ctrl) {
	return (int)SDL_GetGamepadVendor(ctrl);
}

HL_PRIM int HL_NAME(gctrl_product_version)(SDL_Gamepad *ctrl) {
	return (int)SDL_GetGamepadProductVersion(ctrl);
}

HL_PRIM bool HL_NAME(gctrl_rumble)(SDL_Gamepad *ctrl, int low, int high, int duration) {
	return SDL_RumbleGamepad(ctrl, (Uint16)low, (Uint16)high, (Uint32)duration);
}

// joystick

HL_PRIM SDL_Joystick *HL_NAME(joy_open)(int idx) {
	return SDL_OpenJoystick(idx);
}

HL_PRIM void HL_NAME(joy_close)(SDL_Joystick *joy) {
	SDL_CloseJoystick(joy);
}

HL_PRIM int HL_NAME(joy_get_id)(SDL_Joystick *joy) {
	return (int)SDL_GetJoystickID(joy);
}

HL_PRIM vbyte *HL_NAME(joy_name)(SDL_Joystick *joy) {
	const char *name = SDL_GetJoystickName(joy);
	if (name) return hl_copy_bytes((const vbyte*)name, (int)SDL_strlen(name) + 1);
	return NULL;
}

HL_PRIM int HL_NAME(joy_num_axes)(SDL_Joystick *joy) {
	return SDL_GetNumJoystickAxes(joy);
}

HL_PRIM int HL_NAME(joy_num_buttons)(SDL_Joystick *joy) {
	return SDL_GetNumJoystickButtons(joy);
}

HL_PRIM int HL_NAME(joy_num_hats)(SDL_Joystick *joy) {
	return SDL_GetNumJoystickHats(joy);
}

HL_PRIM int HL_NAME(joy_num_balls)(SDL_Joystick *joy) {
	return SDL_GetNumJoystickBalls(joy);
}

HL_PRIM int HL_NAME(joy_get_axis)(SDL_Joystick *joy, int axis) {
	return SDL_GetJoystickAxis(joy, axis);
}

HL_PRIM int HL_NAME(joy_get_hat)(SDL_Joystick *joy, int hat) {
	return SDL_GetJoystickHat(joy, hat);
}

HL_PRIM bool HL_NAME(joy_get_button)(SDL_Joystick *joy, int button) {
	return SDL_GetJoystickButton(joy, button);
}

HL_PRIM int HL_NAME(joy_get_ball)(SDL_Joystick *joy, int ball, int *dx, int *dy) {
	return SDL_GetJoystickBall(joy, ball, dx, dy);
}

HL_PRIM bool HL_NAME(joy_rumble)(SDL_Joystick *joy, int low, int high, int duration) {
	return SDL_RumbleJoystick(joy, (Uint16)low, (Uint16)high, (Uint32)duration);
}

HL_PRIM int HL_NAME(joy_player_index)(SDL_Joystick *joy) {
	return SDL_GetJoystickPlayerIndex(joy);
}

HL_PRIM int HL_NAME(joy_product)(SDL_Joystick *joy) {
	return (int)SDL_GetJoystickProduct(joy);
}

HL_PRIM int HL_NAME(joy_vendor)(SDL_Joystick *joy) {
	return (int)SDL_GetJoystickVendor(joy);
}

HL_PRIM int HL_NAME(joy_product_version)(SDL_Joystick *joy) {
	return (int)SDL_GetJoystickProductVersion(joy);
}

// clipboard

HL_PRIM bool HL_NAME(set_clipboard_text)(vbyte *text) {
	return SDL_SetClipboardText((char*)text);
}

HL_PRIM char *HL_NAME(get_clipboard_text)() {
	char *chr = SDL_GetClipboardText();
	if (chr) {
		vbyte* bytes = hl_copy_bytes((const vbyte*)chr, (int)SDL_strlen(chr) + 1);
		SDL_free(chr);
		return (char*)bytes;
	}
	return NULL;
}

// display

HL_PRIM vbyte *HL_NAME(get_displays)() {
	int n = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&n);
	vbyte *buf = (vbyte*)hl_alloc_bytes(n * 256);
	vbyte *pos = buf;
	for (int i = 0; i < n; i++) {
		const char *name = SDL_GetDisplayName(displays[i]);
		if (name) {
			int len = (int)SDL_strlen(name);
			memcpy(pos, name, len);
			pos += len;
		}
		*pos++ = 0;
	}
	*pos = 0;
	SDL_free(displays);
	return buf;
}

HL_PRIM vbyte *HL_NAME(get_current_display_mode)(SDL_Window *win) {
	vbyte *buf = (vbyte*)hl_alloc_bytes(256);
	vbyte *pos = buf;
	SDL_DisplayID display_id = win ? SDL_GetDisplayForWindow(win) : 0;
	const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(display_id);
	if (!mode) mode = SDL_GetCurrentDisplayMode(display_id);
	if (mode) {
		*(int*)pos = mode->w; pos += 4;
		*(int*)pos = mode->h; pos += 4;
		*(int*)pos = (int)mode->refresh_rate; pos += 4;
		*(int*)pos = (int)mode->format; pos += 4;
	}
	return buf;
}

HL_PRIM vbyte *HL_NAME(get_display_modes)(int displayId) {
	vbyte *buf = (vbyte*)hl_alloc_bytes(256 * 32);
	vbyte *pos = buf;
	int num_modes = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes((SDL_DisplayID)displayId, &num_modes);
	for (int i = 0; i < num_modes && i < 32; i++) {
		const SDL_DisplayMode *mode = modes[i];
		if (mode) {
			*(int*)pos = (int)mode->format; pos += 4;
			*(int*)pos = mode->w; pos += 4;
			*(int*)pos = mode->h; pos += 4;
			*(int*)pos = (int)mode->refresh_rate; pos += 4;
			*(int*)pos = 0; pos += 4; // padding
		}
	}
	*(int*)pos = 0; pos += 4; // sentinel
	*(int*)pos = 0; pos += 4;
	*(int*)pos = 0; pos += 4;
	*(int*)pos = 0; pos += 4;
	*(int*)pos = 0; pos += 4;
	SDL_free(modes);
	return buf;
}

HL_PRIM vbyte *HL_NAME(get_devices)() {
	vbyte *buf = (vbyte*)hl_alloc_bytes(4096);
	vbyte *pos = buf;
	int num = SDL_GetNumVideoDrivers();
	for (int i = 0; i < num; i++) {
		const char *name = SDL_GetVideoDriver(i);
		if (name) {
			int len = (int)SDL_strlen(name);
			memcpy(pos, name, len);
			pos += len;
		}
		*pos++ = 0;
	}
	*pos = 0;
	return buf;
}

HL_PRIM void HL_NAME(set_drag_and_drop_enabled)(bool enable) {
	SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, enable ? "1" : "0");
}

HL_PRIM bool HL_NAME(get_drag_and_drop_enabled)() {
	const char *val = SDL_GetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH);
	return val && SDL_strcmp(val, "1") == 0;
}

// touch

HL_PRIM int HL_NAME(touch_count)() {
	int count = 0;
	SDL_TouchID *touchIds = SDL_GetTouchDevices(&count);
	SDL_free(touchIds);
	return count;
}

HL_PRIM int HL_NAME(touch_get_finger_count)(int idx) {
	int count = 0;
	SDL_TouchID *touchIds = SDL_GetTouchDevices(&count);
	if (idx >= count) {
		SDL_free(touchIds);
		return 0;
	}
	SDL_TouchID touchId = touchIds[idx];
	SDL_free(touchIds);
	int fingerCount = 0;
	SDL_Finger **fingers = SDL_GetTouchFingers(touchId, &fingerCount);
	SDL_free(fingers);
	return fingerCount;
}

HL_PRIM void HL_NAME(touch_get_finger)(int touchIdx, int fingerIdx, float *x, float *y, float *dx, float *dy, float *pressure) {
	int count = 0;
	SDL_TouchID *touchIds = SDL_GetTouchDevices(&count);
	if (touchIdx >= count) {
		SDL_free(touchIds);
		return;
	}
	SDL_TouchID touchId = touchIds[touchIdx];
	SDL_free(touchIds);
	int fingerCount = 0;
	SDL_Finger **fingers = SDL_GetTouchFingers(touchId, &fingerCount);
	if (fingerIdx < fingerCount && fingers[fingerIdx]) {
		*x = fingers[fingerIdx]->x;
		*y = fingers[fingerIdx]->y;
		*dx = 0;
		*dy = 0;
		*pressure = fingers[fingerIdx]->pressure;
	}
	SDL_free(fingers);
}

// cursor

HL_PRIM SDL_Cursor *HL_NAME(create_cursor)(SDL_Surface *surface, int hotX, int hotY) {
	return SDL_CreateColorCursor(surface, hotX, hotY);
}

HL_PRIM SDL_Cursor *HL_NAME(create_system_cursor)(int id) {
	return SDL_CreateSystemCursor((SDL_SystemCursor)id);
}

HL_PRIM void HL_NAME(set_cursor)(SDL_Cursor *cursor) {
	SDL_SetCursor(cursor);
}

HL_PRIM void HL_NAME(free_cursor)(SDL_Cursor *cursor) {
	SDL_DestroyCursor(cursor);
}

HL_PRIM void HL_NAME(show_cursor)() {
	SDL_ShowCursor();
}

HL_PRIM void HL_NAME(hide_cursor)() {
	SDL_HideCursor();
}

HL_PRIM bool HL_NAME(cursor_showing)() {
	return SDL_CursorVisible();
}

// surface

HL_PRIM SDL_Surface *HL_NAME(create_rgb_surface)(int width, int height, int depth, int rMask, int gMask, int bMask, int aMask) {
	return SDL_CreateSurface(width, height, SDL_PIXELFORMAT_UNKNOWN);
}

HL_PRIM void HL_NAME(free_surface)(SDL_Surface *surface) {
	SDL_DestroySurface(surface);
}

HL_PRIM SDL_Surface *HL_NAME(load_bmp)(vbyte *path) {
	return SDL_LoadBMP((char*)path);
}

HL_PRIM bool HL_NAME(save_bmp)(SDL_Surface *surface, vbyte *path) {
	return SDL_SaveBMP(surface, (char*)path);
}

HL_PRIM int HL_NAME(surface_get_width)(SDL_Surface *surface) {
	return surface->w;
}

HL_PRIM int HL_NAME(surface_get_height)(SDL_Surface *surface) {
	return surface->h;
}

HL_PRIM void *HL_NAME(surface_get_pixels)(SDL_Surface *surface) {
	return surface->pixels;
}

// render

HL_PRIM SDL_Renderer *HL_NAME(create_renderer)(SDL_Window *win, int index, int flags) {
	return SDL_CreateRenderer(win, NULL);
}

HL_PRIM void HL_NAME(destroy_renderer)(SDL_Renderer *renderer) {
	SDL_DestroyRenderer(renderer);
}

HL_PRIM SDL_Texture *HL_NAME(create_texture)(SDL_Renderer *renderer, int format, int access, int width, int height) {
	return SDL_CreateTexture(renderer, (Uint32)format, (SDL_TextureAccess)access, width, height);
}

HL_PRIM void HL_NAME(destroy_texture)(SDL_Texture *texture) {
	SDL_DestroyTexture(texture);
}

HL_PRIM void HL_NAME(render_clear)(SDL_Renderer *renderer) {
	SDL_RenderClear(renderer);
}

HL_PRIM void HL_NAME(render_present)(SDL_Renderer *renderer) {
	SDL_RenderPresent(renderer);
}

HL_PRIM void HL_NAME(set_render_draw_color)(SDL_Renderer *renderer, int r, int g, int b, int a) {
	SDL_SetRenderDrawColor(renderer, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
}

HL_PRIM void HL_NAME(render_fill_rect)(SDL_Renderer *renderer, int x, int y, int w, int h) {
	SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
	SDL_RenderFillRect(renderer, &rect);
}

HL_PRIM void HL_NAME(render_rect)(SDL_Renderer *renderer, int x, int y, int w, int h) {
	SDL_FRect rect = { (float)x, (float)y, (float)w, (float)h };
	SDL_RenderRect(renderer, &rect);
}

HL_PRIM void HL_NAME(render_line)(SDL_Renderer *renderer, int x1, int y1, int x2, int y2) {
	SDL_RenderLine(renderer, (float)x1, (float)y1, (float)x2, (float)y2);
}

HL_PRIM void HL_NAME(render_point)(SDL_Renderer *renderer, int x, int y) {
	SDL_RenderPoint(renderer, (float)x, (float)y);
}

HL_PRIM void HL_NAME(render_read_pixels)(SDL_Renderer *renderer, int x, int y, int w, int h) {
	// SDL3: SDL_RenderReadPixels returns a surface
	SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
	if (surface) SDL_DestroySurface(surface);
}

// time

HL_PRIM double HL_NAME(get_ticks)() {
	return (double)SDL_GetTicks();
}

HL_PRIM double HL_NAME(performance_counter)() {
	return (double)SDL_GetPerformanceCounter();
}

HL_PRIM double HL_NAME(performance_frequency)() {
	return (double)SDL_GetPerformanceFrequency();
}

// DEFINE_PRIM for all remaining functions

DEFINE_PRIM(_I32, gctrl_count, _NO_ARG);
DEFINE_PRIM(_ABSTRACT(sdl_gamepad), gctrl_open, _I32);
DEFINE_PRIM(_VOID, gctrl_close, _ABSTRACT(sdl_gamepad));
DEFINE_PRIM(_ABSTRACT(sdl_gamepad), gctrl_from_id, _I32);
DEFINE_PRIM(_I32, gctrl_get_id, _ABSTRACT(sdl_gamepad));
DEFINE_PRIM(_I32, gctrl_get_axis, _ABSTRACT(sdl_gamepad) _I32);
DEFINE_PRIM(_BOOL, gctrl_get_button, _ABSTRACT(sdl_gamepad) _I32);
DEFINE_PRIM(_BYTES, gctrl_name, _ABSTRACT(sdl_gamepad));
DEFINE_PRIM(_I32, gctrl_player_index, _ABSTRACT(sdl_gamepad));
DEFINE_PRIM(_I32, gctrl_product, _ABSTRACT(sdl_gamepad));
DEFINE_PRIM(_I32, gctrl_vendor, _ABSTRACT(sdl_gamepad));
DEFINE_PRIM(_I32, gctrl_product_version, _ABSTRACT(sdl_gamepad));
DEFINE_PRIM(_BOOL, gctrl_rumble, _ABSTRACT(sdl_gamepad) _I32 _I32 _I32);

DEFINE_PRIM(_ABSTRACT(sdl_joystick), joy_open, _I32);
DEFINE_PRIM(_VOID, joy_close, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_get_id, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_BYTES, joy_name, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_num_axes, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_num_buttons, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_num_hats, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_num_balls, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_get_axis, _ABSTRACT(sdl_joystick) _I32);
DEFINE_PRIM(_I32, joy_get_hat, _ABSTRACT(sdl_joystick) _I32);
DEFINE_PRIM(_BOOL, joy_get_button, _ABSTRACT(sdl_joystick) _I32);
DEFINE_PRIM(_I32, joy_get_ball, _ABSTRACT(sdl_joystick) _REF(_I32) _REF(_I32));
DEFINE_PRIM(_BOOL, joy_rumble, _ABSTRACT(sdl_joystick) _I32 _I32 _I32);
DEFINE_PRIM(_I32, joy_player_index, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_product, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_vendor, _ABSTRACT(sdl_joystick));
DEFINE_PRIM(_I32, joy_product_version, _ABSTRACT(sdl_joystick));

DEFINE_PRIM(_BOOL, set_clipboard_text, _BYTES);
DEFINE_PRIM(_BYTES, get_clipboard_text, _NO_ARG);

DEFINE_PRIM(_BYTES, get_displays, _NO_ARG);
DEFINE_PRIM(_BYTES, get_current_display_mode, TWIN);
DEFINE_PRIM(_BYTES, get_display_modes, _I32);
DEFINE_PRIM(_BYTES, get_devices, _NO_ARG);
DEFINE_PRIM(_VOID, set_drag_and_drop_enabled, _BOOL);
DEFINE_PRIM(_BOOL, get_drag_and_drop_enabled, _NO_ARG);

DEFINE_PRIM(_I32, touch_count, _NO_ARG);
DEFINE_PRIM(_I32, touch_get_finger_count, _I32);
DEFINE_PRIM(_VOID, touch_get_finger, _I32 _I32 _REF(_F32) _REF(_F32) _REF(_F32) _REF(_F32) _REF(_F32));

DEFINE_PRIM(_ABSTRACT(sdl_cursor), create_cursor, _SURF _I32 _I32);
DEFINE_PRIM(_ABSTRACT(sdl_cursor), create_system_cursor, _I32);
DEFINE_PRIM(_VOID, set_cursor, _ABSTRACT(sdl_cursor));
DEFINE_PRIM(_VOID, free_cursor, _ABSTRACT(sdl_cursor));
DEFINE_PRIM(_VOID, show_cursor, _NO_ARG);
DEFINE_PRIM(_VOID, hide_cursor, _NO_ARG);
DEFINE_PRIM(_BOOL, cursor_showing, _NO_ARG);

DEFINE_PRIM(_SURF, create_rgb_surface, _I32 _I32 _I32 _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, free_surface, _SURF);
DEFINE_PRIM(_SURF, load_bmp, _BYTES);
DEFINE_PRIM(_BOOL, save_bmp, _SURF _BYTES);
DEFINE_PRIM(_I32, surface_get_width, _SURF);
DEFINE_PRIM(_I32, surface_get_height, _SURF);
DEFINE_PRIM(_REF(_BYTES), surface_get_pixels, _SURF);

DEFINE_PRIM(_ABSTRACT(sdl_renderer), create_renderer, TWIN _I32 _I32);
DEFINE_PRIM(_VOID, destroy_renderer, _ABSTRACT(sdl_renderer));
DEFINE_PRIM(_ABSTRACT(sdl_texture), create_texture, _ABSTRACT(sdl_renderer) _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, destroy_texture, _ABSTRACT(sdl_texture));
DEFINE_PRIM(_VOID, render_clear, _ABSTRACT(sdl_renderer));
DEFINE_PRIM(_VOID, render_present, _ABSTRACT(sdl_renderer));
DEFINE_PRIM(_VOID, set_render_draw_color, _ABSTRACT(sdl_renderer) _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, render_fill_rect, _ABSTRACT(sdl_renderer) _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, render_rect, _ABSTRACT(sdl_renderer) _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, render_line, _ABSTRACT(sdl_renderer) _I32 _I32 _I32 _I32);
DEFINE_PRIM(_VOID, render_point, _ABSTRACT(sdl_renderer) _I32 _I32);
DEFINE_PRIM(_VOID, render_read_pixels, _ABSTRACT(sdl_renderer) _I32 _I32 _I32 _I32);

DEFINE_PRIM(_F64, get_ticks, _NO_ARG);
DEFINE_PRIM(_F64, performance_counter, _NO_ARG);
DEFINE_PRIM(_F64, performance_frequency, _NO_ARG);
