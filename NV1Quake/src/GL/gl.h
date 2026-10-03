/*
GL/gl.h -- type shim, not an OpenGL implementation.

id's glquake.h is the renderer interface every shared file in WinQuake sees,
and quakedef.h reaches it by a quoted include, which MSVC resolves relative to
WinQuake before it ever looks at our -I paths.  So there is no way to shadow
it, and nv1Quake does not try: it uses id's header as-is and supplies the NV1
side separately in nv1quake.h.

That leaves one problem.  id's glquake.h opens with <GL/gl.h> and <GL/glu.h>
and then spells a handful of its prototypes in GL types.  Angle-bracket
includes DO honour -I order, so these two files intercept that and provide just
the types, with no functions and no library behind them.  Nothing in nv1Quake
calls OpenGL; if anything ever tried, it would fail to link, which is the
correct outcome.
*/

#ifndef __gl_h_
#define __gl_h_

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

typedef unsigned int	GLenum;
typedef unsigned char	GLboolean;
typedef unsigned int	GLbitfield;
typedef signed char	GLbyte;
typedef short		GLshort;
typedef int		GLint;
typedef int		GLsizei;
typedef unsigned char	GLubyte;
typedef unsigned short	GLushort;
typedef unsigned int	GLuint;
typedef float		GLfloat;
typedef float		GLclampf;
typedef double		GLdouble;
typedef double		GLclampd;
typedef void		GLvoid;

#endif /* __gl_h_ */
