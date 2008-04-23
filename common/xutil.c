/*
 * common/xutil.c - X-related useful functions
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/* strndup(), asprintf() */
#define _GNU_SOURCE

#include <stdio.h>

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

#include "common/util.h"
#include "common/xutil.h"

/** Get the string value of an atom.
 * \param conn X connection
 * \param w window
 * \param atom the atom
 * \param text buffer to fill
 * \param textlen buffer lenght
 * \return true on sucess, falsse on failure
 */
bool
xutil_gettextprop(xcb_connection_t *conn, xcb_window_t w, xcb_atom_t atom,
                  char *text, ssize_t textlen)
{
    xcb_get_property_cookie_t prop_c;
    xcb_get_property_reply_t *prop_r;
    void *prop_val;

    prop_c = xcb_get_property_unchecked(conn, false,
                                        w, atom,
                                        XCB_GET_PROPERTY_TYPE_ANY,
                                        0L, 1000000L);

    if(!text || !textlen)
        return false;

    prop_r = xcb_get_property_reply(conn, prop_c, NULL);

    if(!prop_r || !prop_r->value_len || prop_r->format != 8)
    {
        if(prop_r)
            p_delete(&prop_r);
        return false;
    }

    prop_val = xcb_get_property_value(prop_r);

    /* Check whether the returned property value is just an ascii
     * string or utf8 string.  At the moment it doesn't handle
     * COMPOUND_TEXT and multibyte but it's not needed...  */
    if(prop_r->type == STRING ||
       prop_r->type == xutil_intern_atom(conn, "UTF8_STRING"))
    {
        if(prop_r->value_len < textlen - 1)
        {
            /* use memcpy() because prop_val may not be \0 terminated */
            memcpy(text, prop_val, prop_r->value_len);
            text[prop_r->value_len] = '\0';
        }
        else
        {
            memcpy(text, prop_val, textlen - 2);
            text[textlen - 1] = '\0';
        }
    }

    p_delete(&prop_r);

    return true;
}

void
xutil_getlockmask(xcb_connection_t *conn, xcb_key_symbols_t *keysyms,
                  unsigned int *numlockmask, unsigned int *shiftlockmask,
                  unsigned int *capslockmask)
{
    xcb_get_modifier_mapping_reply_t *modmap_r;
    xcb_keycode_t *modmap, kc;
    unsigned int mask;
    int i, j;

    modmap_r = xcb_get_modifier_mapping_reply(conn,
                                              xcb_get_modifier_mapping_unchecked(conn),
                                              NULL);

    modmap = xcb_get_modifier_mapping_keycodes(modmap_r);

    for(i = 0; i < 8; i++)
        for(j = 0; j < modmap_r->keycodes_per_modifier; j++)
        {
            kc = modmap[i * modmap_r->keycodes_per_modifier + j];
            mask = (1 << i);

            if(numlockmask != NULL &&
               kc == xcb_key_symbols_get_keycode(keysyms, XK_Num_Lock))
                *numlockmask = mask;
            else if(shiftlockmask != NULL &&
                    kc == xcb_key_symbols_get_keycode(keysyms, XK_Shift_Lock))
                *shiftlockmask = mask;
            else if(capslockmask != NULL &&
                    kc == xcb_key_symbols_get_keycode(keysyms, XK_Caps_Lock))
                *capslockmask = mask;
        }

    p_delete(&modmap_r);
}

/** Equivalent to 'XGetTransientForHint' which is actually a
 * 'XGetWindowProperty' which gets the WM_TRANSIENT_FOR property of
 * the specified window
 * \param c X connection
 * \param win get the property from this window
 * \param prop_win returns the WM_TRANSIENT_FOR property of win
 * \return return true if successfull
 */
bool
xutil_get_transient_for_hint(xcb_connection_t *c, xcb_window_t win,
                             xcb_window_t *prop_win)
{
    xcb_get_property_reply_t *t_hint_r;

    /* Use checked because the error handler should not take care of
     * this error as we only return a boolean */
    t_hint_r = xcb_get_property_reply(c,
                                      xcb_get_property(c, false, win,
                                                       WM_TRANSIENT_FOR,
                                                       WINDOW, 0, 1),
                                      NULL);

    if(!t_hint_r || t_hint_r->type != WINDOW || t_hint_r->format != 32 ||
       t_hint_r->length == 0)
    {
        *prop_win = XCB_NONE;
        if(t_hint_r)
            p_delete(&t_hint_r);

        return false;
    }

    *prop_win = *((xcb_window_t *) xcb_get_property_value(t_hint_r));
    p_delete(&t_hint_r);

    return true;
}

/** Get an internal atom.
 * \param c X connection
 * \param property atom name
 * \return an brand new xcb_atom_t
 */
xcb_atom_t
xutil_intern_atom(xcb_connection_t *c, const char *property)
{
    xcb_atom_t atom = 0;
    xcb_intern_atom_reply_t *r_atom;

    if((r_atom = xcb_intern_atom_reply(c,
	                               xcb_intern_atom_unchecked(c,
                                                                 false,
                                                                 a_strlen(property),
                                                                 property),
                                       NULL)))
    {
        atom = r_atom->atom;
        p_delete(&r_atom);
    }

    return atom;
}

class_hint_t *
xutil_get_class_hint(xcb_connection_t *conn, xcb_window_t win)
{
    xcb_get_property_reply_t *class_hint_r;
    xcb_get_property_cookie_t class_hint_c;
    char *data;
    int len_name, len_class;
    class_hint_t *ch;

    class_hint_c = xcb_get_property_unchecked(conn, false, win, WM_CLASS,
                                              STRING, 0L, 2048L);
    ch = p_new(class_hint_t, 1);

    class_hint_r = xcb_get_property_reply(conn, class_hint_c, NULL);

    if(!class_hint_r || class_hint_r->type != STRING ||
       class_hint_r->format != 8)
    {
        if(class_hint_r)
            p_delete(&class_hint_r);

	return NULL;
    }

    data = xcb_get_property_value(class_hint_r);

    len_name = a_strlen(data);
    len_class = a_strlen(data + len_name + 1);

    ch->res_name = strndup(data, len_name);
    ch->res_class = strndup(data + len_name + 1, len_class);

    p_delete(&class_hint_r);

    return ch;
}

/* Number of different errors */
#define ERRORS_NBR 256

/* Number of different events */
#define EVENTS_NBR 126

void
xutil_set_error_handler_catch_all(xcb_event_handlers_t *evenths,
                                  xcb_generic_error_handler_t handler,
                                  void *data)
{
    int err_num;
    for(err_num = 0; err_num < ERRORS_NBR; err_num++)
	xcb_set_error_handler(evenths, err_num, handler, data);
}

const char *
xutil_error_label[] =
{
    "Success",
    "BadRequest",
    "BadValue",
    "BadWindow",
    "BadPixmap",
    "BadAtom",
    "BadCursor",
    "BadFont",
    "BadMatch",
    "BadDrawable",
    "BadAccess",
    "BadAlloc",
    "BadColor",
    "BadGC",
    "BadIDChoice",
    "BadName",
    "BadLength",
    "BadImplementation",
};

const char *
xutil_request_label[] =
{
    "None",
    "CreateWindow",
    "ChangeWindowAttributes",
    "GetWindowAttributes",
    "DestroyWindow",
    "DestroySubwindows",
    "ChangeSaveSet",
    "ReparentWindow",
    "MapWindow",
    "MapSubwindows",
    "UnmapWindow",
    "UnmapSubwindows",
    "ConfigureWindow",
    "CirculateWindow",
    "GetGeometry",
    "QueryTree",
    "InternAtom",
    "GetAtomName",
    "ChangeProperty",
    "DeleteProperty",
    "GetProperty",
    "ListProperties",
    "SetSelectionOwner",
    "GetSelectionOwner",
    "ConvertSelection",
    "SendEvent",
    "GrabPointer",
    "UngrabPointer",
    "GrabButton",
    "UngrabButton",
    "ChangeActivePointerGrab",
    "GrabKeyboard",
    "UngrabKeyboard",
    "GrabKey",
    "UngrabKey",
    "AllowEvents",
    "GrabServer",
    "UngrabServer",
    "QueryPointer",
    "GetMotionEvents",
    "TranslateCoords",
    "WarpPointer",
    "SetInputFocus",
    "GetInputFocus",
    "QueryKeymap",
    "OpenFont",
    "CloseFont",
    "QueryFont",
    "QueryTextExtents",
    "ListFonts",
    "ListFontsWithInfo",
    "SetFontPath",
    "GetFontPath",
    "CreatePixmap",
    "FreePixmap",
    "CreateGC",
    "ChangeGC",
    "CopyGC",
    "SetDashes",
    "SetClipRectangles",
    "FreeGC",
    "ClearArea",
    "CopyArea",
    "CopyPlane",
    "PolyPoint",
    "PolyLine",
    "PolySegment",
    "PolyRectangle",
    "PolyArc",
    "FillPoly",
    "PolyFillRectangle",
    "PolyFillArc",
    "PutImage",
    "GetImage",
    "PolyText",
    "PolyText",
    "ImageText",
    "ImageText",
    "CreateColormap",
    "FreeColormap",
    "CopyColormapAndFree",
    "InstallColormap",
    "UninstallColormap",
    "ListInstalledColormaps",
    "AllocColor",
    "AllocNamedColor",
    "AllocColorCells",
    "AllocColorPlanes",
    "FreeColors",
    "StoreColors",
    "StoreNamedColor",
    "QueryColors",
    "LookupColor",
    "CreateCursor",
    "CreateGlyphCursor",
    "FreeCursor",
    "RecolorCursor",
    "QueryBestSize",
    "QueryExtension",
    "ListExtensions",
    "ChangeKeyboardMapping",
    "GetKeyboardMapping",
    "ChangeKeyboardControl",
    "GetKeyboardControl",
    "Bell",
    "ChangePointerControl",
    "GetPointerControl",
    "SetScreenSaver",
    "GetScreenSaver",
    "ChangeHosts",
    "ListHosts",
    "SetAccessControl",
    "SetCloseDownMode",
    "KillClient",
    "RotateProperties",
    "ForceScreenSaver",
    "SetPointerMapping",
    "GetPointerMapping",
    "SetModifierMapping",
    "GetModifierMapping",
    "major 120",
    "major 121",
    "major 122",
    "major 123",
    "major 124",
    "major 125",
    "major 126",
    "NoOperation",
};

xutil_error_t *
xutil_get_error(const xcb_generic_error_t *e)
{
    if(e->response_type != 0)
        /* This is not an error, this _should_ not happen */
        return NULL;

    xutil_error_t *err = p_new(xutil_error_t, 1);

    /*
     * Get the request code,  taken from 'xcb-util/wm'. I can't figure
     * out  how it  works BTW,  seems to  get a  byte in  'pad' member
     * (second byte in second element of the array)
     */
    err->request_code = *((uint8_t *) e + 10);

    /* Extensions  generally provide  their  own requests  so we  just
     * store the request code */
    if(err->request_code >= (sizeof(xutil_request_label) / sizeof(char *)))
        asprintf(&err->request_label, "%d", err->request_code);
    else
        err->request_label = a_strdup(xutil_request_label[err->request_code]);

    /* Extensions may also define their  own errors, so just store the
     * error_code */
    if(e->error_code >= (sizeof(xutil_error_label) / sizeof(char *)))
        asprintf(&err->error_label, "%d", e->error_code);
    else
        err->error_label = a_strdup(xutil_error_label[e->error_code]);

    return err;
}

void
xutil_delete_error(xutil_error_t *err)
{
    p_delete(&err->error_label);
    p_delete(&err->request_label);
    p_delete(&err);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
