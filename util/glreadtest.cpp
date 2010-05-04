/* Copyright (C)2004 Landmark Graphics Corporation
 * Copyright (C)2005 Sun Microsystems, Inc.
 * Copyright (C)2010 D. R. Commander
 *
 * This library is free software and may be redistributed and/or modified under
 * the terms of the wxWindows Library License, Version 3.1 or (at your option)
 * any later version.  The full license is in the LICENSE.txt file included
 * with this distribution.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * wxWindows Library License for more details.
 */

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "rrutil.h"
#include "rrtimer.h"
#include "rrerror.h"
#include <errno.h>
#define GL_GLEXT_PROTOTYPES
#include "../rr/glx.h"
#ifdef USEGLP
#include <GL/glp.h>
#endif
#ifdef MESAGLU
#include <mesa/glu.h>
#else
#include <GL/glu.h>
#endif
#include "x11err.h"

static int ALIGN=1;
#define PAD(w) (((w)+(ALIGN-1))&(~(ALIGN-1)))
#define BMPPAD(pitch) ((pitch+(sizeof(int)-1))&(~(sizeof(int)-1)))

//////////////////////////////////////////////////////////////////////
// Structs and globals
//////////////////////////////////////////////////////////////////////

typedef struct _pixelformat
{
	unsigned long roffset, goffset, boffset;
	int pixelsize;
	int glformat;
	int bgr;
	const char *name;
} pixelformat;

static int FORMATS=4
 #ifdef GL_BGRA_EXT
 +1
 #endif
 #ifdef GL_BGR_EXT
 +1
 #endif
 #ifdef GL_ABGR_EXT
 +1
 #endif
 ;

pixelformat pix[4
 #ifdef GL_BGRA_EXT
 +1
 #endif
 #ifdef GL_BGR_EXT
 +1
 #endif
 #ifdef GL_ABGR_EXT
 +1
 #endif
 ]={
	{0, 0, 0, 1, GL_LUMINANCE, 0, "LUM"},
	{0, 0, 0, 1, GL_RED, 0, "RED"},
	#ifdef GL_BGRA_EXT
	{2, 1, 0, 4, GL_BGRA_EXT, 1, "BGRA"},
	#endif
	#ifdef GL_ABGR_EXT
	{3, 2, 1, 4, GL_ABGR_EXT, 0, "ABGR"},
	#endif
	#ifdef GL_BGR_EXT
	{2, 1, 0, 3, GL_BGR_EXT, 1, "BGR"},
	#endif
	{0, 1, 2, 4, GL_RGBA, 0, "RGBA"},
	{0, 1, 2, 3, GL_RGB, 0, "RGB"},
};

#ifdef XDK
// Exceed likes to redefine stdio, so we un-redefine it :/
#undef fprintf
#undef printf
#undef putchar
#undef putc
#undef puts
#undef fputc
#undef fputs
#undef perror
#define GLX11
#endif
#ifndef GLX_X_VISUAL_TYPE
#define GLX_X_VISUAL_TYPE 0x22
#endif
#ifndef GLX_TRUE_COLOR
#define GLX_TRUE_COLOR 0x8002
#endif
#ifndef GLX_PSEUDO_COLOR
#define GLX_PSEUDO_COLOR 0x8004
#endif
#ifndef GLX_DRAWABLE_TYPE
#define GLX_DRAWABLE_TYPE 0x8010
#endif
#ifndef GLX_PBUFFER_BIT
#define GLX_PBUFFER_BIT 0x00000004
#endif
#ifndef GLX_TRANSPARENT_TYPE
#define GLX_TRANSPARENT_TYPE 0x23
#endif
#ifndef GLX_TRANSPARENT_INDEX
#define GLX_TRANSPARENT_INDEX 0x8009
#endif
#ifndef GLX_PBUFFER_HEIGHT
#define GLX_PBUFFER_HEIGHT 0x8040
#endif
#ifndef GLX_PBUFFER_WIDTH
#define GLX_PBUFFER_WIDTH 0x8041
#endif

#define bench_name		"GLreadtest"

#define _WIDTH            701
#define _HEIGHT           701

int WIDTH=_WIDTH, HEIGHT=_HEIGHT;
Display *dpy=NULL;  Window win=0;
rrtimer timer;
int useglp=0;
#ifdef USEGLP
int glpdevice=-1;
#endif
int usewindow=0, useci=0, useoverlay=0, visualid=0, loops=1, pbo=0;
double benchtime=1.0;

#define STRLEN 256

char *sigfig(int fig, char *string, double value)
{
	char format[80];
	double _l=(value==0.0)? 0.0:log10(fabs(value));  int l;
	if(_l<0.)
	{
		l=(int)fabs(floor(_l));
		snprintf(format, 79, "%%%d.%df", fig+l+1, fig+l-1);
	}
	else
	{
		l=(int)_l+1;
		if(fig<=l) snprintf(format, 79, "%%.0f");
		else snprintf(format, 79, "%%%d.%df", fig+1, fig-l);
	}
	snprintf(string, STRLEN-1, format, value);
	return(string);
}

//////////////////////////////////////////////////////////////////////
// Error handling
//////////////////////////////////////////////////////////////////////

extern "C" {
int xhandler(Display *dpy, XErrorEvent *xe)
{
	fprintf(stderr, "X11 Error: %s\n", x11error(xe->error_code));
	return 0;
}
} // extern "C"

//////////////////////////////////////////////////////////////////////
// Pbuffer setup
//////////////////////////////////////////////////////////////////////

void findvisual(XVisualInfo* &v
#ifndef GLX11
, GLXFBConfig &c
#endif
)
{
	int winattribs[]={GLX_RGBA, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8, None};
	int winattribsdb[]={GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8,
		GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
	int winattribsci[]={GLX_BUFFER_SIZE, 8, None, None, None, None, None, None};
	int winattribscidb[]={GLX_DOUBLEBUFFER, GLX_BUFFER_SIZE, 8, None, None,
		None, None, None, None};

	#ifndef GLX11
	int pbattribs[]={GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
		GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, None};
	int pbattribsdb[]={GLX_DOUBLEBUFFER, 1, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8, GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DRAWABLE_TYPE,
		GLX_PBUFFER_BIT, None};
	int pbattribsci[]={GLX_BUFFER_SIZE, 8, GLX_RENDER_TYPE, GLX_COLOR_INDEX_BIT,
		GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, None};
	int pbattribscidb[]={GLX_DOUBLEBUFFER, 1, GLX_BUFFER_SIZE, 8,
		GLX_RENDER_TYPE, GLX_COLOR_INDEX_BIT, GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
		None};
	GLXFBConfig *fbconfigs=NULL;  int nelements=0;
	#endif

	// Use GLX 1.1 functions here in case we're remotely displaying to
	// something that doesn't support GLX 1.3
	if(usewindow)
	{
		try
		{
			if(visualid)
			{
				XVisualInfo vtemp;  int n=0;
				vtemp.visualid=visualid;
				v=XGetVisualInfo(dpy, VisualIDMask, &vtemp, &n);
				if(!v || !n) _throw("Could not obtain visual");
				printf("Visual = 0x%.2x\n", (unsigned int)v->visualid);
				return;
			}
			if(useoverlay)
			{
				winattribsci[2]=winattribscidb[3]=GLX_LEVEL;
				winattribsci[3]=winattribscidb[4]=1;
			};
			if(!(v=glXChooseVisual(dpy, DefaultScreen(dpy), useci?
				winattribsci:winattribs))
				&& !(v=glXChooseVisual(dpy, DefaultScreen(dpy), useci?
				winattribscidb:winattribsdb)))
				_throw("Could not obtain Visual");
			printf("Visual = 0x%.2x\n", (unsigned int)v->visualid);
			return;
		}
		catch(...)
		{
			if(v) {XFree(v);  v=NULL;}  throw;
		}
	}

	#ifndef GLX11
	#ifdef USEGLP
	if(useglp)
	{
		pbattribs[6]=pbattribsdb[8]=pbattribsci[2]=pbattribscidb[4]=None;
		pbattribs[7]=pbattribsdb[9]=pbattribsci[3]=pbattribscidb[5]=None;
		pbattribs[8]=pbattribsdb[10]=pbattribsci[4]=pbattribscidb[6]=None;
		pbattribs[9]=pbattribsdb[11]=pbattribsci[5]=pbattribscidb[7]=None;
		fbconfigs=glPChooseFBConfig(glpdevice, useci? pbattribsci:pbattribs,
			&nelements);
		if(!fbconfigs) fbconfigs=glPChooseFBConfig(glpdevice,
			useci? pbattribscidb:pbattribsdb, &nelements);
	}
	else
	#endif
	{
		fbconfigs=glXChooseFBConfig(dpy, DefaultScreen(dpy), useci?
			pbattribsci:pbattribs, &nelements);
		if(!fbconfigs) fbconfigs=glXChooseFBConfig(dpy, DefaultScreen(dpy),
			useci?pbattribscidb:pbattribsdb, &nelements);
	}
	if(!nelements || !fbconfigs) _throw("Could not obtain Visual");
	c=fbconfigs[0];  XFree(fbconfigs);

	int fbcid=-1;
	#ifdef USEGLP
	if(useglp) glPGetFBConfigAttrib(c, GLP_FBCONFIG_ID, &fbcid);
	else
	#endif
		glXGetFBConfigAttrib(dpy, c, GLX_FBCONFIG_ID, &fbcid);
	printf("FB Config = 0x%.2x\n", fbcid);
	#endif
}

void pbufferinit(Display *dpy, Window win, XVisualInfo *v
#ifndef GLX11
, GLXFBConfig c
#endif
)
{
	#ifndef	GLX11
	GLXPbuffer pbuffer=0;
	int pbattribs[]={GLX_PBUFFER_WIDTH, 0, GLX_PBUFFER_HEIGHT, 0, None};
	#endif
	GLXContext ctx=0;

	// Use GLX 1.1 functions here in case we're remotely displaying to
	// something that doesn't support GLX 1.3
	if(usewindow)
	{
		try
		{
			if(!(ctx=glXCreateContext(dpy, v, NULL, True)))
				_throw("Could not create GL context");
			glXMakeCurrent(dpy, win, ctx);
			return;
		}
		catch(...)
		{
			if(ctx) {glXMakeCurrent(dpy, 0, 0);  glXDestroyContext(dpy, ctx);}
			throw;
		}
	}

	#ifndef GLX11
	try {

	if(!useglp) {if(usewindow) {errifnot(win);}  errifnot(dpy);}

	#ifdef USEGLP
	if(useglp)
		ctx=glPCreateNewContext(c, useci? GLX_COLOR_INDEX_TYPE:GLX_RGBA_TYPE, NULL);
	else
	#endif
	ctx=glXCreateNewContext(dpy, c, useci? GLX_COLOR_INDEX_TYPE:GLX_RGBA_TYPE,
		NULL, True);
	if(!ctx)	_throw("Could not create GL context");

	pbattribs[1]=WIDTH;  pbattribs[3]=HEIGHT;
	#ifdef USEGLP
	if(useglp)
		pbuffer=glPCreateBuffer(c, pbattribs);
	else
	#endif
	pbuffer=glXCreatePbuffer(dpy, c, pbattribs);
	if(!pbuffer) _throw("Could not create Pbuffer");

	#ifdef USEGLP
	if(useglp)
		glPMakeContextCurrent(pbuffer, pbuffer, ctx);
	else
	#endif
	glXMakeContextCurrent(dpy, pbuffer, pbuffer, ctx);

	} catch(...)
	{
		if(pbuffer)
		{
			#ifdef USEGLP
			if(useglp) glPDestroyBuffer(pbuffer);
			else
			#endif
			glXDestroyPbuffer(dpy, pbuffer);
		}
		if(ctx)
		{
			#ifdef USEGLP
			if(useglp)
			{
				glPMakeContextCurrent(0, 0, 0);  glPDestroyContext(ctx);
			}
			else
			#endif
			{
				glXMakeContextCurrent(dpy, 0, 0, 0);  glXDestroyContext(dpy, ctx);
			}
		}
		throw;
	}
	#endif
}

//////////////////////////////////////////////////////////////////////
// Useful functions
//////////////////////////////////////////////////////////////////////

char glerrstr[STRLEN]="No error";

static void check_errors(const char * tag)
{
	int i, error=0;  char *s;
	i=glGetError();
	if(i!=GL_NO_ERROR) error=1;
	while(i!=GL_NO_ERROR)
	{
		s=(char *)gluErrorString(i);
		if(s) snprintf(glerrstr, STRLEN-1, "OpenGL ERROR in %s: %s\n", tag, s);
		else snprintf(glerrstr, STRLEN-1, "OpenGL ERROR #%d in %s\n", i, tag);
		i=glGetError();
	}
	if(error) _throw(glerrstr);
}

//////////////////////////////////////////////////////////////////////
// Buffer initialization and checking
//////////////////////////////////////////////////////////////////////
void initbuf(int x, int y, int w, int h, int format, unsigned char *buf)
{
	int i, j, ps=pix[format].pixelsize;
	for(i=0; i<h; i++)
	{
		for(j=0; j<w; j++)
		{
			if(pix[format].glformat==GL_COLOR_INDEX)
				buf[(i*w+j)*ps]=((i+y)*(j+x))%32;
			else if(pix[format].glformat==GL_LUMINANCE
				|| pix[format].glformat==GL_RED)
				buf[(i*w+j)*ps]=((i+y)*(j+x))%256;
			else
			{
				buf[(i*w+j)*ps+pix[format].roffset]=((i+y)*(j+x))%256;
				buf[(i*w+j)*ps+pix[format].goffset]=((i+y)*(j+x)*2)%256;
				buf[(i*w+j)*ps+pix[format].boffset]=((i+y)*(j+x)*3)%256;
			}
		}
	}
}

int cmpbuf(int x, int y, int w, int h, int format, unsigned char *buf, int bassackwards)
{
	int i, j, l, ps=pix[format].pixelsize;
	for(i=0; i<h; i++)
	{
		l=bassackwards?h-i-1:i;
		for(j=0; j<w; j++)
		{
			if(pix[format].glformat==GL_COLOR_INDEX)
			{
				if(buf[l*PAD(w*ps)+j*ps]!=((i+y)*(j+x))%32) return 0;
			}
			else if(pix[format].glformat==GL_LUMINANCE
				|| pix[format].glformat==GL_RED)
			{
				if(buf[l*PAD(w*ps)+j*ps]!=((i+y)*(j+x))%256) return 0;
			}
			else
			{
				if(buf[l*PAD(w*ps)+j*ps+pix[format].roffset]!=((i+y)*(j+x))%256) return 0;
				if(buf[l*PAD(w*ps)+j*ps+pix[format].goffset]!=((i+y)*(j+x)*2)%256) return 0;
				if(buf[l*PAD(w*ps)+j*ps+pix[format].boffset]!=((i+y)*(j+x)*3)%256) return 0;
			}
		}
	}
	return 1;
}

// Makes sure the frame buffer has been cleared prior to a write
void clearfb(int format)
{
	unsigned char *buf=NULL;  int ps=3, glformat=GL_RGB;
	if(pix[format].glformat==GL_COLOR_INDEX)
	{
		glformat=pix[format].glformat;  ps=1;
	}
	if((buf=(unsigned char *)malloc(WIDTH*HEIGHT*ps))==NULL)
		_throw("Could not allocate buffer");
	memset(buf, 0xFF, WIDTH*HEIGHT*ps);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1); 
	glDrawBuffer(GL_FRONT);
	glReadBuffer(GL_FRONT);
	if(useci) glClearIndex(0.);
	else glClearColor(0., 0., 0., 0.);
	glClear(GL_COLOR_BUFFER_BIT);
	glReadPixels(0, 0, WIDTH, HEIGHT, glformat, GL_UNSIGNED_BYTE, buf);
	check_errors("frame buffer read");
	for(int i=0; i<WIDTH*HEIGHT*ps; i++)
	{
		if(buf[i]!=0) {fprintf(stderr, "Buffer was not cleared\n");  break;}
	}
	if(buf) free(buf);
}

//////////////////////////////////////////////////////////////////////
// The actual tests
//////////////////////////////////////////////////////////////////////

// Generic GL write test
void glwrite(int format)
{
	unsigned char *rgbaBuffer=NULL;  int n, ps=pix[format].pixelsize;
	double rbtime;
	char temps[STRLEN];

	try {

	fprintf(stderr, "glDrawPixels():   ");
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1); 
	glShadeModel(GL_FLAT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	clearfb(format);
	if((rgbaBuffer=(unsigned char *)malloc(WIDTH*HEIGHT*ps))==NULL)
		_throw("Could not allocate buffer");
	initbuf(0, 0, WIDTH, HEIGHT, format, rgbaBuffer);
	n=0;
	timer.start();
	do
	{
		glDrawPixels(WIDTH, HEIGHT, pix[format].glformat, GL_UNSIGNED_BYTE, rgbaBuffer);
		glFinish();
		n++;
	} while((rbtime=timer.elapsed())<benchtime || n<2);

	double avgmps=(double)n*(double)(WIDTH*HEIGHT)/((double)1000000.*rbtime);
	check_errors("frame buffer write");
	fprintf(stderr, "%s Mpixels/sec\n", sigfig(4, temps, avgmps));

	} catch(rrerror &e) {fprintf(stderr, "%s\n", e.getMessage());}

	if(rgbaBuffer) free(rgbaBuffer);
}

// Generic OpenGL readback test
void glread(int format)
{
	unsigned char *rgbaBuffer=NULL;  int n, ps=pix[format].pixelsize;
	double rbtime;  GLuint bufferid=0;
	char temps[STRLEN];

	try {

	fprintf(stderr, "glReadPixels():   ");
	glPixelStorei(GL_UNPACK_ALIGNMENT, ALIGN);
	glPixelStorei(GL_PACK_ALIGNMENT, ALIGN);
	glReadBuffer(GL_FRONT);
	if(pbo)
	{
		const char *ext=(const char *)glGetString(GL_EXTENSIONS);
		if(!ext || !strstr(ext, "GL_ARB_pixel_buffer_object"))
			_throw("GL_ARB_pixel_buffer_object extension not available");
		glGenBuffers(1, &bufferid);
		if(!bufferid) _throw("Could not generate PBO buffer");
		glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, bufferid);
		glBufferData(GL_PIXEL_PACK_BUFFER_ARB, PAD(WIDTH*ps)*HEIGHT, NULL,
			GL_STREAM_READ);
		check_errors("PBO initialization");
	}
	else
	{
		if((rgbaBuffer=(unsigned char *)malloc(PAD(WIDTH*ps)*HEIGHT))==NULL)
			_throw("Could not allocate buffer");
		memset(rgbaBuffer, 0, PAD(WIDTH*ps)*HEIGHT);
	}
	n=0;  rbtime=0.;
	double tmin=0., tmax=0., ssq=0., sum=0.;  int first=1;
	do
	{
		timer.start();
		if(pix[format].glformat==GL_LUMINANCE)
		{
			glPushAttrib(GL_PIXEL_MODE_BIT);
			glPixelTransferf(GL_RED_SCALE, (GLfloat)0.299);
			glPixelTransferf(GL_GREEN_SCALE, (GLfloat)0.587);
			glPixelTransferf(GL_BLUE_SCALE, (GLfloat)0.114);
		}
		if(pbo)
		{
			glReadPixels(0, 0, WIDTH, HEIGHT, pix[format].glformat, GL_UNSIGNED_BYTE,
				NULL);
			rgbaBuffer=(unsigned char *)glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB,
				GL_READ_ONLY);
			if(!rgbaBuffer) _throw("Could not map buffer");
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB);
		}
		else
			glReadPixels(0, 0, WIDTH, HEIGHT, pix[format].glformat, GL_UNSIGNED_BYTE,
				rgbaBuffer);

		if(pix[format].glformat==GL_LUMINANCE)
		{
			glPopAttrib();
		}
		double elapsed=timer.elapsed();
		if(first) {tmin=tmax=elapsed;  first=0;}
		else
		{
			if(elapsed<tmin) tmin=elapsed;
			if(elapsed>tmax) tmax=elapsed;
		}		
		n++;
		rbtime+=elapsed;
		ssq+=pow((double)(WIDTH*HEIGHT)/((double)1000000.*elapsed), 2.0);
		sum+=(double)(WIDTH*HEIGHT)/((double)1000000.*elapsed);
	} while(rbtime<benchtime || n<2);
	if(pbo)
	{
		rgbaBuffer=(unsigned char *)glMapBuffer(GL_PIXEL_PACK_BUFFER_ARB,
			GL_READ_ONLY);
		if(!rgbaBuffer) _throw("Could not map buffer");
	}
	if(!cmpbuf(0, 0, WIDTH, HEIGHT, format, rgbaBuffer, 0))
		_throw("ERROR: Bogus data read back.");
	double mean=sum/(double)n;
	double stddev=sqrt((ssq - 2.0*mean*sum + mean*mean*(double)n)/(double)n);
	double avgmps=(double)n*(double)(WIDTH*HEIGHT)/((double)1000000.*rbtime);
	double minmps=(double)(WIDTH*HEIGHT)/((double)1000000.*tmax);
	double maxmps=(double)(WIDTH*HEIGHT)/((double)1000000.*tmin);
	check_errors("frame buffer read");
	fprintf(stderr, "%s Mpixels/sec ", sigfig(4, temps, avgmps));
	fprintf(stderr, "(min = %s, ", sigfig(4, temps, minmps));
	fprintf(stderr, "max = %s, ", sigfig(4, temps, maxmps));
	fprintf(stderr, "sdev = %s)\n", sigfig(4, temps, stddev));

	} catch(rrerror &e) {fprintf(stderr, "%s\n", e.getMessage());}

	if(rgbaBuffer)
	{
		if(pbo)
		{
			if(!glUnmapBuffer(GL_PIXEL_PACK_BUFFER_ARB))
				_throw("Could not unmap buffer");
		}
		else free(rgbaBuffer);
	}
	if(pbo && bufferid>0)
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER_ARB, 0);
		glDeleteBuffers(1, &bufferid);
	}
}

void display(void)
{
	int format;

	for(format=0; format<FORMATS; format++)
	{
		fprintf(stderr, ">>>>>>>>>>  PIXEL FORMAT:  %s  <<<<<<<<<<\n", pix[format].name);

		#if defined(GL_ABGR_EXT) || defined(GL_BGRA_EXT) || defined(GL_BGR_EXT)
		const char *ext=(const char *)glGetString(GL_EXTENSIONS), *compext=NULL;
		#ifdef GL_ABGR_EXT
		if(pix[format].glformat==GL_ABGR_EXT) compext="GL_EXT_abgr";
		#endif
		#ifdef GL_BGRA_EXT
		if(pix[format].glformat==GL_BGRA_EXT) compext="GL_EXT_bgra";
		#endif
		#ifdef GL_BGR_EXT
		if(pix[format].glformat==GL_BGR_EXT) compext="GL_EXT_bgra";
		#endif
		if(compext && (!ext || !strstr(ext, compext)))
		{
			fprintf(stderr, "%s extension not available.  Skipping ...\n\n",
				compext);
			continue;
		}
		#endif

		glwrite(format);
		for(int i=0; i<loops; i++) glread(format);
		fprintf(stderr, "\n");
	}

	exit(0);
}

void usage(char **argv)
{
	fprintf(stderr, "\nUSAGE: %s [-h|-?] [-window] [-index] [-overlay]\n", argv[0]);
	fprintf(stderr, "       [-width <n>] [-height <n>] [-align <n>] [-visualid <xx>]\n");
	fprintf(stderr, "       [-rgb] [-rgba] [-bgr] [-bgra] [-abgr] [-time <t>] [-loop <l>] [-pbo]\n");
	#ifdef USEGLP
	fprintf(stderr, "       [-device <GLP device>]\n");
	#endif
	fprintf(stderr, "\n-h or -? = This screen\n");
	fprintf(stderr, "-window = Render to a window instead of a Pbuffer\n");
	fprintf(stderr, "-index = Test color index visual instead of RGB\n");
	fprintf(stderr, "-overlay = Render to 8-bit overlay window (implies -window and -index)\n");
	fprintf(stderr, "-width = Set drawable width to n pixels (default: %d)\n", _WIDTH);
	fprintf(stderr, "-height = Set drawable height to n pixels (default: %d)\n", _HEIGHT);
	fprintf(stderr, "-align = Set row alignment to n bytes (default: %d)\n", ALIGN);
	fprintf(stderr, "-visualid = Ignore visual selection and use this visual ID (hex) instead\n");
	fprintf(stderr, "-rgb = Test only RGB pixel format\n");
	fprintf(stderr, "-rgba = Test only RGBA pixel format\n");
	fprintf(stderr, "-bgr = Test only BGR pixel format\n");
	fprintf(stderr, "-bgra = Test only BGRA pixel format\n");
	fprintf(stderr, "-abgr = Test only ABGR pixel format\n");
	fprintf(stderr, "-time <t> = Run each test for <t> seconds\n");
	fprintf(stderr, "-loop <l> = Run readback test <l> times in a row\n");
	fprintf(stderr, "-pbo = Use pixel buffer objects to perform readback\n");
	#ifdef USEGLP
	fprintf(stderr, "-device = Set GLP device to use for rendering (default: Use GLX)\n");
	#endif
	fprintf(stderr, "\n");
	exit(0);
}

//////////////////////////////////////////////////////////////////////
// Main
//////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
	#ifdef USEGLP
	char *device=NULL;
	#endif
	fprintf(stderr, "\n%s v%s (Build %s)\n", bench_name, __VERSION, __BUILD);

	for(int i=0; i<argc; i++)
	{
		if(!stricmp(argv[i], "-h")) usage(argv);
		if(!stricmp(argv[i], "-?")) usage(argv);
		if(!stricmp(argv[i], "-window")) usewindow=1;
		if(!stricmp(argv[i], "-index")) useci=1;
		if(!stricmp(argv[i], "-overlay")) {useci=1;  useoverlay=1;  usewindow=1;}
		if(!stricmp(argv[i], "-pbo")) pbo=1;
		if(!stricmp(argv[i], "-rgb"))
		{
			pixelformat pixtemp={0, 1, 2, 3, GL_RGB, 0, "RGB"};
			pix[0]=pixtemp;
			FORMATS=1;
		}
		if(!stricmp(argv[i], "-rgba"))
		{
			pixelformat pixtemp={0, 1, 2, 4, GL_RGBA, 0, "RGBA"};
			pix[0]=pixtemp;
			FORMATS=1;
		}
		#ifdef GL_BGR_EXT
		if(!stricmp(argv[i], "-bgr"))
		{
			pixelformat pixtemp={2, 1, 0, 3, GL_BGR_EXT, 1, "BGR"};
			pix[0]=pixtemp;
			FORMATS=1;
		}
		#endif
		#ifdef GL_BGRA_EXT
		if(!stricmp(argv[i], "-bgra"))
		{
			pixelformat pixtemp={2, 1, 0, 4, GL_BGRA_EXT, 1, "BGRA"};
			pix[0]=pixtemp;
			FORMATS=1;
		}
		#endif
		#ifdef GL_ABGR_EXT
		if(!stricmp(argv[i], "-abgr"))
		{
			pixelformat pixtemp={3, 2, 1, 4, GL_ABGR_EXT, 0, "ABGR"};
			pix[0]=pixtemp;
			FORMATS=1;
		}
		#endif
		if(!stricmp(argv[i], "-loop") && i<argc-1)
		{
			int temp=atoi(argv[i+1]);  i++;
			if(temp>1) loops=temp;
		}
		if(!stricmp(argv[i], "-align") && i<argc-1)
		{
			int temp=atoi(argv[i+1]);  i++;
			if(temp>=1 && (temp&(temp-1))==0) ALIGN=temp;
		}
		if(!stricmp(argv[i], "-visualid") && i<argc-1)
		{
			int temp=0;
			sscanf(argv[i+1], "%x", &temp);
			if(temp>0) visualid=temp;  i++;
		}
		if(!stricmp(argv[i], "-width") && i<argc-1)
		{
			int temp=atoi(argv[i+1]);  i++;
			if(temp>=1) WIDTH=temp;
		}
		if(!stricmp(argv[i], "-height") && i<argc-1)
		{
			int temp=atoi(argv[i+1]);  i++;
			if(temp>=1) HEIGHT=temp;
		}
		if(!stricmp(argv[i], "-time") && i<argc-1)
		{
			double temp=atof(argv[i+1]);  i++;
			if(temp>0.0) benchtime=temp;
		}
		#ifdef USEGLP
		if(!strnicmp(argv[i], "-d", 2) && i<argc-1)
		{
			char **devices=NULL;  int ndevices=0;
			if((devices=glPGetDeviceNames(&ndevices))==NULL || ndevices<1)
			{
				fprintf(stderr, "ERROR: No GLP devices are registered.\n");
				exit(1);
			}
			if(!strnicmp(argv[i+1], "GLP", 3)) device=NULL;
			else device=argv[i+1];
			if((glpdevice=glPOpenDevice(device))<0)
			{
				fprintf(stderr, "ERROR: Could not open GLP device %s.\n", device);
				exit(1);
			}
			if(!device) device=devices[0];
			useglp=1;
		}
		#endif
	}

	try {

	if(usewindow && useglp)
		_throw("ERROR: Cannot render to a window if GLP mode is enabled.");
	if(argc<2) fprintf(stderr, "\n%s -h for advanced usage.\n", argv[0]);
	#ifdef USEGLP
	if(useglp) fprintf(stderr, "\nRendering to Pbuffer using GLP on device %s\n", device);
	#endif

	if(!useglp)
	{
		XSetErrorHandler(xhandler);
		if(!(dpy=XOpenDisplay(0))) {fprintf(stderr, "Could not open display %s\n", XDisplayName(0));  exit(1);}
		fprintf(stderr, "\nRendering to %s using GLX on display %s\n", usewindow?"window":"Pbuffer", DisplayString(dpy));
		if(pbo) fprintf(stderr, "Using PBO's for readback\n");

		if(DisplayWidth(dpy, DefaultScreen(dpy))<WIDTH && DisplayHeight(dpy, DefaultScreen(dpy))<HEIGHT)
		{
			fprintf(stderr, "ERROR: Please switch to a screen resolution of at least %d x %d.\n", WIDTH, HEIGHT);
			exit(1);
		}
	}

	if(useci)
	{
		FORMATS=1;
		pix[0].roffset=pix[0].goffset=pix[0].boffset=0;
		pix[0].pixelsize=1;
		pix[0].glformat=GL_COLOR_INDEX;
		pix[0].bgr=0;
		pix[0].name="INDEX";
	}

	XVisualInfo *v=NULL;
	#ifndef GLX11
	GLXFBConfig c=0;
	findvisual(v, c);
	#else
	usewindow=1;
	findvisual(v);
	#endif

	if(usewindow)
	{
		XSetWindowAttributes swa;
		Window root=DefaultRootWindow(dpy);
		swa.border_pixel=0;
		swa.event_mask=0;

		if(useoverlay)
		{
			errifnot(root=XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, WIDTH,
				HEIGHT, 0, WhitePixel(dpy, DefaultScreen(dpy)),
				BlackPixel(dpy, DefaultScreen(dpy))));
			XMapWindow(dpy, root);
			XSync(dpy, False);
		}

		if(useci)
		{
			swa.colormap=XCreateColormap(dpy, root, v->visual, AllocAll);
			XColor xc[32];  int i;
			if(v->colormap_size<32) _throw("Color map is not large enough");
			for(i=0; i<32; i++)
			{
				xc[i].red=(i<16? i*16:255)<<8;
				xc[i].green=(i<16? i*16:255-(i-16)*16)<<8;
				xc[i].blue=(i<16? 255:255-(i-16)*16)<<8;
				xc[i].flags = DoRed | DoGreen | DoBlue;
				xc[i].pixel=i;
			}
			XStoreColors(dpy, swa.colormap, xc, 32);
		}
		else
			swa.colormap=XCreateColormap(dpy, root, v->visual, AllocNone);
		errifnot(win=XCreateWindow(dpy, root, 0, 0, WIDTH,
			HEIGHT, 0, v->depth, InputOutput, v->visual,
			CWBorderPixel|CWColormap|CWEventMask, &swa));
		XMapWindow(dpy, win);
		XSync(dpy, False);
	}
	fprintf(stderr, "Drawable size = %d x %d pixels\n", WIDTH, HEIGHT);
	fprintf(stderr, "Using %d-byte row alignment\n\n", ALIGN);
	pbufferinit(dpy, win, v
	#ifndef GLX11
	, c
	#endif
	);

	if(v) XFree(v);
	display();
	return 0;

	} catch(rrerror &e) {fprintf(stderr, "%s\n", e.getMessage());}
}
