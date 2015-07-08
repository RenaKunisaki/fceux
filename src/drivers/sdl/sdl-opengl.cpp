#define GL_GLEXT_LEGACY

#include "sdl.h"
#include "sdl-opengl.h"
#include "../common/vidblit.h"
#include "../../utils/memory.h"

#ifdef APPLEOPENGL
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <OpenGL/glext.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>
#endif
#include <cstring>
#include <cstdlib>

#ifndef APIENTRY
#define APIENTRY
#endif

#define LFG(x) if(!(##x = (x##_Func) SDL_GL_GetProcAddress(#x))) return(0);
#define LFGN(x) p_##x = (x##_Func) SDL_GL_GetProcAddress(#x)

extern Config *g_config;

#ifdef _S9XLUA_H
	extern GLuint g_luaDisplayList;
#endif

static struct {
	//visible area of the game screen.
	//right and bottom are not inclusive.
	double left;
	double right;
	double top;
	double bottom;

	//scale setting.
	struct {
		double x;
		double y;
		int isAuto;
	} scale;

	//stretch setting.
	struct {
		int x;
		int y;
	} stretch;

	//scanline setting.
	int scanlines;

	//dimensions of the display window.
	struct {
		double width;
		double height;
	} window;

	//texture IDs.
	struct {
		GLuint game;
		GLuint scanlines;
	} texture;

	//display list IDs.
	struct {
		GLuint game;
		//Lua display list is defined above because it's global
	} list;

	//actual screen buffer.
	SDL_Surface *buf;
	void *HiBuffer; //used for 8-bit-to-RGB colour conversion
} screen;


typedef void APIENTRY (*glColorTableEXT_Func)(
	GLenum target, GLenum internalformat, GLsizei width, GLenum format,
	GLenum type, const GLvoid *table);
glColorTableEXT_Func p_glColorTableEXT;


void SetOpenGLPalette(uint8 *data) {
	if(!screen.HiBuffer) {
		glBindTexture(GL_TEXTURE_2D, screen.texture.game);
		p_glColorTableEXT(GL_TEXTURE_2D, GL_RGB, 256,
						GL_RGBA, GL_UNSIGNED_BYTE, data);
	} else {
		SetPaletteBlitToHigh((uint8*)data);
	}
}


void BlitOpenGL(uint8 *buf) {
	//map the game video output to the texture object
	glEnable(GL_TEXTURE_2D);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindTexture(GL_TEXTURE_2D, screen.texture.game);

	if(screen.HiBuffer) {
		Blit8ToHigh(buf, (uint8*)screen.HiBuffer, 256, 240, 256*4, 1, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0,
					GL_RGBA, GL_UNSIGNED_BYTE, screen.HiBuffer);
	}
	else {
		//glPixelStorei(GL_UNPACK_ROW_LENGTH, 256);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 256, 256, 0,
					GL_COLOR_INDEX, GL_UNSIGNED_BYTE, buf);
	}

	glCallList(screen.list.game);
	SDL_GL_SwapBuffers();
}


void KillOpenGL(void) {
	GLuint textures[2] = {screen.texture.game, screen.texture.scanlines};
	glDeleteTextures(2, textures);
	if(screen.list.game) glDeleteLists(screen.list.game, 1);
	if(screen.HiBuffer)  free(screen.HiBuffer);

	screen.texture.game      = 0;
	screen.texture.scanlines = 0;
	screen.list.game         = 0;
	screen.HiBuffer          = 0;
}


static void regenerateDisplayList() {
	if(!screen.list.game) screen.list.game = glGenLists(1);
	glNewList(screen.list.game, GL_COMPILE);

	float s_left   = screen.left   / 256.0; //corners of screen
	float s_right  = screen.right  / 256.0;
	float s_bottom = screen.bottom / 256.0;
	float s_top    = screen.top    / 256.0;
	float t_left   = -1.0; //corners of target
	float t_right  =  1.0;
	float t_bottom = -1.0;
	float t_top    =  1.0;

	//array of (x,y) vertices for source and destination
	float s_vx[4] = {s_left,   s_right,  s_right, s_left};
	float s_vy[4] = {s_bottom, s_bottom, s_top,   s_top };
	float t_vx[4] = {t_left,   t_right,  t_right, t_left};
	float t_vy[4] = {t_bottom, t_bottom, t_top,   t_top };
	int i;

	glDisable(GL_BLEND);
	glBegin(GL_QUADS);
	for(i=0; i<4; i++) {
		glTexCoord2f(s_vx[i], s_vy[i]);
		glVertex2f  (t_vx[i], t_vy[i]);
	}
	glEnd();

	if(screen.scanlines) {
		glEnable(GL_BLEND);
		glBindTexture(GL_TEXTURE_2D, screen.texture.scanlines);
		glBlendFunc(GL_DST_COLOR, GL_SRC_ALPHA);

		glBegin(GL_QUADS);
		for(i=0; i<4; i++) {
			glTexCoord2f(s_vx[i], s_vy[i]);
			glVertex2f  (t_vx[i], t_vy[i]);
		}
		glEnd();
	}

#ifdef _S9XLUA_H
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPushMatrix();
	glOrtho(screen.left, screen.right, screen.bottom, screen.top, 0.0, 1.0);
	glLineWidth(screen.scale.x);
	glCallList(g_luaDisplayList);
	glPopMatrix();
#endif

	glEndList();
}


static void createScanlineTexture(int filter) {
	uint8 *buf;
	int x, y;

	screen.scanlines = 1;
	glBindTexture  (GL_TEXTURE_2D, screen.texture.scanlines);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);

	//XXX there's got to be a more efficient way to do this
	buf = (uint8*)FCEU_dmalloc(256 * (256*2) * 4);
	for(y=0; y<(256*2); y++) {
		for(x=0; x<256; x++) {
			buf[y*256*4+x*4]=0;
			buf[y*256*4+x*4+1]=0;
			buf[y*256*4+x*4+2]=0;
			buf[y*256*4+x*4+3]=(y&1)?0x00:0xFF; //?0xa0:0xFF; // <-- Pretty
			//buf[y*256+x]=(y&1)?0x00:0xFF;
		}
	}
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256,
		(screen.scanlines==2)?256*4:512, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
	FCEU_dfree(buf);
}


static void createGameVideoTexture(int filter) {
	glBindTexture  (GL_TEXTURE_2D, screen.texture.game);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
}


static void initPalettedTextureExtension(int efx) {
	//Don't print warning message if palette extension is disabled by user
	if(!(efx&2)) {
		FCEU_printf("Paletted texture extension not found.  "
			"Using slower texture format...\n");
	}
	screen.HiBuffer = FCEU_malloc(4*256*256);
	memset(screen.HiBuffer, 0x00, 4*256*256);

#ifndef LSB_FIRST
	InitBlitToHigh(4,0xFF000000,0xFF0000,0xFF00,efx&2,0,0);
#else
	InitBlitToHigh(4,0xFF,0xFF00,0xFF0000,efx&2,0,0);
#endif
}


static void initExtensions() {
//	LFG(glBindTexture);
	LFGN(glColorTableEXT);
//	LFG(glTexImage2D);
//	LFG(glBegin);
//	LFG(glVertex2f);
//	LFG(glTexCoord2f);
//	LFG(glEnd);
//	LFG(glEnable);
//	LFG(glBlendFunc);
//	LFG(glGetString);
//	LFG(glViewport);
//	LFG(glGenTextures);
//	LFG(glDeleteTextures);
//	LFG(glTexParameteri);
//	LFG(glClearColor);
//	LFG(glLoadIdentity);
//	LFG(glClear);
//	LFG(glMatrixMode);
//	LFG(glDisable);
}


int InitOpenGL(int left, int right, int top, int bottom,
	double xscale, double yscale,
	int efx, int ipolate,
	int xstretch, int ystretch,
	SDL_Surface *screenBuf)
{
	screen.left      = left;
	screen.right     = right;
	screen.top       = top;
	screen.bottom    = bottom;
	screen.scale.x   = xscale;
	screen.scale.y   = yscale;
	screen.stretch.x = xstretch;
	screen.stretch.y = ystretch;
	screen.buf       = screenBuf;
	screen.HiBuffer  = 0;
	screen.scanlines = 0;
	screen.list.game = 0;
	g_config->getOption("SDL.AutoScale", &screen.scale.isAuto);
	//printf("Init OpenGL: rect=(l=%d r=%d t=%d b=%d) scale=(%1.1f, %1.1f) "
	//	"stretch=(%d, %d) ipolate=%d efx=0x%04X\n",
	//	left, top, right, bottom, xs, ys, stretchx, stretchy, ipolate, efx);


	//get the initial viewport, which tells us the window size.
	double viewport[4];
	glGetDoublev(GL_VIEWPORT, viewport);
	screen.window.width  = viewport[2] - viewport[0];
	screen.window.height = viewport[3] - viewport[1];


	//use paletted textures if available
	initExtensions();
	const char *extensions = (const char*)glGetString(GL_EXTENSIONS);
	if((efx&2) || !extensions || !p_glColorTableEXT
	|| !strstr(extensions,"GL_EXT_paletted_texture")) {
		initPalettedTextureExtension(efx);
	}


	/* if(screenBuf->flags & SDL_FULLSCREEN) {
		//XXX still necessary? what is this even doing?
		//seems to be just scaling up to fit the screen and preserving the
		//aspect ratio, which we already do below.
		screen.scale.x = (double)screenBuf->w / (double)(right  - left);
		screen.scale.y = (double)screenBuf->h / (double)(bottom - top);
		if(screen.scale.x < screen.scale.y) screen.scale.y = screen.scale.x;
		if(screen.scale.y < screen.scale.x) screen.scale.x = screen.scale.y;
	} */


	//generate the textures for video output and scanlines
	int filter = ipolate ? GL_LINEAR : GL_NEAREST;
	GLuint textures[2];
	glGenTextures(2, &textures[0]);
	screen.texture.game      = textures[0];
	screen.texture.scanlines = textures[1];

	if(efx&1) createScanlineTexture(filter);
	createGameVideoTexture(filter);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);	// Background color to black.


	//set up our matrix and viewport;
	//XXX honour manual scale settings. need to figure out how to set up the
	//ortho and coords for that so that it's still centred (or in a corner/edge
	//if we wanted to add that option).
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	double ax = 1.0, ay = 1.0; //aspect
	if(screen.window.width > screen.window.height)
		 ax = screen.window.width  / screen.window.height;
	else ay = screen.window.height / screen.window.width;
	if(screen.stretch.x) ax = 1.0; //allow stretching horizontally?
	if(screen.stretch.y) ay = 1.0; //allow stretching vertically?
	glOrtho(-1*ax, 1*ax, -1*ay, 1*ay, 0.0, 1.0);

	//(when stretching is disabled, game will scale but keep aspect ratio.)
	//XXX do we need to factor in the actual NES ratio (256/240) here?
	//it looks fine now but maybe I just can't tell.


	// In a double buffered setup with page flipping,
	//be sure to clear both buffers.
	glClear(GL_COLOR_BUFFER_BIT); SDL_GL_SwapBuffers();
	glClear(GL_COLOR_BUFFER_BIT); SDL_GL_SwapBuffers();

	regenerateDisplayList();
	return 1; //success!
}
