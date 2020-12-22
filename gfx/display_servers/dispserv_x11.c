/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *  Copyright (C) 2016-2019 - Brad Parker
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

/* We are targeting XRandR 1.2 here. */
#include <math.h>

#include <compat/strl.h>
#include <string/stdstring.h>

#include <sys/types.h>
#include <unistd.h>
#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#ifdef __cplusplus
#include <cstring> // required for strcpy
#endif

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/randr.h>
#include <X11/extensions/Xrender.h>
#endif

#include "../video_display_server.h"
#include "../common/x11_common.h"
#include "../../retroarch.h"
#include "../video_crt_switch.h" /* needed to set aspect for low res in linux */
#define LIBSWR "libswitchres.so"

#include "switchres_wrapper.h"

typedef struct
{
#ifdef HAVE_XRANDR
   XRRModeInfo crt_rrmode;
#endif
   int crt_name_id;
   int monitor_index;
   unsigned opacity;
   char crt_name[16];
   char new_mode[256];
   char old_mode[256];
   char orig_output[256];
   bool using_global_dpy;
   bool crt_en;
   bool decorations;
} dispserv_x11_t;



#ifdef HAVE_XRANDR
static Display* x11_display_server_open_display(dispserv_x11_t *dispserv)
{
   Display *dpy                           = g_x11_dpy;
   if (!dispserv)
      return NULL;
   dispserv->using_global_dpy             = (dpy != NULL);

   /* SDL might use X11 but doesn't use g_x11_dpy, so open it manually */
   if (!dpy)
      dpy                                 = XOpenDisplay(0);

   return dpy;
}

static void x11_display_server_close_display(dispserv_x11_t *dispserv,
      Display *dpy)
{
   if (!dpy || !dispserv || dispserv->using_global_dpy || dpy == g_x11_dpy)
      return;

   XCloseDisplay(dpy);
}
#endif

static bool x11_display_server_set_resolution(void *data,
      unsigned width, unsigned height, int int_hz, float hz,
      int center, int monitor_index, int xoffset, int padjust)
{
   unsigned char interlace = 0, ret;
   LIBTYPE dlp = OPENLIB(LIBSWR);

   if (!dlp) {
      printf("Loading %s failed.\n", LIBSWR);
      printf("Error: %s\n", LIBERROR());
      exit(EXIT_FAILURE);
   }

   printf("Loading %s succeded.\n", LIBSWR);

   LIBERROR();
   srAPI* SRobj =  (srAPI*)LIBFUNC(dlp, "srlib");
   if ((err_msg = LIBERROR()) != NULL) {
      printf("Failed to load srAPI: %s\n", err_msg);
      CLOSELIB(dlp);
      exit(EXIT_FAILURE);
   }

	printf("Init a new switchres_manager object:\n");
	SRobj->init();

   printf("Orignial resolution expected: %dx%d@%f-%d\n", width, height, hz, interlace);

   ret = SRobj->sr_add_mode(width, height, hz, interlace, &srm);
	if(!ret) 
	{
		printf("ERROR: couldn't add the required mode. Exiting!\n");
		SRobj->deinit();
		exit(1);
	}
	printf("Got resolution: %dx%d%c@%f\n", srm.width, srm.height, srm.interlace, srm.refresh);

   ret = SRobj->sr_switch_to_mode(srm.width, srm.height, rr, srm.interlace, &srm);
	if(!ret) 
	{
		printf("ERROR: couldn't switch to the required mode. Exiting!\n");
		SRobj->deinit();
		exit(1);
	}

   SRobj->deinit();

   CLOSELIB(dlp);

}


#ifdef HAVE_XRANDR


static void x11_display_server_set_screen_orientation(void *data,
      enum rotation rotation)
{
   int i, j;
   XRRScreenResources *screen     = NULL;
   /* switched to using XOpenDisplay() due to deinit order issue with g_x11_dpy when restoring original rotation on exit */
   Display                   *dpy = XOpenDisplay(0);
   XRRScreenConfiguration *config = XRRGetScreenInfo(dpy, DefaultRootWindow(dpy));
   double dpi = (25.4 * DisplayHeight(dpy, DefaultScreen(dpy))) / DisplayHeightMM(dpy, DefaultScreen(dpy));

   XGrabServer(dpy);

   screen = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));

   for (i = 0; i < screen->noutput; i++)
   {
      XRROutputInfo *info = XRRGetOutputInfo(dpy, screen, screen->outputs[i]);

      if (info->connection != RR_Connected)
      {
         XRRFreeOutputInfo(info);
         continue;
      }

      for (j = 0; j < info->ncrtc; j++)
      {
         XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, screen, screen->crtcs[j]);
         Rotation new_rotation = RR_Rotate_0;

         if (crtc->width == 0 || crtc->height == 0)
         {
            XRRFreeCrtcInfo(crtc);
            continue;
         }

         switch (rotation)
         {
            case ORIENTATION_NORMAL:
            default:
               if (crtc->rotations & RR_Rotate_0)
                  new_rotation = RR_Rotate_0;
               break;
            case ORIENTATION_VERTICAL:
               if (crtc->rotations & RR_Rotate_270)
                  new_rotation = RR_Rotate_270;
               break;
            case ORIENTATION_FLIPPED:
               if (crtc->rotations & RR_Rotate_180)
                  new_rotation = RR_Rotate_180;
               break;
            case ORIENTATION_FLIPPED_ROTATED:
               if (crtc->rotations & RR_Rotate_90)
                  new_rotation = RR_Rotate_90;
               break;
         }

         XRRSetCrtcConfig(dpy, screen, screen->crtcs[j], CurrentTime,
               0, 0, None, RR_Rotate_0, NULL, 0);

         if ((crtc->rotation & RR_Rotate_0 || crtc->rotation & RR_Rotate_180) && (rotation == ORIENTATION_VERTICAL || rotation == ORIENTATION_FLIPPED_ROTATED))
         {
            unsigned width = crtc->width;
            crtc->width = crtc->height;
            crtc->height = width;
         }
         else if ((crtc->rotation & RR_Rotate_90 || crtc->rotation & RR_Rotate_270) && (rotation == ORIENTATION_NORMAL || rotation == ORIENTATION_FLIPPED))
         {
            unsigned width = crtc->width;
            crtc->width    = crtc->height;
            crtc->height   = width;
         }

         crtc->rotation = new_rotation;

         XRRSetScreenSize(dpy, DefaultRootWindow(dpy), crtc->width, crtc->height, (25.4 * crtc->width) / dpi, (25.4 * crtc->height) / dpi);

         XRRSetCrtcConfig(dpy, screen, screen->crtcs[j], CurrentTime, crtc->x, crtc->y, crtc->mode, crtc->rotation, crtc->outputs, crtc->noutput);

         XRRFreeCrtcInfo(crtc);
      }

      XRRFreeOutputInfo(info);
   }

   XRRFreeScreenResources(screen);

   XUngrabServer(dpy);
   XSync(dpy, False);
   XRRFreeScreenConfigInfo(config);
   XCloseDisplay(dpy);
}

static enum rotation x11_display_server_get_screen_orientation(void *data)
{
   int i, j;
   XRRScreenConfiguration *config = NULL;
   enum rotation     rotation     = ORIENTATION_NORMAL;
   dispserv_x11_t *dispserv       = (dispserv_x11_t*)data;
   Display               *dpy     = x11_display_server_open_display(dispserv);
   XRRScreenResources *screen     = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));
   if (!screen)
     return ORIENTATION_NORMAL;
   config                         = XRRGetScreenInfo(dpy, DefaultRootWindow(dpy));

   for (i = 0; i < screen->noutput; i++)
   {
      XRROutputInfo *info = XRRGetOutputInfo(dpy, screen, screen->outputs[i]);

      if (info->connection != RR_Connected)
      {
         XRRFreeOutputInfo(info);
         continue;
      }

      for (j = 0; j < info->ncrtc; j++)
      {
         XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, screen, screen->crtcs[j]);

         if (crtc->width == 0 || crtc->height == 0)
         {
            XRRFreeCrtcInfo(crtc);
            continue;
         }

         switch (crtc->rotation)
         {
            case RR_Rotate_0:
            default:
               rotation = ORIENTATION_NORMAL;
               break;
            case RR_Rotate_270:
               rotation = ORIENTATION_VERTICAL;
               break;
            case RR_Rotate_180:
               rotation = ORIENTATION_FLIPPED;
               break;
            case RR_Rotate_90:
               rotation = ORIENTATION_FLIPPED_ROTATED;
               break;
         }

         XRRFreeCrtcInfo(crtc);
      }

      XRRFreeOutputInfo(info);
   }

   XRRFreeScreenResources(screen);
   XRRFreeScreenConfigInfo(config);

   x11_display_server_close_display(dispserv, dpy);

   return rotation;
}
#endif

static void* x11_display_server_init(void)
{
   dispserv_x11_t *dispserv = (dispserv_x11_t*)calloc(1, sizeof(*dispserv));

   if (!dispserv)
      return NULL;

   return dispserv;
}

static void x11_display_server_destroy(void *data)
{
#ifdef HAVE_XRANDR
   int m, j, i;
#endif
   dispserv_x11_t *dispserv = (dispserv_x11_t*)data;

   if (!dispserv)
      return;

#ifdef HAVE_XRANDR
   if (dispserv->crt_en)
   {
      XRRModeInfo *swoldmode   = NULL;
      XRRModeInfo *swdeskmode  = NULL;
      XRRScreenResources 
         *resources            = NULL;
      XRRScreenResources  *res = NULL;
      Display *dpy             = XOpenDisplay(0);
      int screen               = DefaultScreen(dpy);
      Window window            = RootWindow(dpy, screen);
      bool crt_exists          = false;
      char dmode[25]           = {0};

      strlcpy(dmode, "d_mo", sizeof(dmode));

      dispserv->crt_rrmode.name          = dmode;
      dispserv->crt_rrmode.nameLength    = strlen(dispserv->crt_name);
      dispserv->crt_rrmode.dotClock      = 13849698;
      dispserv->crt_rrmode.width         = 700;
      dispserv->crt_rrmode.hSyncStart    = 742;
      dispserv->crt_rrmode.hSyncEnd      = 801;
      dispserv->crt_rrmode.hTotal        = 867;
      dispserv->crt_rrmode.height        = 480;
      dispserv->crt_rrmode.vSyncStart    = 490;
      dispserv->crt_rrmode.vSyncEnd      = 496;
      dispserv->crt_rrmode.vTotal        = 533;
      dispserv->crt_rrmode.modeFlags     = 26;
      /* 10 for -hsync -vsync. ?? for -hsync -vsync interlaced */
      dispserv->crt_rrmode.hSkew         = 0;

      res                      = XRRGetScreenResources(dpy, window);
      resources                = XRRGetScreenResourcesCurrent(dpy, window);
      XSync(dpy, False);

      resources = XRRGetScreenResourcesCurrent(dpy, window);

      for (m = 0; m < resources->nmode; m++)
      {
         if (string_is_equal(resources->modes[m].name, dmode))
         {
            crt_exists = true;
            break;
         }
      }

      XRRFreeScreenResources(resources);


      if (!crt_exists)
         XRRCreateMode(dpy, window, &dispserv->crt_rrmode); 

      resources = XRRGetScreenResourcesCurrent(dpy, window);

      for (m = 0; m < resources->nmode; m++)
      {
         if (string_is_equal(resources->modes[m].name, dmode))
         {
            swdeskmode = &resources->modes[m];
            break;
         }
      }

      if (dispserv->monitor_index == 20)
      {
         for (i = 0; i < res->noutput; i++)
         {
            XRROutputInfo *outputs = 
               XRRGetOutputInfo(dpy, res, res->outputs[i]);

            if (outputs->connection == RR_Connected)
            {
               XRRCrtcInfo *crtc;

               XRRAddOutputMode(dpy, res->outputs[i], swdeskmode->id);
               XSync(dpy, False);
               strlcpy(dispserv->orig_output, outputs->name,
                     sizeof(dispserv->orig_output));
               crtc         = XRRGetCrtcInfo(dpy, resources, outputs->crtc);
               crtc->mode   = swdeskmode->id;
               crtc->width  = swdeskmode->width;
               crtc->height = swdeskmode->height;
               XRRSetCrtcConfig(dpy, res,res->crtcs[i],
                     CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0);
               XSync(dpy, False);
               XRRSetCrtcConfig(dpy, res, res->crtcs[i], CurrentTime,
                     crtc->x, crtc->y, crtc->mode, crtc->rotation,
                     crtc->outputs, crtc->noutput);
               XSync(dpy, False);


               XRRFreeCrtcInfo(crtc);
            }  
            XRRFreeOutputInfo(outputs);
         }
      }
      else
      {
         XRROutputInfo *outputs = XRRGetOutputInfo(dpy, res,
               res->outputs[dispserv->monitor_index]);

         if (outputs->connection == RR_Connected)
         {
            XRRCrtcInfo *crtc = NULL;
            XRRAddOutputMode(dpy,
                  res->outputs[dispserv->monitor_index], swdeskmode->id);
            XSync(dpy, False);
            strlcpy(dispserv->orig_output, outputs->name,
                  sizeof(dispserv->orig_output));
            crtc         = XRRGetCrtcInfo(dpy, resources, outputs->crtc);
            crtc->mode   = swdeskmode->id;
            crtc->width  = swdeskmode->width;
            crtc->height = swdeskmode->height;
            XRRSetCrtcConfig(dpy, res,
                  res->crtcs[dispserv->monitor_index],
                  CurrentTime, 0, 0, None, RR_Rotate_0, NULL, 0);
            XSync(dpy, False);
            XRRSetCrtcConfig(dpy, res,
                  res->crtcs[dispserv->monitor_index],
                  CurrentTime, crtc->x, crtc->y,
                  crtc->mode, crtc->rotation,
                  crtc->outputs, crtc->noutput);
            XSync(dpy, False);

            XRRFreeCrtcInfo(crtc);
         }
         XRRFreeOutputInfo(outputs);
      }

      for (m = 0; m < resources->nmode; m++)
      {
         for (j = 0; j < res->noutput; j++)
         {
            for (i = 1 ; i <= dispserv->crt_name_id; i++ )
            {
               XRROutputInfo *outputs = XRRGetOutputInfo(dpy, res, res->outputs[j]);
               if (outputs->connection == RR_Connected)
               {
                  snprintf(dispserv->old_mode, sizeof(dispserv->old_mode),
                        "CRT%d", i);
                  if (string_is_equal(resources->modes[m].name,
                           dispserv->old_mode))
                  {
                     swoldmode = &resources->modes[m];
                     XRRDeleteOutputMode(dpy, res->outputs[j], swoldmode->id);
                     XRRDestroyMode(dpy, swoldmode->id);
                     XSync(dpy, False);
                  }
               }
            }
         }
      }
      XRRFreeScreenResources(resources);
      XRRFreeScreenResources(res);
      XCloseDisplay(dpy);
   }

#endif

   if (dispserv)
      free(dispserv);
}

static bool x11_display_server_set_window_opacity(void *data, unsigned opacity)
{
   dispserv_x11_t *serv = (dispserv_x11_t*)data;
   Atom net_wm_opacity  = XInternAtom(g_x11_dpy, "_NET_WM_WINDOW_OPACITY", False);
   Atom cardinal        = XInternAtom(g_x11_dpy, "CARDINAL", False);

   serv->opacity        = opacity;

   opacity              = opacity * ((unsigned)-1 / 100.0);

   if (opacity == (unsigned)-1)
      XDeleteProperty(g_x11_dpy, g_x11_win, net_wm_opacity);
   else
      XChangeProperty(g_x11_dpy, g_x11_win, net_wm_opacity, cardinal,
            32, PropModeReplace, (const unsigned char*)&opacity, 1);

   return true;
}

static bool x11_display_server_set_window_decorations(void *data, bool on)
{
   dispserv_x11_t *serv = (dispserv_x11_t*)data;

   if (serv)
      serv->decorations = on;

   /* menu_setting performs a reinit instead to properly apply
    * decoration changes */

   return true;
}


const char *x11_display_server_get_output_options(void *data)
{
#ifdef HAVE_XRANDR
   Display *dpy;
   XRRScreenResources *res;
   XRROutputInfo *info;
   Window root;
   int i;
   static char s[PATH_MAX_LENGTH];

   if (!(dpy = XOpenDisplay(0)))
      return NULL;

   root = RootWindow(dpy, DefaultScreen(dpy));

   if (!(res = XRRGetScreenResources(dpy, root)))
      return NULL;

   for (i = 0; i < res->noutput; i++)
   {
      if (!(info = XRRGetOutputInfo(dpy, res, res->outputs[i])))
         return NULL;

      strlcat(s, info->name, sizeof(s));
      if ((i+1) < res->noutput)
         strlcat(s, "|", sizeof(s));
   }

   return s;
#else
   /* TODO/FIXME - hardcoded for now; list should be built up dynamically later */
   return "HDMI-0|HDMI-1|HDMI-2|HDMI-3|DVI-0|DVI-1|DVI-2|DVI-3|VGA-0|VGA-1|VGA-2|VGA-3|Config";
#endif
}

static uint32_t x11_display_server_get_flags(void *data)
{
   uint32_t             flags   = 0;

#ifdef HAVE_XRANDR
   BIT32_SET(flags, DISPSERV_CTX_CRT_SWITCHRES);
#endif

   return flags;
}

const video_display_server_t dispserv_x11 = {
   x11_display_server_init,
   x11_display_server_destroy,
   x11_display_server_set_window_opacity,
   NULL, /* set_window_progress */
   x11_display_server_set_window_decorations,
#ifdef HAVE_XRANDR
   x11_display_server_set_resolution,
#else
   NULL, /* set_resolution */
#endif
   NULL, /* get_resolution_list */
   x11_display_server_get_output_options,
#ifdef HAVE_XRANDR
   x11_display_server_set_screen_orientation,
   x11_display_server_get_screen_orientation,
#else
   NULL, /* set_screen_orientation */
   NULL, /* get_screen_orientation */
#endif
   x11_display_server_get_flags,
   "x11"
};
