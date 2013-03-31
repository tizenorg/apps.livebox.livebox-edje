/*
 * Copyright 2013  Samsung Electronics Co., Ltd
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
#include <ctype.h>

#include <Elementary.h>
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
#include <livebox-errno.h>

#include "script_port.h"

#define TEXT_CLASS	"tizen"
#define BASE_WIDTH	720.0f

#define PUBLIC __attribute__((visibility("default")))

struct image_option {
	int orient;
	int aspect;
	enum {
		FILL_DISABLE,
		FILL_IN_SIZE,
		FILL_OVER_SIZE,
	} fill;

	int width;
	int height;
};

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
	Evas_Object *win;
};

static inline double scale_get(void)
{
	int width;
	int height;
	ecore_x_window_size_get(0, &width, &height);
	return (double)width / BASE_WIDTH;
}

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

PUBLIC const char *script_magic_id(void)
{
	return "edje";
}

PUBLIC int script_update_color(void *h, Evas *e, const char *id, const char *part, const char *rgba)
{
	struct info *handle = h;
	Evas_Object *edje;
	int r[3], g[3], b[3], a[3];
	int ret;

	edje = find_edje(handle, id);
	if (!edje)
		return LB_STATUS_ERROR_NOT_EXIST;

	ret = sscanf(rgba, "%d %d %d %d %d %d %d %d %d %d %d %d",
					r, g, b, a,			/* OBJECT */
					r + 1, g + 1, b + 1, a + 1,	/* OUTLINE */
					r + 2, g + 2, b + 2, a + 2);	/* SHADOW */
	if (ret != 12) {
		DbgPrint("id[%s] part[%s] rgba[%s]\n", id, part, rgba);
		return LB_STATUS_ERROR_INVALID;
	}

	ret = edje_object_color_class_set(elm_layout_edje_get(edje), part,
				r[0], g[0], b[0], a[0], /* OBJECT */
				r[1], g[1], b[1], a[1], /* OUTLINE */
				r[2], g[2], b[2], a[2]); /* SHADOW */

	DbgPrint("EDJE[%s] color class is %s changed", id, ret == EINA_TRUE ? "successfully" : "not");
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_update_text(void *h, Evas *e, const char *id, const char *part, const char *text)
{
	struct info *handle = h;
	Evas_Object *edje;

	edje = find_edje(handle, id);
	if (!edje)
		return LB_STATUS_ERROR_NOT_EXIST;

	elm_object_part_text_set(edje, part, text);
	return LB_STATUS_SUCCESS;
}

static void parse_aspect(struct image_option *img_opt, const char *value, int len)
{
	while (len > 0 && *value == ' ') {
		value++;
		len--;
	}

	if (len < 4)
		return;

	img_opt->aspect = !strncasecmp(value, "true", 4);
	DbgPrint("Parsed ASPECT: %d (%s)\n", img_opt->aspect, value);
}

static void parse_orient(struct image_option *img_opt, const char *value, int len)
{
	while (len > 0 && *value == ' ') {
		value++;
		len--;
	}

	if (len < 4)
		return;

	img_opt->orient = !strncasecmp(value, "true", 4);
	DbgPrint("Parsed ORIENT: %d (%s)\n", img_opt->aspect, value);
}

static void parse_size(struct image_option *img_opt, const char *value, int len)
{
	int width;
	int height;
	char *buf;

	while (len > 0 && *value == ' ') {
		value++;
		len--;
	}

	buf = strndup(value, len);
	if (!buf) {
		ErrPrint("Heap: %s\n", strerror(errno));
		return;
	}

	if (sscanf(buf, "%dx%d", &width, &height) == 2) {
		img_opt->width = width;
		img_opt->height = height;
		DbgPrint("Parsed size : %dx%d (%s)\n", width, height, buf);
	} else {
		DbgPrint("Invalid size tag[%s]\n", buf);
	}

	free(buf);
}

static void parse_fill(struct image_option *img_opt, const char *value, int len)
{
	while (len > 0 && *value == ' ') {
		value++;
		len--;
	}

	if (!strncasecmp(value, "in-size", len))
		img_opt->fill = FILL_IN_SIZE;
	else if (!strncasecmp(value, "over-size", len))
		img_opt->fill = FILL_OVER_SIZE;
	else
		img_opt->fill = FILL_DISABLE;

	DbgPrint("Parsed FILL: %d (%s)\n", img_opt->fill, value);
}

static inline void parse_image_option(const char *option, struct image_option *img_opt)
{
	const char *ptr;
	const char *cmd;
	const char *value;
	struct {
		const char *cmd;
		void (*handler)(struct image_option *img_opt, const char *value, int len);
	} cmd_list[] = {
		{
			.cmd = "aspect", /* Keep the aspect ratio */
			.handler = parse_aspect,
		},
		{
			.cmd = "orient", /* Keep the orientation value: for the rotated images */
			.handler = parse_orient,
		},
		{
			.cmd = "fill", /* Fill the image to its container */
			.handler = parse_fill, /* Value: in-size, over-size, disable(default) */
		},
		{
			.cmd = "size",
			.handler = parse_size,
		},
	};
	enum {
		STATE_START,
		STATE_TOKEN,
		STATE_DATA,
		STATE_IGNORE,
		STATE_ERROR,
		STATE_END,
	} state;
	int idx;
	int tag;

	if (!option || !*option)
		return;

	state = STATE_START;
	/*!
	 * \note
	 * GCC 4.7 warnings uninitialized idx and tag value.
	 * But it will be initialized by the state machine. :(
	 * Anyway, I just reset idx and tag for reducing the GCC4.7 complains.
	 */
	idx = 0;
	tag = 0;
	cmd = NULL;
	value = NULL;

	for (ptr = option; state != STATE_END; ptr++) {
		switch (state) {
		case STATE_START:
			if (*ptr == '\0') {
				state = STATE_END;
				continue;
			}

			if (isalpha(*ptr)) {
				state = STATE_TOKEN;
				ptr--;
			}
			tag = 0;
			idx = 0;

			cmd = cmd_list[tag].cmd;
			break;
		case STATE_IGNORE:
			if (*ptr == '=') {
				state = STATE_DATA;
				value = ptr;
			} else if (*ptr == '\0') {
				state = STATE_END;
			}
			break;
		case STATE_TOKEN:
			if (cmd[idx] == '\0' && (*ptr == ' ' || *ptr == '\t' || *ptr == '=')) {
				if (*ptr == '=') {
					value = ptr;
					state = STATE_DATA;
				} else {
					state = STATE_IGNORE;
				}
				idx = 0;
			} else if (*ptr == '\0') {
				state = STATE_END;
			} else if (cmd[idx] == *ptr) {
				idx++;
			} else {
				ptr -= (idx + 1);

				tag++;
				if (tag == sizeof(cmd_list) / sizeof(cmd_list[0])) {
					tag = 0;
					state = STATE_ERROR;
				} else {
					cmd = cmd_list[tag].cmd;
				}
				idx = 0;
			}
			break;
		case STATE_DATA:
			if (*ptr == ';' || *ptr == '\0') {
				cmd_list[tag].handler(img_opt, value + 1, idx);
				state = *ptr ? STATE_START : STATE_END;
			} else {
				idx++;
			}
			break;
		case STATE_ERROR:
			if (*ptr == ';')
				state = STATE_START;
			else if (*ptr == '\0')
				state = STATE_END;
			break;
		default:
			break;
		}
	}
}

PUBLIC int script_update_image(void *_h, Evas *e, const char *id, const char *part, const char *path, const char *option)
{
	struct info *handle = _h;
	Evas_Load_Error err;
	Evas_Object *edje;
	Evas_Object *img;
	Evas_Coord w, h;
	struct obj_info *obj_info;
	struct child *child;
	struct image_option img_opt = {
		.aspect = 0,
		.orient = 0,
		.fill = FILL_DISABLE,
		.width = -1,
		.height = -1,
	};

	edje = find_edje(handle, id);
	if (!edje) {
		ErrPrint("No such object: %s\n", id);
		return LB_STATUS_ERROR_NOT_EXIST;
	}

	obj_info = evas_object_data_get(edje, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not available\n");
		return LB_STATUS_ERROR_FAULT;
	}

	img = elm_object_part_content_unset(edje, part);
	if (img) {
		Eina_List *l;
		Eina_List *n;

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
		return LB_STATUS_SUCCESS;
	}

	child = malloc(sizeof(*child));
	if (!child) {
		ErrPrint("Heap: %s\n", strerror(errno));
		return LB_STATUS_ERROR_MEMORY;
	}

	child->part = strdup(part);
	if (!child->part) {
		ErrPrint("Heap: %s\n", strerror(errno));
		free(child);
		return LB_STATUS_ERROR_MEMORY;
	}

	img = evas_object_image_add(e);
	if (!img) {
		ErrPrint("Failed to add an image object\n");
		free(child->part);
		free(child);
		return LB_STATUS_ERROR_FAULT;
	}

	evas_object_image_preload(img, EINA_FALSE);
	parse_image_option(option, &img_opt);
	evas_object_image_load_orientation_set(img, img_opt.orient);

	evas_object_image_file_set(img, path, NULL);
	err = evas_object_image_load_error_get(img);
	if (err != EVAS_LOAD_ERROR_NONE) {
		ErrPrint("Load error: %s\n", evas_load_error_str(err));
		evas_object_del(img);
		free(child->part);
		free(child);
		return LB_STATUS_ERROR_IO;
	}

	evas_object_image_size_get(img, &w, &h);
	if (img_opt.aspect) {
		if (img_opt.fill == FILL_OVER_SIZE) {
			Evas_Coord part_w;
			Evas_Coord part_h;

			if (img_opt.width >= 0 && img_opt.height >= 0) {
				part_w = img_opt.width * scale_get();
				part_h = img_opt.height * scale_get();
			} else {
				part_w = 0;
				part_h = 0;
				edje_object_part_geometry_get(elm_layout_edje_get(edje), part, NULL, NULL, &part_w, &part_h);
			}
			DbgPrint("Original %dx%d (part: %dx%d)\n", w, h, part_w, part_h);

			if (part_w > w || part_h > h) {
				double fw;
				double fh;

				fw = (double)part_w / (double)w;
				fh = (double)part_h / (double)h;

				if (fw > fh) {
					w = part_w;
					h = (double)h * fw;
				} else {
					h = part_h;
					w = (double)w * fh;
				}
			}
			DbgPrint("Size: %dx%d\n", w, h);

			evas_object_image_load_size_set(img, w, h);
			evas_object_image_load_region_set(img, (w - part_w) / 2, (h - part_h) / 2, part_w, part_h);
			evas_object_image_fill_set(img, 0, 0, part_w, part_h);
			evas_object_image_reload(img);
		} else {
			evas_object_image_fill_set(img, 0, 0, w, h);
			evas_object_size_hint_fill_set(img, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_size_hint_aspect_set(img, EVAS_ASPECT_CONTROL_BOTH, w, h);
		}
	} else {
		if (img_opt.width >= 0 && img_opt.height >= 0) {
			w = img_opt.width;
			h = img_opt.height;
			DbgPrint("Using given image size: %dx%d\n", w, h);
		}

		evas_object_image_fill_set(img, 0, 0, w, h);
		evas_object_size_hint_fill_set(img, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_size_hint_weight_set(img, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	}

	/*!
	 * \note
	 * object will be shown by below statement automatically
	 */
	DbgPrint("%s part swallow image %p (%dx%d)\n", part, img, w, h);
	child->obj = img;
	elm_object_part_content_set(edje, part, img);
	obj_info->children = eina_list_append(obj_info->children, child);

	return LB_STATUS_SUCCESS;
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
	edje_object_part_geometry_get(elm_layout_edje_get(obj), source, &px, &py, &pw, &ph);

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

	elm_object_signal_callback_del(obj, "*", "*", script_signal_cb);

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

PUBLIC int script_update_script(void *h, Evas *e, const char *src_id, const char *target_id, const char *part, const char *path, const char *group)
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
		return LB_STATUS_ERROR_NOT_EXIST;
	}

	obj_info = evas_object_data_get(edje, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not valid\n");
		return LB_STATUS_ERROR_INVALID;
	}

	obj = elm_object_part_content_unset(edje, part);
	if (obj) {
		Eina_List *l;
		Eina_List *n;

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
		return LB_STATUS_SUCCESS;
	}

	obj = elm_layout_add(edje);
	if (!obj) {
		ErrPrint("Failed to add a new edje object\n");
		return LB_STATUS_ERROR_FAULT;
	}

	if (!elm_layout_file_set(obj, path, group)) {
 		int err;
		const char *errmsg;

		err = edje_object_load_error_get(elm_layout_edje_get(obj));
		errmsg = edje_load_error_str(err);
		ErrPrint("Could not load %s from %s: %s\n", group, path, errmsg);
		evas_object_del(obj);
		return LB_STATUS_ERROR_IO;
	}

	evas_object_show(obj);

	obj_info = calloc(1, sizeof(*obj_info));
	if (!obj_info) {
		ErrPrint("Failed to add a obj_info\n");
		evas_object_del(obj);
		return LB_STATUS_ERROR_MEMORY;
	}

	obj_info->id = strdup(target_id);
	if (!obj_info->id) {
		ErrPrint("Failed to add a obj_info\n");
		free(obj_info);
		evas_object_del(obj);
		return LB_STATUS_ERROR_MEMORY;
	}

	child = malloc(sizeof(*child));
	if (!child) {
		ErrPrint("Error: %s\n", strerror(errno));
		free(obj_info->id);
		free(obj_info);
		evas_object_del(obj);
		return LB_STATUS_ERROR_MEMORY;
	}

	child->part = strdup(part);
	if (!child->part) {
		ErrPrint("Error: %s\n", strerror(errno));
		free(child);
		free(obj_info->id);
		free(obj_info);
		evas_object_del(obj);
		return LB_STATUS_ERROR_MEMORY;
	}

	child->obj = obj;

	evas_object_data_set(obj, "obj_info", obj_info);
	evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, edje_del_cb, handle);
	elm_object_signal_callback_add(obj, "*", "*", script_signal_cb, handle);
	handle->obj_list = eina_list_append(handle->obj_list, obj);

	DbgPrint("%s part swallow edje %p\n", part, obj);
	elm_object_part_content_set(edje, part, obj);
	obj_info = evas_object_data_get(edje, "obj_info");
	obj_info->children = eina_list_append(obj_info->children, child);
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_update_signal(void *h, Evas *e, const char *id, const char *part, const char *signal)
{
	struct info *handle = h;
	Evas_Object *edje;

	DbgPrint("id[%s], part[%s], signal[%s]\n", id, part, signal);

	edje = find_edje(handle, id);
	if (!edje)
		return LB_STATUS_ERROR_NOT_EXIST;

	elm_object_signal_emit(edje, signal, part);
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_update_drag(void *h, Evas *e, const char *id, const char *part, double x, double y)
{
	struct info *handle = h;
	Evas_Object *edje;

	DbgPrint("id[%s], part[%s], %lfx%lf\n", id, part, x, y);

	edje = find_edje(handle, id);
	if (!edje)
		return LB_STATUS_ERROR_NOT_EXIST;

	edje_object_part_drag_value_set(elm_layout_edje_get(edje), part, x, y);
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_update_size(void *han, Evas *e, const char *id, int w, int h)
{
	struct info *handle = han;
	Evas_Object *edje;

	edje = find_edje(handle, id);
	if (!edje)
		return LB_STATUS_ERROR_NOT_EXIST;

	if (!id) {
		handle->w = w;
		handle->h = h;
	}

	DbgPrint("Resize object to %dx%d\n", w, h);
	evas_object_resize(edje, w, h);
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_update_category(void *h, Evas *e, const char *id, const char *category)
{
	struct info *handle = h;

	DbgPrint("id[%s], category[%s]\n", id, category);

	if (handle->category) {
		free(handle->category);
		handle->category = NULL;
	}

	if (!category)
		return LB_STATUS_SUCCESS;

	handle->category = strdup(category);
	if (!handle->category) {
		ErrPrint("Error: %s\n", strerror(errno));
		return LB_STATUS_ERROR_MEMORY;
	}

	return LB_STATUS_SUCCESS;
}

PUBLIC void *script_create(const char *file, const char *group)
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

PUBLIC int script_destroy(void *_handle)
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
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_load(void *_handle, Evas *e, int w, int h)
{
	struct info *handle;
	Evas_Object *edje;
	struct obj_info *obj_info;

	handle = _handle;

	obj_info = calloc(1, sizeof(*obj_info));
	if (!obj_info) {
		ErrPrint("Heap: %s\n", strerror(errno));
		return LB_STATUS_ERROR_MEMORY;
	}

	obj_info->win = evas_object_rectangle_add(e);
	if (!obj_info->win) {
		free(obj_info);
		return LB_STATUS_ERROR_FAULT;
	}

	edje = elm_layout_add(obj_info->win);
	if (!edje) {
		ErrPrint("Failed to create an edje object\n");
		evas_object_del(obj_info->win);
		free(obj_info);
		return LB_STATUS_ERROR_FAULT;
	}

	DbgPrint("Load edje: %s - %s\n", handle->file, handle->group);
	if (!elm_layout_file_set(edje, handle->file, handle->group)) {
 		int err;
		const char *errmsg;

		err = edje_object_load_error_get(elm_layout_edje_get(edje));
		errmsg = edje_load_error_str(err);
		ErrPrint("Could not load %s from %s: %s\n", handle->group, handle->file, errmsg);
		evas_object_del(edje);
		evas_object_del(obj_info->win);
		free(obj_info);
		return LB_STATUS_ERROR_IO;
	}

	handle->e = e;
	handle->w = w;
	handle->h = h;

	elm_object_signal_callback_add(edje, "*", "*", script_signal_cb, handle);
	evas_object_event_callback_add(edje, EVAS_CALLBACK_DEL, edje_del_cb, handle);
	evas_object_size_hint_weight_set(edje, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
	evas_object_size_hint_fill_set(edje, EVAS_HINT_FILL, EVAS_HINT_FILL);
	evas_object_resize(edje, handle->w, handle->h);
	evas_object_show(edje);
	evas_object_data_set(edje, "obj_info", obj_info);

	handle->obj_list = eina_list_append(handle->obj_list, edje);
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_unload(void *_handle, Evas *e)
{
	struct info *handle;
	Evas_Object *edje;

	handle = _handle;

	DbgPrint("Unload edje: %s - %s\n", handle->file, handle->group);
	edje = eina_list_nth(handle->obj_list, 0);
	if (edje)
		evas_object_del(edje);
	handle->e = NULL;
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_init(void)
{
	char *argv[] = {
		"livebox.edje",
		NULL,
	};
	/* ecore is already initialized */
	elm_init(1, argv);
	elm_config_scale_set(scale_get());

	return LB_STATUS_SUCCESS;
}

PUBLIC int script_fini(void)
{
	elm_shutdown();
	return LB_STATUS_SUCCESS;
}

/* End of a file */
