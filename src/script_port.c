/*
 * Copyright 2012  Samsung Electronics Co., Ltd
 *
 * Licensed under the Flora License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.tizenopensource.org/license
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>

#include <Evas.h>
#include <Edje.h>
#include <Eina.h>
#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Eet.h>
#include <Ecore_X.h>

#include <dlog.h>
#include <debug.h>
#include <vconf.h>

#include "script_port.h"

#define TEXT_CLASS	"tizen"

extern void evas_common_font_flush(void);
extern int evas_common_font_cache_get(void);
extern void evas_common_font_cache_set(int size);

struct info {
	char *file;
	char *group;
	char *category;
	int w;
	int h;

	Evas *e;

	Eina_List *obj_list;
};

struct child {
	Evas_Object *obj;
	char *part;
};

struct obj_info {
	char *id;
	Eina_List *children;
};

struct {
	Ecore_Event_Handler *property_handler;
	char *font;
	int size;
} s_info = {
	.property_handler = NULL,
	.font = NULL,
	.size = -100,
};

/*!
 * \NOTE
 * Reservce this for future use
static inline void common_cache_flush(void *evas)
{
	int file_cache;
	int collection_cache;
	int image_cache;
	int font_cache;

	file_cache = edje_file_cache_get();
	collection_cache = edje_collection_cache_get();
	image_cache = evas_image_cache_get(evas);
	font_cache = evas_font_cache_get(evas);

	edje_file_cache_set(file_cache);
	edje_collection_cache_set(collection_cache);
	evas_image_cache_set(evas, 0);
	evas_font_cache_set(evas, 0);

	evas_image_cache_flush(evas);
	evas_render_idle_flush(evas);
	evas_font_cache_flush(evas);

	edje_file_cache_flush();
	edje_collection_cache_flush();

	edje_file_cache_set(file_cache);
	edje_collection_cache_set(collection_cache);
	evas_image_cache_set(evas, image_cache);
	evas_font_cache_set(evas, font_cache);

	eet_clearcache();
}
 */

static inline Evas_Object *find_edje(struct info *handle, const char *id)
{
	Eina_List *l;
	Evas_Object *edje;
	struct obj_info *obj_info;

	EINA_LIST_FOREACH(handle->obj_list, l, edje) {
		obj_info = evas_object_data_get(edje, "obj_info");
		if (!obj_info) {
			ErrPrint("Object info is not valid\n");
			continue;
		}

		if (!id) {
			if (!obj_info->id)
				return edje;

			continue;
		} else if (!obj_info->id) {
			continue;
		}

		if (!strcmp(obj_info->id, id))
			return edje;
	}

	DbgPrint("EDJE[%s] is not found\n", id);
	return NULL;
}

const char *script_magic_id(void)
{
	return "edje";
}

int script_update_color(void *h, Evas *e, const char *id, const char *part, const char *rgba)
{
	struct info *handle = h;
	Evas_Object *edje;
	int r[3], g[3], b[3], a[3];
	int ret;

	edje = find_edje(handle, id);
	if (!edje)
		return -ENOENT;

	ret = sscanf(rgba, "%d %d %d %d %d %d %d %d %d %d %d %d",
					r, g, b, a,			/* OBJECT */
					r + 1, g + 1, b + 1, a + 1,	/* OUTLINE */
					r + 2, g + 2, b + 2, a + 2);	/* SHADOW */
	if (ret != 12) {
		DbgPrint("id[%s] part[%s] rgba[%s]\n", id, part, rgba);
		return -EINVAL;
	}

	ret = edje_object_color_class_set(edje, part,
				r[0], g[0], b[0], a[0], /* OBJECT */
				r[1], g[1], b[1], a[1], /* OUTLINE */
				r[2], g[2], b[2], a[2]); /* SHADOW */

	DbgPrint("EDJE[%s] color class is %s changed", id, ret == EINA_TRUE ? "successfully" : "not");
	return 0;
}

int script_update_text(void *h, Evas *e, const char *id, const char *part, const char *text)
{
	struct info *handle = h;
	Evas_Object *edje;

	edje = find_edje(handle, id);
	if (!edje)
		return -ENOENT;

	edje_object_part_text_set(edje, part, text);
	return 0;
}

int script_update_image(void *_h, Evas *e, const char *id, const char *part, const char *path)
{
	struct info *handle = _h;
	Evas_Load_Error err;
	Evas_Object *edje;
	Evas_Object *img;
	Evas_Coord w, h;
	struct obj_info *obj_info;
	struct child *child;

	edje = find_edje(handle, id);
	if (!edje) {
		ErrPrint("No such object: %s\n", id);
		return -ENOENT;
	}

	obj_info = evas_object_data_get(edje, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not available\n");
		return -EFAULT;
	}

	img = edje_object_part_swallow_get(edje, part);
	if (img) {
		Eina_List *l;
		Eina_List *n;

		edje_object_part_unswallow(edje, img);

		EINA_LIST_FOREACH_SAFE(obj_info->children, l, n, child) {
			if (child->obj != img)
				continue;

			obj_info->children = eina_list_remove(obj_info->children, child);
			free(child->part);
			free(child);
			break;
		}

		DbgPrint("delete object %s %p\n", part, img);
		evas_object_del(img);
	}

	if (!path || !strlen(path) || access(path, R_OK) != 0) {
		DbgPrint("SKIP - Path: [%s]\n", path);
		return 0;
	}

	child = malloc(sizeof(*child));
	if (!child) {
		ErrPrint("Heap: %s\n", strerror(errno));
		return -ENOMEM;
	}

	child->part = strdup(part);
	if (!child->part) {
		ErrPrint("Heap: %s\n", strerror(errno));
		free(child);
		return -ENOMEM;
	}

	img = evas_object_image_filled_add(e);
	if (!img) {
		ErrPrint("Failed to add an image object\n");
		free(child->part);
		free(child);
		return -EFAULT;
	}

	child->obj = img;

	evas_object_image_file_set(img, path, NULL);
	err = evas_object_image_load_error_get(img);
	if (err != EVAS_LOAD_ERROR_NONE) {
		ErrPrint("Load error: %s\n", evas_load_error_str(err));
		evas_object_del(img);
		free(child->part);
		free(child);
		return -EIO;
	}

	evas_object_image_size_get(img, &w, &h);
	evas_object_image_fill_set(img, 0, 0, w, h);
	evas_object_size_hint_fill_set(img, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_size_hint_weight_set(img, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_resize(img, w, h);

	/*!
	 * \note
	 * object will be shown by below statement automatically
	 */
	DbgPrint("%s part swallow image %p\n", part, img);
	edje_object_part_swallow(edje, part, img);
	obj_info->children = eina_list_append(obj_info->children, child);

	return 0;
}

static void script_signal_cb(void *data, Evas_Object *obj, const char *emission, const char *source)
{
	struct info *handle = data;
	Evas_Coord w;
	Evas_Coord h;
	Evas_Coord px = 0;
	Evas_Coord py = 0;
	Evas_Coord pw = 0;
	Evas_Coord ph = 0;
	double sx;
	double sy;
	double ex;
	double ey;

	evas_object_geometry_get(obj, NULL, NULL, &w, &h);
	edje_object_part_geometry_get(obj, source, &px, &py, &pw, &ph);

	sx = ex = 0.0f;
	if (w) {
		sx = (double)px / (double)w;
		ex = (double)(px + pw) / (double)w;
	}

	sy = ey = 0.0f;
	if (h) {
		sy = (double)py / (double)h;
		ey = (double)(py + ph) / (double)h;
	}

	DbgPrint("Signal emit: source[%s], emission[%s]\n", source, emission);
	script_signal_emit(handle->e, source, emission, sx, sy, ex, ey);
}

static void edje_del_cb(void *_info, Evas *e, Evas_Object *obj, void *event_info)
{
	struct info *handle = _info;
	struct obj_info *obj_info;
	struct child *child;

	handle->obj_list = eina_list_remove(handle->obj_list, obj);

	obj_info = evas_object_data_del(obj, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not valid\n");
		return;
	}

	DbgPrint("delete object %s %p\n", obj_info->id, obj);

	edje_object_signal_callback_del_full(obj, "*", "*", script_signal_cb, handle);

	EINA_LIST_FREE(obj_info->children, child) {
		DbgPrint("delete object %s %p\n", child->part, child->obj);
		if (child->obj)
			evas_object_del(child->obj);
		free(child->part);
		free(child);
	}

	free(obj_info->id);
	free(obj_info);
}

int script_update_script(void *h, Evas *e, const char *src_id, const char *target_id, const char *part, const char *path, const char *group)
{
	struct info *handle = h;
	Evas_Object *edje;
	Evas_Object *obj;
	struct obj_info *obj_info;
	struct child *child;

	DbgPrint("src_id[%s] target_id[%s] part[%s] path[%s] group[%s]\n", src_id, target_id, part, path, group);

	edje = find_edje(handle, src_id);
	if (!edje) {
		ErrPrint("Edje is not exists\n");
		return -ENOENT;
	}

	obj_info = evas_object_data_get(edje, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not valid\n");
		return -EINVAL;
	}

	obj = edje_object_part_swallow_get(edje, part);
	if (obj) {
		Eina_List *l;
		Eina_List *n;

		edje_object_part_unswallow(edje, obj);

		EINA_LIST_FOREACH_SAFE(obj_info->children, l, n, child) {
			if (child->obj != obj)
				continue;

			obj_info->children = eina_list_remove(obj_info->children, child);
			free(child->part);
			free(child);
			break;
		}

		DbgPrint("delete object %s %p\n", part, obj);
		evas_object_del(obj);
	}

	if (!path || !strlen(path) || access(path, R_OK) != 0) {
		DbgPrint("SKIP - Path: [%s]\n", path);
		return 0;
	}

	obj = edje_object_add(e);
	if (!obj) {
		ErrPrint("Failed to add a new edje object\n");
		return -EFAULT;
	}

	//edje_object_text_class_set(obj, TEXT_CLASS, s_info.font, s_info.size);
	if (!edje_object_file_set(obj, path, group)) {
 		int err;
		const char *errmsg;

		err = edje_object_load_error_get(obj);
		errmsg = edje_load_error_str(err);
		ErrPrint("Could not load %s from %s: %s\n", group, path, errmsg);
		evas_object_del(obj);
		return -EIO;
	}

	evas_object_show(obj);

	obj_info = calloc(1, sizeof(*obj_info));
	if (!obj_info) {
		ErrPrint("Failed to add a obj_info\n");
		evas_object_del(obj);
		return -ENOMEM;
	}

	obj_info->id = strdup(target_id);
	if (!obj_info->id) {
		ErrPrint("Failed to add a obj_info\n");
		free(obj_info);
		evas_object_del(obj);
		return -ENOMEM;
	}

	child = malloc(sizeof(*child));
	if (!child) {
		ErrPrint("Error: %s\n", strerror(errno));
		free(obj_info->id);
		free(obj_info);
		evas_object_del(obj);
		return -ENOMEM;
	}

	child->part = strdup(part);
	if (!child->part) {
		ErrPrint("Error: %s\n", strerror(errno));
		free(child);
		free(obj_info->id);
		free(obj_info);
		evas_object_del(obj);
		return -ENOMEM;
	}

	child->obj = obj;

	evas_object_data_set(obj, "obj_info", obj_info);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, edje_del_cb, handle);
	edje_object_signal_callback_add(obj, "*", "*", script_signal_cb, handle);
	handle->obj_list = eina_list_append(handle->obj_list, obj);

	DbgPrint("%s part swallow edje %p\n", part, obj);
	edje_object_part_swallow(edje, part, obj);
	obj_info = evas_object_data_get(edje, "obj_info");
	obj_info->children = eina_list_append(obj_info->children, child);
	return 0;
}

int script_update_signal(void *h, Evas *e, const char *id, const char *part, const char *signal)
{
	struct info *handle = h;
	Evas_Object *edje;

	DbgPrint("id[%s], part[%s], signal[%s]\n", id, part, signal);

	edje = find_edje(handle, id);
	if (!edje)
		return -ENOENT;

	edje_object_signal_emit(edje, signal, part);
	return 0;
}

int script_update_drag(void *h, Evas *e, const char *id, const char *part, double x, double y)
{
	struct info *handle = h;
	Evas_Object *edje;

	DbgPrint("id[%s], part[%s], %lfx%lf\n", id, part, x, y);

	edje = find_edje(handle, id);
	if (!edje)
		return -ENOENT;

	edje_object_part_drag_value_set(edje, part, x, y);
	return 0;
}

int script_update_size(void *han, Evas *e, const char *id, int w, int h)
{
	struct info *handle = han;
	Evas_Object *edje;

	edje = find_edje(handle, id);
	if (!edje)
		return -ENOENT;

	if (!id) {
		handle->w = w;
		handle->h = h;
	}

	DbgPrint("Resize object to %dx%d\n", w, h);
	evas_object_resize(edje, w, h);
	return 0;
}

int script_update_category(void *h, Evas *e, const char *id, const char *category)
{
	struct info *handle = h;

	DbgPrint("id[%s], category[%s]\n", id, category);

	if (handle->category) {
		free(handle->category);
		handle->category = NULL;
	}

	if (!category)
		return 0;

	handle->category = strdup(category);
	if (!handle->category) {
		ErrPrint("Error: %s\n", strerror(errno));
		return -ENOMEM;
	}

	return 0;
}

void *script_create(const char *file, const char *group)
{
	struct info *handle;

	DbgPrint("file[%s], group[%s]\n", file, group);

	handle = calloc(1, sizeof(*handle));
	if (!handle) {
		ErrPrint("Error: %s\n", strerror(errno));
		return NULL;
	}

	handle->file = strdup(file);
	if (!handle->file) {
		ErrPrint("Error: %s\n", strerror(errno));
		free(handle);
		return NULL;
	}

	handle->group = strdup(group);
	if (!handle->group) {
		ErrPrint("Error: %s\n", strerror(errno));
		free(handle->file);
		free(handle);
		return NULL;
	}

	return handle;
}

int script_destroy(void *_handle)
{
	struct info *handle;
	Evas_Object *edje;

	handle = _handle;

	edje = eina_list_nth(handle->obj_list, 0);
	if (edje)
		evas_object_del(edje);

	free(handle->category);
	free(handle->file);
	free(handle->group);
	free(handle);
	return 0;
}

int script_load(void *_handle, Evas *e, int w, int h)
{
	struct info *handle;
	Evas_Object *edje;
	struct obj_info *obj_info;

	handle = _handle;

	obj_info = calloc(1, sizeof(*obj_info));
	if (!obj_info) {
		ErrPrint("Heap: %s\n", strerror(errno));
		return -ENOMEM;
	}

	edje = edje_object_add(e);
	if (!edje) {
		ErrPrint("Failed to create an edje object\n");
		free(obj_info);
		return -EFAULT;
	}

	//edje_object_text_class_set(edje, TEXT_CLASS, s_info.font, s_info.size);
	DbgPrint("Load edje: %s - %s\n", handle->file, handle->group);
	if (!edje_object_file_set(edje, handle->file, handle->group)) {
 		int err;
		const char *errmsg;

		err = edje_object_load_error_get(edje);
		errmsg = edje_load_error_str(err);
		ErrPrint("Could not load %s from %s: %s\n", handle->group, handle->file, errmsg);
		evas_object_del(edje);
		free(obj_info);
		return -EIO;
	}

	handle->e = e;
	handle->w = w;
	handle->h = h;

	edje_object_signal_callback_add(edje, "*", "*", script_signal_cb, handle);
	evas_object_event_callback_add(edje, EVAS_CALLBACK_DEL, edje_del_cb, handle);
	evas_object_size_hint_weight_set(edje, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_fill_set(edje, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_resize(edje, handle->w, handle->h);
	evas_object_show(edje);
	evas_object_data_set(edje, "obj_info", obj_info);

	handle->obj_list = eina_list_append(handle->obj_list, edje);
	return 0;
}

int script_unload(void *_handle, Evas *e)
{
	struct info *handle;
	Evas_Object *edje;

	handle = _handle;

	DbgPrint("Unload edje: %s - %s\n", handle->file, handle->group);
	edje = eina_list_nth(handle->obj_list, 0);
	if (edje)
		evas_object_del(edje);
	handle->e = NULL;
	return 0;
}

static Eina_Bool property_cb(void *data, int type, void *event)
{
	Ecore_X_Event_Window_Property *info = (Ecore_X_Event_Window_Property *)event;

	if (info->atom == ecore_x_atom_get("FONT_TYPE_change") || info->atom == ecore_x_atom_get("BADA_FONT_change")) {
		Eina_List *list;
		char *text;
		char *font;
		int cache;

		font = vconf_get_str("db/setting/accessibility/font_name");
		if (!font)
			return ECORE_CALLBACK_PASS_ON;

		if (s_info.font)
			free(s_info.font);

		s_info.font = font;

		cache = evas_common_font_cache_get();
		evas_common_font_cache_set(0);
		evas_common_font_flush();

		list = edje_text_class_list();
		EINA_LIST_FREE(list, text) {
			if (!strncasecmp(text, TEXT_CLASS, strlen(TEXT_CLASS))) {
				edje_text_class_del(text);
				edje_text_class_set(text, s_info.font, s_info.size);
				DbgPrint("Update text class %s (%s, %d)\n", text, s_info.font, s_info.size);
			} else {
				DbgPrint("Skip text class %s\n", text);
			}
		}

		evas_common_font_cache_set(cache);
	}

	return ECORE_CALLBACK_PASS_ON;
}

static void font_name_cb(keynode_t *node, void *user_data)
{
	const char *font;

	if (!node)
		return;

	font = vconf_keynode_get_str(node);
	if (!font)
		return;

	DbgPrint("Font changed to %s\n", font);
}

static void font_size_cb(keynode_t *node, void *user_data)
{
	if (!node)
		return;
	/*!
	 * \TODO
	 * Implementing me.
	 */
	DbgPrint("Size type: %d\n", vconf_keynode_get_int(node));
}

int script_init(void)
{
	int ret;
	/* ecore is already initialized */
	edje_init();

	s_info.property_handler = ecore_event_handler_add(ECORE_X_EVENT_WINDOW_PROPERTY, property_cb, NULL);
	if (!s_info.property_handler)
		ErrPrint("Failed to add a property change event handler\n");

	ret = vconf_notify_key_changed(VCONFKEY_SETAPPL_ACCESSIBILITY_FONT_SIZE, font_size_cb, NULL);
	if (ret < 0)
		ErrPrint("Failed to add vconf for font size change\n");

	ret = vconf_notify_key_changed("db/setting/accessibility/font_name", font_name_cb, NULL);
	if (ret < 0)
		ErrPrint("Failed to add vconf for font name change\n");

	return 0;
}

int script_fini(void)
{

	vconf_ignore_key_changed("db/setting/accessibility/font_name", font_name_cb);
	vconf_ignore_key_changed(VCONFKEY_SETAPPL_ACCESSIBILITY_FONT_SIZE, font_size_cb);
	ecore_event_handler_del(s_info.property_handler);
	s_info.property_handler = NULL;
	edje_shutdown();
	return 0;
}

/* End of a file */
