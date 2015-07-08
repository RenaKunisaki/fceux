void SetOpenGLPalette(uint8 *data);
void BlitOpenGL(uint8 *buf);
void KillOpenGL(void);
int InitOpenGL(int left, int right, int top, int bottom,
	double xscale, double yscale,
	int efx, int ipolate,
	int xstretch, int ystretch,
	SDL_Surface *screenBuf);

