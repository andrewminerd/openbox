// openbox.cc for Openbox
// Copyright (c) 2001 Sean 'Shaleh' Perry <shaleh@debian.org>
// Copyright (c) 1997 - 2000 Brad Hughes (bhughes@tcac.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// stupid macros needed to access some functions in version 2 of the GNU C
// library
#ifndef   _GNU_SOURCE
#define   _GNU_SOURCE
#endif // _GNU_SOURCE

#ifdef    HAVE_CONFIG_H
#  include "../config.h"
#endif // HAVE_CONFIG_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#ifdef    SHAPE
#include <X11/extensions/shape.h>
#endif // SHAPE

#include "i18n.h"
#include "openbox.h"
#include "Basemenu.h"
#include "Clientmenu.h"
#include "Rootmenu.h"
#include "Screen.h"

#ifdef    SLIT
#include "Slit.h"
#endif // SLIT

#include "Toolbar.h"
#include "Window.h"
#include "Workspace.h"
#include "Workspacemenu.h"
#include "Util.h"

#include <string>
#include <algorithm>

#ifdef    HAVE_STDIO_H
#  include <stdio.h>
#endif // HAVE_STDIO_H

#ifdef    HAVE_STDLIB_H
#  include <stdlib.h>
#endif // HAVE_STDLIB_H

#ifdef    HAVE_STRING_H
#  include <string.h>
#endif // HAVE_STRING_H

#ifdef    HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif // HAVE_UNISTD_H

#ifdef    HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif // HAVE_SYS_PARAM_H

#ifndef   MAXPATHLEN
#define   MAXPATHLEN 255
#endif // MAXPATHLEN

#ifdef    HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif // HAVE_SYS_SELECT_H

#ifdef    HAVE_SIGNAL_H
#  include <signal.h>
#endif // HAVE_SIGNAL_H

#ifdef    HAVE_SYS_SIGNAL_H
#  include <sys/signal.h>
#endif // HAVE_SYS_SIGNAL_H

#ifdef    HAVE_SYS_STAT_H
#  include <sys/types.h>
#  include <sys/stat.h>
#endif // HAVE_SYS_STAT_H

#ifdef    TIME_WITH_SYS_TIME
#  include <sys/time.h>
#  include <time.h>
#else // !TIME_WITH_SYS_TIME
#  ifdef    HAVE_SYS_TIME_H
#    include <sys/time.h>
#  else // !HAVE_SYS_TIME_H
#    include <time.h>
#  endif // HAVE_SYS_TIME_H
#endif // TIME_WITH_SYS_TIME

#ifdef    HAVE_LIBGEN_H
#  include <libgen.h>
#endif // HAVE_LIBGEN_H

#ifndef   HAVE_BASENAME
static inline char *basename (char *s) {
  char *save = s;

  while (*s) if (*s++ == '/') save = s;

  return save;
}
#endif // HAVE_BASENAME


// X event scanner for enter/leave notifies - adapted from twm
typedef struct scanargs {
  Window w;
  Bool leave, inferior, enter;
} scanargs;

static Bool queueScanner(Display *, XEvent *e, char *args) {
  if ((e->type == LeaveNotify) &&
      (e->xcrossing.window == ((scanargs *) args)->w) &&
      (e->xcrossing.mode == NotifyNormal)) {
    ((scanargs *) args)->leave = True;
    ((scanargs *) args)->inferior = (e->xcrossing.detail == NotifyInferior);
  } else if ((e->type == EnterNotify) &&
             (e->xcrossing.mode == NotifyUngrab)) {
    ((scanargs *) args)->enter = True;
  }

  return False;
}

Openbox *openbox;


Openbox::Openbox(int m_argc, char **m_argv, char *dpy_name, char *rc)
  : BaseDisplay(m_argv[0], dpy_name) {
  grab();

  if (! XSupportsLocale())
    fprintf(stderr, "X server does not support locale\n");

  if (XSetLocaleModifiers("") == NULL)
    fprintf(stderr, "cannot set locale modifiers\n");

  ::openbox = this;
  argc = m_argc;
  argv = m_argv;
  if (rc == NULL) {
    char *homedir = getenv("HOME");

    rc_file = new char[strlen(homedir) + strlen("/.openbox/rc") + 1];
    sprintf(rc_file, "%s/.openbox", homedir);

    // try to make sure the ~/.openbox directory exists
    mkdir(rc_file, S_IREAD | S_IWRITE | S_IEXEC | S_IRGRP | S_IWGRP | S_IXGRP |
          S_IROTH | S_IWOTH | S_IXOTH);
    
    sprintf(rc_file, "%s/.openbox/rc", homedir);
  } else {
    rc_file = bstrdup(rc);
  }
  config.setFile(rc_file);

  no_focus = False;

  resource.menu_file = resource.style_file = NULL;
  resource.titlebar_layout = NULL;
  resource.auto_raise_delay.tv_sec = resource.auto_raise_delay.tv_usec = 0;

  focused_window = masked_window = NULL;
  masked = None;

  windowSearchList = new LinkedList<WindowSearch>;
  menuSearchList = new LinkedList<MenuSearch>;

#ifdef    SLIT
  slitSearchList = new LinkedList<SlitSearch>;
#endif // SLIT

  toolbarSearchList = new LinkedList<ToolbarSearch>;
  groupSearchList = new LinkedList<WindowSearch>;

  menuTimestamps = new LinkedList<MenuTimestamp>;

  load();

#ifdef    HAVE_GETPID
  openbox_pid = XInternAtom(getXDisplay(), "_BLACKBOX_PID", False);
#endif // HAVE_GETPID

  screenList = new LinkedList<BScreen>;
  for (int i = 0; i < getNumberOfScreens(); i++) {
    BScreen *screen = new BScreen(*this, i, config);

    if (! screen->isScreenManaged()) {
      delete screen;
      continue;
    }

    screenList->insert(screen);
  }

  if (! screenList->count()) {
    fprintf(stderr,
	    i18n->getMessage(openboxSet, openboxNoManagableScreens,
	       "Openbox::Openbox: no managable screens found, aborting.\n"));
    ::exit(3);
  }

  // save current settings and default values
  save();
  
  XSynchronize(getXDisplay(), False);
  XSync(getXDisplay(), False);

  reconfigure_wait = reread_menu_wait = False;

  timer = new BTimer(*this, *this);
  timer->setTimeout(0);
  timer->fireOnce(True);

  ungrab();
}


Openbox::~Openbox() {
  while (screenList->count())
    delete screenList->remove(0);

  while (menuTimestamps->count()) {
    MenuTimestamp *ts = menuTimestamps->remove(0);

    if (ts->filename)
      delete [] ts->filename;

    delete ts;
  }

  if (resource.menu_file)
    delete [] resource.menu_file;

  if (resource.style_file)
    delete [] resource.style_file;

  if (resource.titlebar_layout)
    delete [] resource.titlebar_layout;

  delete timer;

  delete screenList;
  delete menuTimestamps;

  delete windowSearchList;
  delete menuSearchList;
  delete toolbarSearchList;
  delete groupSearchList;

  delete [] rc_file;

#ifdef    SLIT
  delete slitSearchList;
#endif // SLIT
}


void Openbox::process_event(XEvent *e) {
  if ((masked == e->xany.window) && masked_window &&
      (e->type == MotionNotify)) {
    last_time = e->xmotion.time;
    masked_window->motionNotifyEvent(&e->xmotion);

    return;
  }

  switch (e->type) {
  case ButtonPress: {
    // strip the lock key modifiers
    e->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);

    last_time = e->xbutton.time;

    OpenboxWindow *win = (OpenboxWindow *) 0;
    Basemenu *menu = (Basemenu *) 0;

#ifdef    SLIT
    Slit *slit = (Slit *) 0;
#endif // SLIT

    Toolbar *tbar = (Toolbar *) 0;

    if ((win = searchWindow(e->xbutton.window))) {
      win->buttonPressEvent(&e->xbutton);

      if (e->xbutton.button == 1)
	win->installColormap(True);
    } else if ((menu = searchMenu(e->xbutton.window))) {
      menu->buttonPressEvent(&e->xbutton);

#ifdef    SLIT
    } else if ((slit = searchSlit(e->xbutton.window))) {
      slit->buttonPressEvent(&e->xbutton);
#endif // SLIT

    } else if ((tbar = searchToolbar(e->xbutton.window))) {
      tbar->buttonPressEvent(&e->xbutton);
    } else {
      LinkedListIterator<BScreen> it(screenList);
      BScreen *screen = it.current();
      for (; screen; it++, screen = it.current()) {
	if (e->xbutton.window == screen->getRootWindow()) {
	  if (e->xbutton.button == 1) {
            if (! screen->isRootColormapInstalled())
	      screen->getImageControl()->installRootColormap();

	    if (screen->getWorkspacemenu()->isVisible())
	      screen->getWorkspacemenu()->hide();

            if (screen->getRootmenu()->isVisible())
              screen->getRootmenu()->hide();
          } else if (e->xbutton.button == 2) {
	    int mx = e->xbutton.x_root -
	      (screen->getWorkspacemenu()->getWidth() / 2);
	    int my = e->xbutton.y_root -
	      (screen->getWorkspacemenu()->getTitleHeight() / 2);

	    if (mx < 0) mx = 0;
	    if (my < 0) my = 0;

	    if (mx + screen->getWorkspacemenu()->getWidth() >
		screen->size().w())
	      mx = screen->size().w() -
		screen->getWorkspacemenu()->getWidth() -
		screen->getBorderWidth();

	    if (my + screen->getWorkspacemenu()->getHeight() >
		screen->size().h())
	      my = screen->size().h() -
		screen->getWorkspacemenu()->getHeight() -
		screen->getBorderWidth();

	    screen->getWorkspacemenu()->move(mx, my);

	    if (! screen->getWorkspacemenu()->isVisible()) {
	      screen->getWorkspacemenu()->removeParent();
	      screen->getWorkspacemenu()->show();
	    }
	  } else if (e->xbutton.button == 3) {
	    int mx = e->xbutton.x_root -
	      (screen->getRootmenu()->getWidth() / 2);
	    int my = e->xbutton.y_root -
	      (screen->getRootmenu()->getTitleHeight() / 2);

	    if (mx < 0) mx = 0;
	    if (my < 0) my = 0;

	    if (mx + screen->getRootmenu()->getWidth() > screen->size().w())
	      mx = screen->size().w() -
		screen->getRootmenu()->getWidth() -
		screen->getBorderWidth();

	    if (my + screen->getRootmenu()->getHeight() > screen->size().h())
		my = screen->size().h() -
		  screen->getRootmenu()->getHeight() -
		  screen->getBorderWidth();

	    screen->getRootmenu()->move(mx, my);

	    if (! screen->getRootmenu()->isVisible()) {
	      checkMenu();
	      screen->getRootmenu()->show();
	    }
          } else if (e->xbutton.button == 4) {
            if ((screen->getCurrentWorkspaceID() + 1) >
                screen->getWorkspaceCount() - 1)
              screen->changeWorkspaceID(0);
            else
              screen->changeWorkspaceID(screen->getCurrentWorkspaceID() + 1);
          } else if (e->xbutton.button == 5) {
            if ((screen->getCurrentWorkspaceID() - 1) < 0)
              screen->changeWorkspaceID(screen->getWorkspaceCount() - 1);
            else
              screen->changeWorkspaceID(screen->getCurrentWorkspaceID() - 1);
          }
        }
      }
    }

    break;
  }

  case ButtonRelease: {
    // strip the lock key modifiers
    e->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);

    last_time = e->xbutton.time;

    OpenboxWindow *win = (OpenboxWindow *) 0;
    Basemenu *menu = (Basemenu *) 0;
    Toolbar *tbar = (Toolbar *) 0;

    if ((win = searchWindow(e->xbutton.window)))
      win->buttonReleaseEvent(&e->xbutton);
    else if ((menu = searchMenu(e->xbutton.window)))
      menu->buttonReleaseEvent(&e->xbutton);
    else if ((tbar = searchToolbar(e->xbutton.window)))
      tbar->buttonReleaseEvent(&e->xbutton);

    break;
  }

  case ConfigureRequest: {
    OpenboxWindow *win = (OpenboxWindow *) 0;

#ifdef    SLIT
    Slit *slit = (Slit *) 0;
#endif // SLIT

    if ((win = searchWindow(e->xconfigurerequest.window))) {
      win->configureRequestEvent(&e->xconfigurerequest);

#ifdef    SLIT
    } else if ((slit = searchSlit(e->xconfigurerequest.window))) {
      slit->configureRequestEvent(&e->xconfigurerequest);
#endif // SLIT

    } else {
      grab();

      if (validateWindow(e->xconfigurerequest.window)) {
	XWindowChanges xwc;

	xwc.x = e->xconfigurerequest.x;
	xwc.y = e->xconfigurerequest.y;
	xwc.width = e->xconfigurerequest.width;
	xwc.height = e->xconfigurerequest.height;
	xwc.border_width = e->xconfigurerequest.border_width;
	xwc.sibling = e->xconfigurerequest.above;
	xwc.stack_mode = e->xconfigurerequest.detail;

	XConfigureWindow(getXDisplay(), e->xconfigurerequest.window,
			 e->xconfigurerequest.value_mask, &xwc);
      }

      ungrab();
    }

    break;
  }

  case MapRequest: {
#ifdef    DEBUG
    fprintf(stderr,
	    i18n->getMessage(openboxSet, openboxMapRequest,
		 "Openbox::process_event(): MapRequest for 0x%lx\n"),
	    e->xmaprequest.window);
#endif // DEBUG

    OpenboxWindow *win = searchWindow(e->xmaprequest.window);

    if (! win)
      win = new OpenboxWindow(*this, e->xmaprequest.window);

    if ((win = searchWindow(e->xmaprequest.window)))
      win->mapRequestEvent(&e->xmaprequest);

    break;
  }

  case MapNotify: {
    OpenboxWindow *win = searchWindow(e->xmap.window);

    if (win)
      win->mapNotifyEvent(&e->xmap);

      break;
  }

  case UnmapNotify: {
    OpenboxWindow *win = (OpenboxWindow *) 0;

#ifdef    SLIT
    Slit *slit = (Slit *) 0;
#endif // SLIT

    if ((win = searchWindow(e->xunmap.window))) {
      win->unmapNotifyEvent(&e->xunmap);
      if (focused_window == win)
	focused_window = (OpenboxWindow *) 0;
#ifdef    SLIT
    } else if ((slit = searchSlit(e->xunmap.window))) {
      slit->removeClient(e->xunmap.window);
#endif // SLIT

    }

    break;
  }

  case DestroyNotify: {
    OpenboxWindow *win = (OpenboxWindow *) 0;

#ifdef    SLIT
    Slit *slit = (Slit *) 0;
#endif // SLIT

    if ((win = searchWindow(e->xdestroywindow.window))) {
      win->destroyNotifyEvent(&e->xdestroywindow);
      if (focused_window == win)
	focused_window = (OpenboxWindow *) 0;
#ifdef    SLIT
    } else if ((slit = searchSlit(e->xdestroywindow.window))) {
      slit->removeClient(e->xdestroywindow.window, False);
#endif // SLIT
    }

    break;
  }

  case MotionNotify: {
    // strip the lock key modifiers
    e->xbutton.state &= ~(NumLockMask | ScrollLockMask | LockMask);
    
    last_time = e->xmotion.time;

    OpenboxWindow *win = (OpenboxWindow *) 0;
    Basemenu *menu = (Basemenu *) 0;

    if ((win = searchWindow(e->xmotion.window)))
      win->motionNotifyEvent(&e->xmotion);
    else if ((menu = searchMenu(e->xmotion.window)))
      menu->motionNotifyEvent(&e->xmotion);

    break;
  }

  case PropertyNotify: {
    last_time = e->xproperty.time;

    if (e->xproperty.state != PropertyDelete) {
      OpenboxWindow *win = searchWindow(e->xproperty.window);

      if (win)
	win->propertyNotifyEvent(e->xproperty.atom);
    }

    break;
  }

  case EnterNotify: {
    last_time = e->xcrossing.time;

    BScreen *screen = (BScreen *) 0;
    OpenboxWindow *win = (OpenboxWindow *) 0;
    Basemenu *menu = (Basemenu *) 0;
    Toolbar *tbar = (Toolbar *) 0;

#ifdef    SLIT
    Slit *slit = (Slit *) 0;
#endif // SLIT

    if (e->xcrossing.mode == NotifyGrab) break;

    XEvent dummy;
    scanargs sa;
    sa.w = e->xcrossing.window;
    sa.enter = sa.leave = False;
    XCheckIfEvent(getXDisplay(), &dummy, queueScanner, (char *) &sa);

    if ((e->xcrossing.window == e->xcrossing.root) &&
	(screen = searchScreen(e->xcrossing.window))) {
      screen->getImageControl()->installRootColormap();
    } else if ((win = searchWindow(e->xcrossing.window))) {
      if (win->getScreen()->sloppyFocus() &&
	  (! win->isFocused()) && (! no_focus)) {
	grab();

        if (((! sa.leave) || sa.inferior) && win->isVisible() &&
            win->setInputFocus())
	  win->installColormap(True);

        ungrab();
      }
    } else if ((menu = searchMenu(e->xcrossing.window))) {
      menu->enterNotifyEvent(&e->xcrossing);
    } else if ((tbar = searchToolbar(e->xcrossing.window))) {
      tbar->enterNotifyEvent(&e->xcrossing);
#ifdef    SLIT
    } else if ((slit = searchSlit(e->xcrossing.window))) {
      slit->enterNotifyEvent(&e->xcrossing);
#endif // SLIT
    }
    break;
  }

  case LeaveNotify: {
    last_time = e->xcrossing.time;

    OpenboxWindow *win = (OpenboxWindow *) 0;
    Basemenu *menu = (Basemenu *) 0;
    Toolbar *tbar = (Toolbar *) 0;

#ifdef    SLIT
    Slit *slit = (Slit *) 0;
#endif // SLIT

    if ((menu = searchMenu(e->xcrossing.window)))
      menu->leaveNotifyEvent(&e->xcrossing);
    else if ((win = searchWindow(e->xcrossing.window)))
      win->installColormap(False);
    else if ((tbar = searchToolbar(e->xcrossing.window)))
      tbar->leaveNotifyEvent(&e->xcrossing);
#ifdef    SLIT
    else if ((slit = searchSlit(e->xcrossing.window)))
      slit->leaveNotifyEvent(&e->xcrossing);
#endif // SLIT

    break;
  }

  case Expose: {
    OpenboxWindow *win = (OpenboxWindow *) 0;
    Basemenu *menu = (Basemenu *) 0;
    Toolbar *tbar = (Toolbar *) 0;

    if ((win = searchWindow(e->xexpose.window)))
      win->exposeEvent(&e->xexpose);
    else if ((menu = searchMenu(e->xexpose.window)))
      menu->exposeEvent(&e->xexpose);
    else if ((tbar = searchToolbar(e->xexpose.window)))
      tbar->exposeEvent(&e->xexpose);

    break;
  }

  case KeyPress: {
    Toolbar *tbar = searchToolbar(e->xkey.window);

    if (tbar && tbar->isEditing())
      tbar->keyPressEvent(&e->xkey);

    break;
  }

  case ColormapNotify: {
    BScreen *screen = searchScreen(e->xcolormap.window);

    if (screen)
      screen->setRootColormapInstalled((e->xcolormap.state ==
					ColormapInstalled) ? True : False);

    break;
  }

  case FocusIn: {
    if (e->xfocus.mode == NotifyUngrab || e->xfocus.detail == NotifyPointer)
      break;

    OpenboxWindow *win = searchWindow(e->xfocus.window);
    if (win && ! win->isFocused())
      setFocusedWindow(win);

    break;
  }

  case FocusOut:
    break;

  case ClientMessage: {
    if (e->xclient.format == 32) {
      if (e->xclient.message_type == getWMChangeStateAtom()) {
        OpenboxWindow *win = searchWindow(e->xclient.window);
        if (! win || ! win->validateClient()) return;

        if (e->xclient.data.l[0] == IconicState)
	  win->iconify();
        if (e->xclient.data.l[0] == NormalState)
          win->deiconify();
      } else if (e->xclient.message_type == getOpenboxChangeWorkspaceAtom()) {
	BScreen *screen = searchScreen(e->xclient.window);

	if (screen && e->xclient.data.l[0] >= 0 &&
	    e->xclient.data.l[0] < screen->getWorkspaceCount())
	  screen->changeWorkspaceID(e->xclient.data.l[0]);
      } else if (e->xclient.message_type == getOpenboxChangeWindowFocusAtom()) {
	OpenboxWindow *win = searchWindow(e->xclient.window);

	if (win && win->isVisible() && win->setInputFocus())
          win->installColormap(True);
      } else if (e->xclient.message_type == getOpenboxCycleWindowFocusAtom()) {
        BScreen *screen = searchScreen(e->xclient.window);

        if (screen) {
          if (! e->xclient.data.l[0])
            screen->prevFocus();
          else
            screen->nextFocus();
	}
      } else if (e->xclient.message_type == getOpenboxChangeAttributesAtom()) {
	OpenboxWindow *win = searchWindow(e->xclient.window);

	if (win && win->validateClient()) {
	  OpenboxHints net;
	  net.flags = e->xclient.data.l[0];
	  net.attrib = e->xclient.data.l[1];
	  net.workspace = e->xclient.data.l[2];
	  net.stack = e->xclient.data.l[3];
	  net.decoration = e->xclient.data.l[4];

	  win->changeOpenboxHints(&net);
	}
      }
    }

    break;
  }


  default: {
#ifdef    SHAPE
    if (e->type == getShapeEventBase()) {
      XShapeEvent *shape_event = (XShapeEvent *) e;
      OpenboxWindow *win = (OpenboxWindow *) 0;

      if ((win = searchWindow(e->xany.window)) ||
	  (shape_event->kind != ShapeBounding))
	win->shapeEvent(shape_event);
    }
#endif // SHAPE

  }
  } // switch
}


Bool Openbox::handleSignal(int sig) {
  switch (sig) {
  case SIGHUP:
  case SIGUSR1:
    reconfigure();
    break;

  case SIGUSR2:
    rereadMenu();
    break;

  case SIGPIPE:
  case SIGSEGV:
  case SIGFPE:
  case SIGINT:
  case SIGTERM:
    shutdown();

  default:
    return False;
  }

  return True;
}


BScreen *Openbox::searchScreen(Window window) {
  LinkedListIterator<BScreen> it(screenList);

  for (BScreen *curr = it.current(); curr; it++, curr = it.current()) {
    if (curr->getRootWindow() == window) {
      return curr;
    }
  }

  return (BScreen *) 0;
}


OpenboxWindow *Openbox::searchWindow(Window window) {
  LinkedListIterator<WindowSearch> it(windowSearchList);

  for (WindowSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
      if (tmp->getWindow() == window) {
	return tmp->getData();
      }
  }

  return (OpenboxWindow *) 0;
}


OpenboxWindow *Openbox::searchGroup(Window window, OpenboxWindow *win) {
  OpenboxWindow *w = (OpenboxWindow *) 0;
  LinkedListIterator<WindowSearch> it(groupSearchList);

  for (WindowSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window) {
      w = tmp->getData();
      if (w->getClientWindow() != win->getClientWindow())
        return win;
    }
  }

  return (OpenboxWindow *) 0;
}


Basemenu *Openbox::searchMenu(Window window) {
  LinkedListIterator<MenuSearch> it(menuSearchList);

  for (MenuSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window)
      return tmp->getData();
  }

  return (Basemenu *) 0;
}


Toolbar *Openbox::searchToolbar(Window window) {
  LinkedListIterator<ToolbarSearch> it(toolbarSearchList);

  for (ToolbarSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window)
      return tmp->getData();
  }

  return (Toolbar *) 0;
}


#ifdef    SLIT
Slit *Openbox::searchSlit(Window window) {
  LinkedListIterator<SlitSearch> it(slitSearchList);

  for (SlitSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window)
      return tmp->getData();
  }

  return (Slit *) 0;
}
#endif // SLIT


void Openbox::saveWindowSearch(Window window, OpenboxWindow *data) {
  windowSearchList->insert(new WindowSearch(window, data));
}


void Openbox::saveGroupSearch(Window window, OpenboxWindow *data) {
  groupSearchList->insert(new WindowSearch(window, data));
}


void Openbox::saveMenuSearch(Window window, Basemenu *data) {
  menuSearchList->insert(new MenuSearch(window, data));
}


void Openbox::saveToolbarSearch(Window window, Toolbar *data) {
  toolbarSearchList->insert(new ToolbarSearch(window, data));
}


#ifdef    SLIT
void Openbox::saveSlitSearch(Window window, Slit *data) {
  slitSearchList->insert(new SlitSearch(window, data));
}
#endif // SLIT


void Openbox::removeWindowSearch(Window window) {
  LinkedListIterator<WindowSearch> it(windowSearchList);
  for (WindowSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window) {
      windowSearchList->remove(tmp);
      delete tmp;
      break;
    }
  }
}


void Openbox::removeGroupSearch(Window window) {
  LinkedListIterator<WindowSearch> it(groupSearchList);
  for (WindowSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window) {
      groupSearchList->remove(tmp);
      delete tmp;
      break;
    }
  }
}


void Openbox::removeMenuSearch(Window window) {
  LinkedListIterator<MenuSearch> it(menuSearchList);
  for (MenuSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window) {
      menuSearchList->remove(tmp);
      delete tmp;
      break;
    }
  }
}


void Openbox::removeToolbarSearch(Window window) {
  LinkedListIterator<ToolbarSearch> it(toolbarSearchList);
  for (ToolbarSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window) {
      toolbarSearchList->remove(tmp);
      delete tmp;
      break;
    }
  }
}


#ifdef    SLIT
void Openbox::removeSlitSearch(Window window) {
  LinkedListIterator<SlitSearch> it(slitSearchList);
  for (SlitSearch *tmp = it.current(); tmp; it++, tmp = it.current()) {
    if (tmp->getWindow() == window) {
      slitSearchList->remove(tmp);
      delete tmp;
      break;
    }
  }
}
#endif // SLIT


void Openbox::restart(const char *prog) {
  shutdown();

  if (prog) {
    execlp(prog, prog, NULL);
    perror(prog);
  }

  // fall back in case the above execlp doesn't work
  execvp(argv[0], argv);
  execvp(basename(argv[0]), argv);
}


void Openbox::shutdown() {
  BaseDisplay::shutdown();

  XSetInputFocus(getXDisplay(), PointerRoot, None, CurrentTime);

  LinkedListIterator<BScreen> it(screenList);
  for (BScreen *s = it.current(); s; it++, s = it.current())
    s->shutdown();

  XSync(getXDisplay(), False);
}


void Openbox::save() {
  config.setAutoSave(false);
  
  // save all values as they are so that the defaults will be written to the rc
  // file
  
  config.setValue("session.menuFile", getMenuFilename());
  config.setValue("session.colorsPerChannel",
                  resource.colors_per_channel);
  config.setValue("session.doubleClickInterval",
                  (long)resource.double_click_interval);
  config.setValue("session.autoRaiseDelay",
          ((resource.auto_raise_delay.tv_sec * 1000) +
           (resource.auto_raise_delay.tv_usec / 1000)));
  config.setValue("session.cacheLife", (long)resource.cache_life / 60000);
  config.setValue("session.cacheMax", (long)resource.cache_max);
  config.setValue("session.styleFile", resource.style_file);

  LinkedListIterator<BScreen> it(screenList);
  for (BScreen *s = it.current(); s != NULL; it++, s = it.current()) {
    s->save();
    s->getToolbar()->save();
#ifdef    SLIT
    s->getSlit()->save();
#endif // SLIT
  }

  config.setAutoSave(true);
  config.save();
}

void Openbox::load() {
  if (!config.load())
    config.create();

  std::string s;
  long l;
  
  if (resource.menu_file)
    delete [] resource.menu_file;
  if (config.getValue("session.menuFile", "Session.MenuFile", s))
    resource.menu_file = bstrdup(s.c_str());
  else
    resource.menu_file = bstrdup(DEFAULTMENU);

  if (config.getValue("session.colorsPerChannel", "Session.ColorsPerChannel",
                      l))
    resource.colors_per_channel = (l < 2 ? 2 : (l > 6 ? 6 : l)); // >= 2, <= 6
  else
    resource.colors_per_channel = 4;

  if (resource.style_file)
    delete [] resource.style_file;
  if (config.getValue("session.styleFile", "Session.StyleFile", s))
    resource.style_file = bstrdup(s.c_str());
  else
    resource.style_file = bstrdup(DEFAULTSTYLE);

  if (resource.titlebar_layout)
    delete [] resource.titlebar_layout;
  if (config.getValue("session.titlebarLayout", "Session.TitlebarLayout", s))
    resource.titlebar_layout = bstrdup(s.c_str());
  else
    resource.titlebar_layout = bstrdup("ILMC");

  if (config.getValue("session.doubleClickInterval",
                      "Session.DoubleClickInterval", l))
    resource.double_click_interval = l;
  else
    resource.double_click_interval = 250;

  if (!config.getValue("session.autoRaiseDelay", "Session.AutoRaiseDelay", l))
    resource.auto_raise_delay.tv_usec = l;
  else
    resource.auto_raise_delay.tv_usec = 400;
  resource.auto_raise_delay.tv_sec = resource.auto_raise_delay.tv_usec / 1000;
  resource.auto_raise_delay.tv_usec -=
    (resource.auto_raise_delay.tv_sec * 1000);
  resource.auto_raise_delay.tv_usec *= 1000;

  if (config.getValue("session.cacheLife", "Session.CacheLife", l))
    resource.cache_life = l;
  else
    resource.cache_life = 51;
  resource.cache_life *= 60000;

  if (config.getValue("session.cacheMax", "Session.CacheMax", l))
    resource.cache_max = l;
  else
    resource.cache_max = 200;
}


void Openbox::reconfigure() {
  reconfigure_wait = True;

  if (! timer->isTiming()) timer->start();
}


void Openbox::real_reconfigure() {
  grab();

  load();
  
  for (int i = 0, n = menuTimestamps->count(); i < n; i++) {
    MenuTimestamp *ts = menuTimestamps->remove(0);

    if (ts) {
      if (ts->filename)
	delete [] ts->filename;

      delete ts;
    }
  }

  LinkedListIterator<BScreen> it(screenList);
  for (BScreen *screen = it.current(); screen; it++, screen = it.current()) {
    screen->reconfigure();
  }

  ungrab();
}


void Openbox::checkMenu() {
  Bool reread = False;
  LinkedListIterator<MenuTimestamp> it(menuTimestamps);
  for (MenuTimestamp *tmp = it.current(); tmp && (! reread);
       it++, tmp = it.current()) {
    struct stat buf;

    if (! stat(tmp->filename, &buf)) {
      if (tmp->timestamp != buf.st_ctime)
        reread = True;
    } else {
      reread = True;
    }
  }

  if (reread) rereadMenu();
}


void Openbox::rereadMenu() {
  reread_menu_wait = True;

  if (! timer->isTiming()) timer->start();
}


void Openbox::real_rereadMenu() {
  for (int i = 0, n = menuTimestamps->count(); i < n; i++) {
    MenuTimestamp *ts = menuTimestamps->remove(0);

    if (ts) {
      if (ts->filename)
	delete [] ts->filename;

      delete ts;
    }
  }

  LinkedListIterator<BScreen> it(screenList);
  for (BScreen *screen = it.current(); screen; it++, screen = it.current())
    screen->rereadMenu();
}


void Openbox::setStyleFilename(const char *filename) {
  if (resource.style_file)
    delete [] resource.style_file;

  resource.style_file = bstrdup(filename);
  config.setValue("session.styleFile", resource.style_file);
}


void Openbox::setMenuFilename(const char *filename) {
  Bool found = False;

  LinkedListIterator<MenuTimestamp> it(menuTimestamps);
  for (MenuTimestamp *tmp = it.current(); tmp && (! found);
       it++, tmp = it.current()) {
    if (! strcmp(tmp->filename, filename)) found = True;
  }
  if (! found) {
    struct stat buf;

    if (! stat(filename, &buf)) {
      MenuTimestamp *ts = new MenuTimestamp;

      ts->filename = bstrdup(filename);
      ts->timestamp = buf.st_ctime;

      menuTimestamps->insert(ts);
    }
  }
}


void Openbox::timeout() {
  if (reconfigure_wait)
    real_reconfigure();

  if (reread_menu_wait)
    real_rereadMenu();

  reconfigure_wait = reread_menu_wait = False;
}


void Openbox::setFocusedWindow(OpenboxWindow *win) {
  BScreen *old_screen = (BScreen *) 0, *screen = (BScreen *) 0;
  OpenboxWindow *old_win = (OpenboxWindow *) 0;
  Toolbar *old_tbar = (Toolbar *) 0, *tbar = (Toolbar *) 0;
  Workspace *old_wkspc = (Workspace *) 0, *wkspc = (Workspace *) 0;

  if (focused_window) {
    old_win = focused_window;
    old_screen = old_win->getScreen();
    old_tbar = old_screen->getToolbar();
    old_wkspc = old_screen->getWorkspace(old_win->getWorkspaceNumber());

    old_win->setFocusFlag(False);
    old_wkspc->getMenu()->setItemSelected(old_win->getWindowNumber(), False);
  }

  if (win && ! win->isIconic()) {
    screen = win->getScreen();
    tbar = screen->getToolbar();
    wkspc = screen->getWorkspace(win->getWorkspaceNumber());

    focused_window = win;

    win->setFocusFlag(True);
    wkspc->getMenu()->setItemSelected(win->getWindowNumber(), True);
  } else {
    focused_window = (OpenboxWindow *) 0;
  }

  if (tbar)
    tbar->redrawWindowLabel(True);
  if (screen)
    screen->updateNetizenWindowFocus();

  if (old_tbar && old_tbar != tbar)
    old_tbar->redrawWindowLabel(True);
  if (old_screen && old_screen != screen)
    old_screen->updateNetizenWindowFocus();
}
