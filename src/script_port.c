/*
 * Copyright 2013  Samsung Electronics Co., Ltd
 *
 * Licensed under the Flora License, Version 1.1 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://floralicense.org/license/
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

#include <system_settings.h>

#include <dlog.h>
#include <debug.h>
#include <vconf.h>
#include <livebox-errno.h>
#include <livebox-service.h>

#include "script_port.h"

#define TEXT_CLASS	"tizen"
#define DEFAULT_FONT_SIZE	-100

#define PUBLIC __attribute__((visibility("default")))

struct image_option {
	int orient;
	int aspect;
	enum {
		FILL_DISABLE,
		FILL_IN_SIZE,
		FILL_OVER_SIZE
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

	int is_mouse_down;

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
	Evas_Object *parent;
	Eina_List *access_chain;
};

static struct {
	char *font_name;
	int font_size;

	Eina_List *handle_list;
} s_info = {
	.font_name = NULL,
	.font_size = -100,

	.handle_list = NULL,
};

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
			if (!obj_info->id) {
				return edje;
			}

			continue;
		} else if (!obj_info->id) {
			continue;
		}

		if (!strcmp(obj_info->id, id)) {
			return edje;
		}
	}

	DbgPrint("EDJE[%s] is not found\n", id);
	return NULL;
}

static inline void rebuild_focus_chain(Evas_Object *obj)
{
	struct obj_info *obj_info;
	Evas_Object *ao;
	Eina_List *l;

	obj_info = evas_object_data_get(obj, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not available\n");
		return;
	}

	elm_object_focus_custom_chain_unset(obj);

	EINA_LIST_FOREACH(obj_info->access_chain, l, ao) {
		DbgPrint("Append %p\n", ao);
		elm_object_focus_custom_chain_append(obj, ao, NULL);
	}
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
	if (!edje) {
		return LB_STATUS_ERROR_NOT_EXIST;
	}

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

static void activate_cb(void *data, Evas_Object *part_obj, Elm_Object_Item *item)
{
	Evas_Object *ao;
	Evas_Object *edje;
	Evas *e;
	int x;
	int y;
	int w;
	int h;
	double timestamp;

	ao = evas_object_data_get(part_obj, "ao");
	if (!ao) {
		return;
	}

	edje = evas_object_data_get(ao, "edje");
	if (!edje) {
		return;
	}

	e = evas_object_evas_get(part_obj);
	evas_object_geometry_get(part_obj, &x, &y, &w, &h);
	x += w / 2;
	y += h / 2;

#if defined(_USE_ECORE_TIME_GET)
	timestamp = ecore_time_get();
#else
	struct timeval tv;
	if (gettimeofday(&tv, NULL) < 0) {
		ErrPrint("Failed to get time\n");
		timestamp = 0.0f;
	} else {
		timestamp = (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0f);
	}
#endif

	DbgPrint("Cursor is on %dx%d\n", x, y);
	evas_event_feed_mouse_move(e, x, y, timestamp * 1000, NULL);
	evas_event_feed_mouse_down(e, 1, EVAS_BUTTON_NONE, (timestamp + 0.01f) * 1000, NULL);
	evas_event_feed_mouse_move(e, x, y, (timestamp + 0.02f) * 1000, NULL);
	evas_event_feed_mouse_up(e, 1, EVAS_BUTTON_NONE, (timestamp + 0.03f) * 1000, NULL);
}

PUBLIC int script_update_text(void *h, Evas *e, const char *id, const char *part, const char *text)
{
	struct obj_info *obj_info;
	struct info *handle = h;
	Evas_Object *edje;
	Evas_Object *to;

	edje = find_edje(handle, id);
	if (!edje) {
		ErrPrint("Failed to find EDJE\n");
		return LB_STATUS_ERROR_NOT_EXIST;
	}

	obj_info = evas_object_data_get(edje, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not available\n");
		return LB_STATUS_ERROR_FAULT;
	}

	elm_object_part_text_set(edje, part, text ? text : "");

	to = (Evas_Object *)edje_object_part_object_get(elm_layout_edje_get(edje), part);
	if (to) {
		Evas_Object *ao;
		char *utf8;

		ao = evas_object_data_get(to, "ao");
		if (!ao) {
			ao = elm_access_object_register(to, edje);
			if (!ao) {
				ErrPrint("Unable to add ao: %s\n", part);
				goto out;
			}
			obj_info->access_chain = eina_list_append(obj_info->access_chain, ao);
			evas_object_data_set(to, "ao", ao);
			evas_object_data_set(ao, "edje", edje);
			elm_access_activate_cb_set(ao, activate_cb, NULL);
			elm_object_focus_custom_chain_append(edje, ao, NULL);
		}

		if (!text || !strlen(text)) {
			obj_info->access_chain = eina_list_remove(obj_info->access_chain, ao);
			evas_object_data_del(to, "ao");
			evas_object_data_del(ao, "edje");
			elm_access_object_unregister(ao);
			DbgPrint("[%s] Remove access object\n", part);

			rebuild_focus_chain(edje);
			goto out;
		}

		utf8 = elm_entry_markup_to_utf8(text);
		if ((!utf8 || !strlen(utf8))) {
			free(utf8);

			obj_info->access_chain = eina_list_remove(obj_info->access_chain, ao);
			evas_object_data_del(to, "ao");
			evas_object_data_del(ao, "edje");
			elm_access_object_unregister(ao);
			DbgPrint("[%s] Remove access object\n", part);

			rebuild_focus_chain(edje);
			goto out;
		}

		elm_access_info_set(ao, ELM_ACCESS_INFO, utf8);
		free(utf8);
	} else {
		ErrPrint("Unable to get text part[%s]\n", part);
	}

out:
	return LB_STATUS_SUCCESS;
}

static void parse_aspect(struct image_option *img_opt, const char *value, int len)
{
	while (len > 0 && *value == ' ') {
		value++;
		len--;
	}

	if (len < 4) {
		return;
	}

	img_opt->aspect = !strncasecmp(value, "true", 4);
	DbgPrint("Parsed ASPECT: %d (%s)\n", img_opt->aspect, value);
}

static void parse_orient(struct image_option *img_opt, const char *value, int len)
{
	while (len > 0 && *value == ' ') {
		value++;
		len--;
	}

	if (len < 4) {
		return;
	}

	img_opt->orient = !strncasecmp(value, "true", 4);
	DbgPrint("Parsed ORIENT: %d (%s)\n", img_opt->orient, value);
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

	if (!strncasecmp(value, "in-size", len)) {
		img_opt->fill = FILL_IN_SIZE;
	} else if (!strncasecmp(value, "over-size", len)) {
		img_opt->fill = FILL_OVER_SIZE;
	} else {
		img_opt->fill = FILL_DISABLE;
	}

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
		STATE_END
	} state;
	int idx;
	int tag;

	if (!option || !*option) {
		return;
	}

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
			if (*ptr == ';') {
				state = STATE_START;
			} else if (*ptr == '\0') {
				state = STATE_END;
			}
			break;
		default:
			break;
		}
	}
}

PUBLIC int script_update_access(void *_h, Evas *e, const char *id, const char *part, const char *text, const char *option)
{
	struct info *handle = _h;
	Evas_Object *edje;
	struct obj_info *obj_info;
	Evas_Object *to;

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

	to = (Evas_Object *)edje_object_part_object_get(elm_layout_edje_get(edje), part);
	if (to) {
		Evas_Object *ao;

		ao = evas_object_data_get(to, "ao");
		if (ao) {
			if (text && strlen(text)) {
				elm_access_info_set(ao, ELM_ACCESS_INFO, text);
			} else {
				obj_info->access_chain = eina_list_remove(obj_info->access_chain, ao);
				evas_object_data_del(to, "ao");
				evas_object_data_del(ao, "edje");
				elm_access_object_unregister(ao);
				DbgPrint("Successfully unregistered\n");

				rebuild_focus_chain(edje);
			}
		} else if (text && strlen(text)) {
			ao = elm_access_object_register(to, edje);
			if (!ao) {
				ErrPrint("Unable to register access object\n");
			} else {
				elm_access_info_set(ao, ELM_ACCESS_INFO, text);
				obj_info->access_chain = eina_list_append(obj_info->access_chain, ao);
				evas_object_data_set(to, "ao", ao);
				elm_object_focus_custom_chain_append(edje, ao, NULL);
				DbgPrint("[%s] Register access info: (%s)\n", part, text);
				evas_object_data_set(ao, "edje", edje);
				elm_access_activate_cb_set(ao, activate_cb, NULL);
			}
		}
	} else {
		ErrPrint("[%s] is not exists\n", part);
	}

	return LB_STATUS_SUCCESS;
}

PUBLIC int script_operate_access(void *_h, Evas *e, const char *id, const char *part, const char *operation, const char *option)
{
	struct info *handle = _h;
	Evas_Object *edje;
	struct obj_info *obj_info;
	Elm_Access_Action_Info action_info;
	int ret;

	if (!operation || !strlen(operation)) {
		return LB_STATUS_ERROR_INVALID;
	}

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

	memset(&action_info, 0, sizeof(action_info));

	/* OPERATION is defined in liblivebox package */
	if (!strcasecmp(operation, "set,hl")) {
		if (part) {
			Evas_Object *to;
			Evas_Coord x;
			Evas_Coord y;
			Evas_Coord w;
			Evas_Coord h;

			to = (Evas_Object *)edje_object_part_object_get(elm_layout_edje_get(edje), part);
			if (!to) {
				ErrPrint("Invalid part: %s\n", part);
				goto out;
			}

			evas_object_geometry_get(to, &x, &y, &w, &h);

			action_info.x = x + w / 2;
			action_info.y = x + h / 2;
		} else if (option && sscanf(option, "%dx%d", &action_info.x, &action_info.y) == 2) {
		} else {
			ErrPrint("Insufficient info for HL\n");
			goto out;
		}

		DbgPrint("TXxTY: %dx%d\n", action_info.x, action_info.y);
		ret = elm_access_action(edje, ELM_ACCESS_ACTION_HIGHLIGHT, &action_info);
		if (ret == EINA_FALSE) {
			ErrPrint("Action error\n");
		}
	} else if (!strcasecmp(operation, "unset,hl")) {
		ret = elm_access_action(edje, ELM_ACCESS_ACTION_UNHIGHLIGHT, &action_info);
		if (ret == EINA_FALSE) {
			ErrPrint("Action error\n");
		}
	} else if (!strcasecmp(operation, "next,hl")) {
		action_info.highlight_cycle = (!!option) && (!!strcasecmp(option, "no,cycle"));

		ret = elm_access_action(edje, ELM_ACCESS_ACTION_HIGHLIGHT_NEXT, &action_info);
		if (ret == EINA_FALSE) {
			ErrPrint("Action error\n");
		}
	} else if (!strcasecmp(operation, "prev,hl")) {
		action_info.highlight_cycle = EINA_TRUE;
		ret = elm_access_action(edje, ELM_ACCESS_ACTION_HIGHLIGHT_PREV, &action_info);
		if (ret == EINA_FALSE) {
			ErrPrint("Action error\n");
		}
	}

out:
	return LB_STATUS_SUCCESS;
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
		Evas_Object *ao;

		EINA_LIST_FOREACH_SAFE(obj_info->children, l, n, child) {
			if (child->obj != img) {
				continue;
			}

			obj_info->children = eina_list_remove(obj_info->children, child);
			free(child->part);
			free(child);
			break;
		}

		DbgPrint("delete object %s %p\n", part, img);
		ao = evas_object_data_del(img, "ao");
		if (ao) {
			obj_info->access_chain = eina_list_remove(obj_info->access_chain, ao);
			evas_object_data_del(ao, "edje");
			elm_access_object_unregister(ao);
			DbgPrint("Successfully unregistered\n");
		}
		evas_object_del(img);

		rebuild_focus_chain(edje);
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
				part_w = img_opt.width * elm_config_scale_get();
				part_h = img_opt.height * elm_config_scale_get();
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

			if (!part_w || !part_h || !w || !h) {
				evas_object_del(img);
				free(child->part);
				free(child);
				return LB_STATUS_ERROR_INVALID;
			}

			if (evas_object_image_region_support_get(img)) {
				evas_object_image_load_region_set(img, (w - part_w) / 2, (h - part_h) / 2, part_w, part_h);
				evas_object_image_load_size_set(img, part_w, part_h);
				evas_object_image_filled_set(img, EINA_TRUE);
				//evas_object_image_fill_set(img, 0, 0, part_w, part_h);
				DbgPrint("Size: %dx%d (region: %dx%d - %dx%d)\n", w, h, (w - part_w) / 2, (h - part_h) / 2, part_w, part_h);
			} else {
				Ecore_Evas *ee;
				Evas *e;
				Evas_Object *src_img;
				Evas_Coord rw, rh;
				const void *data;

				DbgPrint("Part loading is not supported\n");
				ee = ecore_evas_buffer_new(part_w, part_h);
				if (!ee) {
					ErrPrint("Failed to create a EE\n");
					evas_object_del(img);
					free(child->part);
					free(child);
					return LB_STATUS_ERROR_FAULT;
				}

				ecore_evas_alpha_set(ee, EINA_TRUE);

				e = ecore_evas_get(ee);
				if (!e) {
					ErrPrint("Unable to get Evas\n");
					ecore_evas_free(ee);

					evas_object_del(img);
					free(child->part);
					free(child);
					return LB_STATUS_ERROR_FAULT;
				}

				src_img = evas_object_image_filled_add(e);
				if (!src_img) {
					ErrPrint("Unable to add an image\n");
					ecore_evas_free(ee);

					evas_object_del(img);
					free(child->part);
					free(child);
					return LB_STATUS_ERROR_FAULT;
				}

				evas_object_image_alpha_set(src_img, EINA_TRUE);
				evas_object_image_colorspace_set(src_img, EVAS_COLORSPACE_ARGB8888);
        			evas_object_image_smooth_scale_set(src_img, EINA_TRUE);
				evas_object_image_load_orientation_set(src_img, img_opt.orient);
				evas_object_image_file_set(src_img, path, NULL);
				err = evas_object_image_load_error_get(src_img);
				if (err != EVAS_LOAD_ERROR_NONE) {
					ErrPrint("Load error: %s\n", evas_load_error_str(err));
					evas_object_del(src_img);
					ecore_evas_free(ee);

					evas_object_del(img);
					free(child->part);
					free(child);
					return LB_STATUS_ERROR_IO;
				}
				evas_object_image_size_get(src_img, &rw, &rh);
				evas_object_image_fill_set(src_img, 0, 0, rw, rh);
				evas_object_resize(src_img, w, h);
				evas_object_move(src_img, -(w - part_w) / 2, -(h - part_h) / 2);
				evas_object_show(src_img);

				data = ecore_evas_buffer_pixels_get(ee);
				if (!data) {
					ErrPrint("Unable to get pixels\n");
					evas_object_del(src_img);
					ecore_evas_free(ee);

					evas_object_del(img);
					free(child->part);
					free(child);
					return LB_STATUS_ERROR_IO;
				}

				e = evas_object_evas_get(img);
				evas_object_del(img);
				img = evas_object_image_filled_add(e);
				if (!img) {
					evas_object_del(src_img);
					ecore_evas_free(ee);

					free(child->part);
					free(child);
					return LB_STATUS_ERROR_MEMORY;
				}

				evas_object_image_colorspace_set(img, EVAS_COLORSPACE_ARGB8888);
        			evas_object_image_smooth_scale_set(img, EINA_TRUE);
				evas_object_image_alpha_set(img, EINA_TRUE);
				evas_object_image_data_set(img, NULL);
				evas_object_image_size_set(img, part_w, part_h);
				evas_object_resize(img, part_w, part_h);
				evas_object_image_data_copy_set(img, (void *)data);
				evas_object_image_fill_set(img, 0, 0, part_w, part_h);
				evas_object_image_data_update_add(img, 0, 0, part_w, part_h);

				evas_object_del(src_img);
				ecore_evas_free(ee);
			}
		} else if (img_opt.fill == FILL_IN_SIZE) {
			Evas_Coord part_w;
			Evas_Coord part_h;

			if (img_opt.width >= 0 && img_opt.height >= 0) {
				part_w = img_opt.width * elm_config_scale_get();
				part_h = img_opt.height * elm_config_scale_get();
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
			evas_object_image_fill_set(img, 0, 0, part_w, part_h);
			evas_object_size_hint_fill_set(img, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_size_hint_weight_set(img, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
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
		evas_object_image_filled_set(img, EINA_TRUE);
	}

	/*!
	 * \note
	 * object will be shown by below statement automatically
	 */
	DbgPrint("%s part swallow image %p (%dx%d)\n", part, img, w, h);
	child->obj = img;
	elm_object_part_content_set(edje, part, img);
	obj_info->children = eina_list_append(obj_info->children, child);

	/*!
	 * \note
	 * This object is not registered as an access object.
	 * So the developer should add it to access list manually, using DESC_ACCESS block.
	 */
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

	script_signal_emit(handle->e, source, emission, sx, sy, ex, ey);
}

static void edje_del_cb(void *_info, Evas *e, Evas_Object *obj, void *event_info)
{
	struct info *handle = _info;
	struct obj_info *obj_info;
	struct obj_info *parent_obj_info;
	struct child *child;
	Evas_Object *ao;

	handle->obj_list = eina_list_remove(handle->obj_list, obj);

	obj_info = evas_object_data_del(obj, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not valid\n");
		return;
	}

	DbgPrint("delete object %s %p\n", obj_info->id, obj);
	parent_obj_info = evas_object_data_get(obj_info->parent, "obj_info");
	if (parent_obj_info) {
		Eina_List *l;
		Eina_List *n;

		EINA_LIST_FOREACH_SAFE(parent_obj_info->children, l, n, child) {
			if (child->obj != obj) {
				continue;
			}

			/*!
			 * \note
			 * If this code is executed,
			 * The parent is not deleted by desc, this object is deleted by itself.
			 * It is not possible, but we care it.
			 */
			DbgPrint("Parent's children is updated: %s\n", child->part);
			parent_obj_info->children = eina_list_remove(parent_obj_info->children, child);
			free(child->part);
			free(child);
			break;
		}
	} else {
		DbgPrint("Parent EDJE\n");
	}

	elm_object_signal_callback_del(obj, "*", "*", script_signal_cb);

	elm_object_focus_custom_chain_unset(obj);

	EINA_LIST_FREE(obj_info->children, child) {
		DbgPrint("delete object %s %p\n", child->part, child->obj);
		if (child->obj) {
			Evas_Object *ao;
			ao = evas_object_data_del(child->obj, "ao");
			if (ao) {
				obj_info->access_chain = eina_list_remove(obj_info->access_chain, ao);
				evas_object_data_del(ao, "edje");
				elm_access_object_unregister(ao);
			}
			evas_object_del(child->obj);
		}
		free(child->part);
		free(child);
	}

	EINA_LIST_FREE(obj_info->access_chain, ao) {
		evas_object_data_del(ao, "edje");
		elm_access_object_unregister(ao);
	}

	free(obj_info->id);
	free(obj_info);
}

static inline Evas_Object *get_highlighted_object(Evas_Object *obj)
{
	Evas_Object *o, *ho;

	o = evas_object_name_find(evas_object_evas_get(obj), "_elm_access_disp");
	if (!o) return NULL;

	ho = evas_object_data_get(o, "_elm_access_target");
	return ho;
}

/*!
	LB_ACCESS_HIGHLIGHT		0
	LB_ACCESS_HIGHLIGHT_NEXT	1
	LB_ACCESS_HIGHLIGHT_PREV	2
	LB_ACCESS_ACTIVATE		3
	LB_ACCESS_ACTION		4
	LB_ACCESS_SCROLL		5
*/
PUBLIC int script_feed_event(void *h, Evas *e, int event_type, int x, int y, int down, double timestamp)
{
	struct info *handle = h;
	Evas_Object *edje;
	struct obj_info *obj_info;
	int ret = LB_STATUS_SUCCESS;

	edje = find_edje(handle, NULL); /*!< Get the base layout */
	if (!edje) {
		ErrPrint("Base layout is not exist\n");
		return LB_STATUS_ERROR_NOT_EXIST;
	}

	obj_info = evas_object_data_get(edje, "obj_info");
	if (!obj_info) {
		ErrPrint("Object info is not valid\n");
		return LB_STATUS_ERROR_INVALID;
	}

	if (event_type & LB_SCRIPT_ACCESS_EVENT) {
		Elm_Access_Action_Info info;
		Elm_Access_Action_Type action;

		memset(&info, 0, sizeof(info));

		if ((event_type & LB_SCRIPT_ACCESS_HIGHLIGHT) == LB_SCRIPT_ACCESS_HIGHLIGHT) {
			action = ELM_ACCESS_ACTION_HIGHLIGHT;
			info.x = x;
			info.y = y;
			ret = elm_access_action(edje, action, &info);
			DbgPrint("ACCESS_HIGHLIGHT: %dx%d returns %d\n", x, y, ret);
			if (ret == EINA_TRUE) {
				if (!get_highlighted_object(edje)) {
					ErrPrint("Highlighted object is not found\n");
					ret = LB_ACCESS_STATUS_ERROR;
				} else {
					DbgPrint("Highlighted object is found\n");
					ret = LB_ACCESS_STATUS_DONE;
				}
			} else {
				ErrPrint("Action error\n");
				ret = LB_ACCESS_STATUS_ERROR;
			}
		} else if ((event_type & LB_SCRIPT_ACCESS_HIGHLIGHT_NEXT) == LB_SCRIPT_ACCESS_HIGHLIGHT_NEXT) {
			action = ELM_ACCESS_ACTION_HIGHLIGHT_NEXT;
			info.highlight_cycle = EINA_FALSE;
			ret = elm_access_action(edje, action, &info);
			DbgPrint("ACCESS_HIGHLIGHT_NEXT, returns %d\n", ret);
			ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_LAST : LB_ACCESS_STATUS_DONE;
		} else if ((event_type & LB_SCRIPT_ACCESS_HIGHLIGHT_PREV) == LB_SCRIPT_ACCESS_HIGHLIGHT_PREV) {
			action = ELM_ACCESS_ACTION_HIGHLIGHT_PREV;
			info.highlight_cycle = EINA_FALSE;
			ret = elm_access_action(edje, action, &info);
			DbgPrint("ACCESS_HIGHLIGHT_PREV, returns %d\n", ret);
			ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_FIRST : LB_ACCESS_STATUS_DONE;
		} else if ((event_type & LB_SCRIPT_ACCESS_ACTIVATE) == LB_SCRIPT_ACCESS_ACTIVATE) {
			action = ELM_ACCESS_ACTION_ACTIVATE;
			ret = elm_access_action(edje, action, &info);
			DbgPrint("ACCESS_ACTIVATE, returns %d\n", ret);
			ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_ERROR : LB_ACCESS_STATUS_DONE;
		} else if ((event_type & LB_SCRIPT_ACCESS_ACTION) == LB_SCRIPT_ACCESS_ACTION) {
			if (down == 0) {
				action = ELM_ACCESS_ACTION_UP;
				ret = elm_access_action(edje, action, &info);
				DbgPrint("ACCESS_ACTION(%d), returns %d\n", down, ret);
				ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_ERROR : LB_ACCESS_STATUS_DONE;
			} else if (down == 1) {
				action = ELM_ACCESS_ACTION_DOWN;
				ret = elm_access_action(edje, action, &info);
				DbgPrint("ACCESS_ACTION(%d), returns %d\n", down, ret);
				ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_ERROR : LB_ACCESS_STATUS_DONE;
			} else {
				ErrPrint("Invalid access event\n");
				ret = LB_ACCESS_STATUS_ERROR;
			}
		} else if ((event_type & LB_SCRIPT_ACCESS_SCROLL) == LB_SCRIPT_ACCESS_SCROLL) {
			action = ELM_ACCESS_ACTION_SCROLL;
			info.x = x;
			info.y = y;
			switch (down) {
			case 0:
				info.mouse_type = 0;
				ret = elm_access_action(edje, action, &info);
				DbgPrint("ACCESS_HIGHLIGHT_SCROLL, returns %d\n", ret);
				ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_ERROR : LB_ACCESS_STATUS_DONE;
				break;
			case -1:
				info.mouse_type = 1;
				ret = elm_access_action(edje, action, &info);
				DbgPrint("ACCESS_HIGHLIGHT_SCROLL, returns %d\n", ret);
				ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_ERROR : LB_ACCESS_STATUS_DONE;
				break;
			case 1:
				info.mouse_type = 2;
				ret = elm_access_action(edje, action, &info);
				DbgPrint("ACCESS_HIGHLIGHT_SCROLL, returns %d\n", ret);
				ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_ERROR : LB_ACCESS_STATUS_DONE;
				break;
			default:
				ret = LB_ACCESS_STATUS_ERROR;
				break;
			}
		} else if ((event_type & LB_SCRIPT_ACCESS_UNHIGHLIGHT) == LB_SCRIPT_ACCESS_UNHIGHLIGHT) {
			action = ELM_ACCESS_ACTION_UNHIGHLIGHT;
			ret = elm_access_action(edje, action, &info);
			DbgPrint("ACCESS_UNHIGHLIGHT, returns %d\n", ret);
			ret = (ret == EINA_FALSE) ? LB_ACCESS_STATUS_ERROR : LB_ACCESS_STATUS_DONE;
		} else {
			DbgPrint("Invalid event\n");
			ret = LB_ACCESS_STATUS_ERROR;
		}

	} else if (event_type & LB_SCRIPT_MOUSE_EVENT) {
		double cur_timestamp;

#if defined(_USE_ECORE_TIME_GET)
		cur_timestamp = ecore_time_get();
#else
		struct timeval tv;
		if (gettimeofday(&tv, NULL) < 0) {
			ErrPrint("Failed to get time\n");
			cur_timestamp = 0.0f;
		} else {
			cur_timestamp = (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0f);
		}
#endif
		if (cur_timestamp - timestamp > 0.1f && handle->is_mouse_down == 0) {
			DbgPrint("Discard lazy event : %lf\n", cur_timestamp - timestamp);
			return LB_STATUS_SUCCESS;
		}

		switch (event_type) {
		case LB_SCRIPT_MOUSE_DOWN:
			if (handle->is_mouse_down == 0) {
				evas_event_feed_mouse_move(e, x, y, timestamp * 1000, NULL);
				evas_event_feed_mouse_down(e, 1, EVAS_BUTTON_NONE, (timestamp + 0.01f) * 1000, NULL);
				handle->is_mouse_down = 1;
			}
			break;
		case LB_SCRIPT_MOUSE_MOVE:
			evas_event_feed_mouse_move(e, x, y, timestamp * 1000, NULL);
			break;
		case LB_SCRIPT_MOUSE_UP:
			if (handle->is_mouse_down == 1) {
				evas_event_feed_mouse_move(e, x, y, timestamp * 1000, NULL);
				evas_event_feed_mouse_up(e, 1, EVAS_BUTTON_NONE, (timestamp + 0.01f) * 1000, NULL);
				handle->is_mouse_down = 0;
			}
			break;
		case LB_SCRIPT_MOUSE_IN:
			evas_event_feed_mouse_in(e, timestamp * 1000, NULL);
			break;
		case LB_SCRIPT_MOUSE_OUT:
			evas_event_feed_mouse_out(e, timestamp * 1000, NULL);
			break;
		default:
			return LB_STATUS_ERROR_INVALID;
		}
	} else if (event_type & LB_SCRIPT_KEY_EVENT) {
		DbgPrint("Key event is not implemented\n");
		return LB_STATUS_ERROR_NOT_IMPLEMENTED;
	}

	return ret;
}

PUBLIC int script_update_script(void *h, Evas *e, const char *src_id, const char *target_id, const char *part, const char *path, const char *group)
{
	struct info *handle = h;
	Evas_Object *edje;
	Evas_Object *obj;
	struct obj_info *obj_info;
	struct child *child;
	char _target_id[32];

	edje = find_edje(handle, src_id);
	if (!edje) {
		ErrPrint("Edje is not exists (%s)\n", src_id);
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
			if (child->obj != obj) {
				continue;
			}

			obj_info->children = eina_list_remove(obj_info->children, child);

			free(child->part);
			free(child);
			break;
		}

		DbgPrint("delete object %s %p\n", part, obj);
		/*!
		 * \note
		 * This will call the edje_del_cb.
		 * It will delete all access objects
		 */
		evas_object_del(obj);
	}

	if (!path || !strlen(path) || access(path, R_OK) != 0) {
		DbgPrint("SKIP - Path: [%s]\n", path);
		return LB_STATUS_SUCCESS;
	}

	if (!target_id) {
		if (find_edje(handle, part)) {
			double timestamp;

			do {
#if defined(_USE_ECORE_TIME_GET)
				timestamp = ecore_time_get();
#else
				struct timeval tv;
				if (gettimeofday(&tv, NULL) < 0) {
					static int local_idx = 0;
					timestamp = (double)(local_idx++);
				} else {
					timestamp = (double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0f);
				}
#endif

				snprintf(_target_id, sizeof(_target_id), "%lf", timestamp);
			} while (find_edje(handle, _target_id));

			target_id = _target_id;
		} else {
			target_id = part;
		}

		DbgPrint("Anonymouse target id: %s\n", target_id);
	}

	obj = elm_layout_add(edje);
	if (!obj) {
		ErrPrint("Failed to add a new edje object\n");
		return LB_STATUS_ERROR_FAULT;
	}

	edje_object_scale_set(elm_layout_edje_get(obj), elm_config_scale_get());

	if (!elm_layout_file_set(obj, path, group)) {
 		int err;
		err = edje_object_load_error_get(elm_layout_edje_get(obj));
		ErrPrint("Could not load %s from %s: %s\n", group, path, edje_load_error_str(err));
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

	obj_info->parent = edje;

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

	edje = find_edje(handle, id);
	if (!edje) {
		return LB_STATUS_ERROR_NOT_EXIST;
	}

	elm_object_signal_emit(edje, signal, part);
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_update_drag(void *h, Evas *e, const char *id, const char *part, double x, double y)
{
	struct info *handle = h;
	Evas_Object *edje;

	edje = find_edje(handle, id);
	if (!edje) {
		return LB_STATUS_ERROR_NOT_EXIST;
	}

	edje_object_part_drag_value_set(elm_layout_edje_get(edje), part, x, y);
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_update_size(void *han, Evas *e, const char *id, int w, int h)
{
	struct info *handle = han;
	Evas_Object *edje;

	edje = find_edje(handle, id);
	if (!edje) {
		return LB_STATUS_ERROR_NOT_EXIST;
	}

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

	if (handle->category) {
		free(handle->category);
		handle->category = NULL;
	}

	if (!category) {
		return LB_STATUS_SUCCESS;
	}

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

	s_info.handle_list = eina_list_append(s_info.handle_list, handle);

	return handle;
}

PUBLIC int script_destroy(void *_handle)
{
	struct info *handle;
	Evas_Object *edje;

	handle = _handle;

	if (!eina_list_data_find(s_info.handle_list, handle)) {
		DbgPrint("Not found (already deleted?)\n");
		return LB_STATUS_ERROR_NOT_EXIST;
	}

	s_info.handle_list = eina_list_remove(s_info.handle_list, handle);

	edje = eina_list_nth(handle->obj_list, 0);
	if (edje) {
		evas_object_del(edje);
	}

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

	obj_info->parent = evas_object_rectangle_add(e);
	if (!obj_info->parent) {
		ErrPrint("Unable to create a parent box\n");
		free(obj_info);
		return LB_STATUS_ERROR_FAULT;
	}

	edje = elm_layout_add(obj_info->parent);
	if (!edje) {
		ErrPrint("Failed to create an edje object\n");
		evas_object_del(obj_info->parent);
		free(obj_info);
		return LB_STATUS_ERROR_FAULT;
	}

	edje_object_scale_set(elm_layout_edje_get(edje), elm_config_scale_get());

	if (!elm_layout_file_set(edje, handle->file, handle->group)) {
 		int err;

		err = edje_object_load_error_get(elm_layout_edje_get(edje));
		ErrPrint("Could not load %s from %s: %s\n", handle->group, handle->file, edje_load_error_str(err));
		evas_object_del(edje);
		evas_object_del(obj_info->parent);
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
	Evas_Object *parent = NULL;

	handle = _handle;

	edje = eina_list_nth(handle->obj_list, 0);
	if (edje) {
		struct obj_info *obj_info;

		obj_info = evas_object_data_get(edje, "obj_info");
		if (obj_info) {
			parent = obj_info->parent;
		}
		evas_object_del(edje);
	}

	if (parent) {
		DbgPrint("Delete parent box\n");
		evas_object_del(parent);
	}

	handle->e = NULL;
	return LB_STATUS_SUCCESS;
}

static void access_cb(keynode_t *node, void *user_data)
{
	int state;

	if (!node) {
		if (vconf_get_bool(VCONFKEY_SETAPPL_ACCESSIBILITY_TTS, &state) != 0) {
			ErrPrint("Idle lock state is not valid\n");
			state = 0; /* DISABLED */
		}
	} else {
		state = vconf_keynode_get_bool(node);
	}

	DbgPrint("ELM CONFIG ACCESS: %d\n", state);
	elm_config_access_set(state);
}

static void update_font_cb(void *data)
{
	elm_config_font_overlay_set(TEXT_CLASS, s_info.font_name, DEFAULT_FONT_SIZE);
	DbgPrint("Update text class %s (%s, %d)\n", TEXT_CLASS, s_info.font_name, DEFAULT_FONT_SIZE);
}

static void font_changed_cb(keynode_t *node, void *user_data)
{
	char *font_name;

	if (s_info.font_name) {
		font_name = vconf_get_str("db/setting/accessibility/font_name");
		if (!font_name) {
			ErrPrint("Invalid font name (NULL)\n");
			return;
		}

		if (!strcmp(s_info.font_name, font_name)) {
			DbgPrint("Font is not changed (Old: %s(%p) <> New: %s(%p))\n", s_info.font_name, s_info.font_name, font_name, font_name);
			free(font_name);
			return;
		}

		DbgPrint("Release old font name: %s(%p)\n", s_info.font_name, s_info.font_name);
		free(s_info.font_name);
	} else {
		int ret;

		/*!
		 * Get the first font name using system_settings API.
		 */
		font_name = NULL;
		ret = system_settings_get_value_string(SYSTEM_SETTINGS_KEY_FONT_TYPE, &font_name);
		if (ret != SYSTEM_SETTINGS_ERROR_NONE || !font_name) {
			ErrPrint("System setting get: %d, font_name[%p]\n", ret, font_name);
			return;
		}
	}

	s_info.font_name = font_name;
	DbgPrint("Font name is changed to %s(%p)\n", s_info.font_name, s_info.font_name);

	/*!
	 * \NOTE
	 * Try to update all liveboxes
	 */
	update_font_cb(NULL);
}

static inline int convert_font_size(int size)
{
	switch (size) {
	case SYSTEM_SETTINGS_FONT_SIZE_SMALL:
		size = -80;
		break;
	case SYSTEM_SETTINGS_FONT_SIZE_NORMAL:
		size = -100;
		break;
	case SYSTEM_SETTINGS_FONT_SIZE_LARGE:
		size = -150;
		break;
	case SYSTEM_SETTINGS_FONT_SIZE_HUGE:
		size = -190;
		break;
	case SYSTEM_SETTINGS_FONT_SIZE_GIANT:
		size = -250;
		break;
	default:
		size = -100;
		break;
	}

	DbgPrint("Return size: %d\n", size);
	return size;
}

static void font_size_cb(system_settings_key_e key, void *user_data)
{
	int size;

	if (system_settings_get_value_int(SYSTEM_SETTINGS_KEY_FONT_SIZE, &size) != SYSTEM_SETTINGS_ERROR_NONE) {
		return;
	}

	size = convert_font_size(size);

	if (size == s_info.font_size) {
		DbgPrint("Font size is not changed\n");
		return;
	}

	s_info.font_size = size;
	DbgPrint("Font size is changed to %d, but don't update the font info\n", size);
}

PUBLIC int script_init(double scale)
{
	int ret;
	char *argv[] = {
		"livebox.edje",
		NULL,
	};

	/* ecore is already initialized */
	elm_init(1, argv);
	elm_config_scale_set(scale);
	DbgPrint("Scale is updated: %lf\n", scale);

	ret = vconf_notify_key_changed(VCONFKEY_SETAPPL_ACCESSIBILITY_TTS, access_cb, NULL);
	DbgPrint("TTS changed: %d\n", ret);

	ret = vconf_notify_key_changed("db/setting/accessibility/font_name", font_changed_cb, NULL);
	DbgPrint("System font is changed: %d\n", ret);
	
	ret = system_settings_set_changed_cb(SYSTEM_SETTINGS_KEY_FONT_SIZE, font_size_cb, NULL);
	DbgPrint("System font size is changed: %d\n", ret);

	access_cb(NULL, NULL);
	font_changed_cb(NULL, NULL);
	font_size_cb(SYSTEM_SETTINGS_KEY_FONT_SIZE, NULL);
	return LB_STATUS_SUCCESS;
}

PUBLIC int script_fini(void)
{
	int ret;
	Eina_List *l;
	Eina_List *n;
	struct info *handle;

	EINA_LIST_FOREACH_SAFE(s_info.handle_list, l, n, handle) {
		script_destroy(handle);
	}

	ret = system_settings_unset_changed_cb(SYSTEM_SETTINGS_KEY_FONT_SIZE);
	DbgPrint("Unset font size change event callback: %d\n", ret);

	ret = vconf_ignore_key_changed("db/setting/accessibility/font_name", font_changed_cb);
	DbgPrint("Unset font name change event callback: %d\n", ret);

	ret = vconf_ignore_key_changed(VCONFKEY_SETAPPL_ACCESSIBILITY_TTS, access_cb);
	DbgPrint("Unset tts: %d\n", ret);

	elm_shutdown();

	free(s_info.font_name);
	s_info.font_name = NULL;
	return LB_STATUS_SUCCESS;
}

/* End of a file */
