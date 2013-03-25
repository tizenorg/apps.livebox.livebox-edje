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

#ifdef __cplusplus
extern "C" {
#endif

/*!
 */
extern int script_signal_emit(Evas *e, const char *part, const char *signal, double x, double y, double ex, double ey);

/*!
 * Implement below functions
 */
extern int script_update_color(void *h, Evas *e, const char *id, const char *part, const char *rgba);
extern int script_update_text(void *h, Evas *e, const char *id, const char *part, const char *text);
extern int script_update_image(void *h, Evas *e, const char *id, const char *part, const char *path, const char *option);
extern int script_update_script(void *h, Evas *e, const char *src_id, const char *target_id, const char *part, const char *path, const char *group);
extern int script_update_signal(void *h, Evas *e, const char *id, const char *part, const char *signal);
extern int script_update_drag(void *h, Evas *e, const char *id, const char *part, double x, double y);
extern int script_update_size(void *handle, Evas *e, const char *id, int w, int h);
extern int script_update_category(void *h, Evas *e, const char *id, const char *category);

extern void *script_create(const char *file, const char *group);
extern int script_destroy(void *handle);

extern int script_load(void *handle, Evas *e, int w, int h);
extern int script_unload(void *handle, Evas *e);

extern int script_init(void);
extern int script_fini(void);

extern const char *script_magic_id(void);

#ifdef __cplusplus
}
#endif

/* End of a file */
