/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * The Apache mod_magick_resize module provides a filter that resizes an
 * image read by mod_magick.
 *
 *  Author: Graham Leggett
 *
 * Basic configuration:
 *
 * <Location />
 *   <IfModule magick_resize_module>
 *     <If "%{QUERY_STRING} =~ /./">
 *       SetOutputFilter MAGICK_RESIZE
 *     </If>
 *   </IfModule>
 * </Location>
 *
 */

#include <apr_strings.h>

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "ap_expr.h"

#include "mod_magick.h"

module AP_MODULE_DECLARE_DATA magick_resize_module;

#define DEFAULT_FILTER_TYPE CubicFilter

typedef struct magick_conf {
    int columns_set:1; /* have the columns been set */
    int rows_set:1; /* have the rows been set */
    int filter_type_set:1; /* has the filter been set */
    int blur_set:1; /* has the blur been set */
    ap_expr_info_t *columns;  /* resize to columns */
    ap_expr_info_t *rows; /* resize to rows */
    ap_expr_info_t *filter_type; /* resize filter type */
    ap_expr_info_t *blur; /* resize blur */
} magick_conf;

static void *create_magick_dir_config(apr_pool_t *p, char *dummy)
{
    magick_conf *new = (magick_conf *) apr_pcalloc(p, sizeof(magick_conf));

    return (void *) new;
}

static void *merge_magick_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    magick_conf *new = (magick_conf *) apr_pcalloc(p, sizeof(magick_conf));
    magick_conf *add = (magick_conf *) addv;
    magick_conf *base = (magick_conf *) basev;

    new->rows = (add->rows_set == 0) ? base->rows : add->rows;
    new->rows_set = add->rows_set || base->rows_set;

    new->columns = (add->columns_set == 0) ? base->columns : add->columns;
    new->columns_set = add->columns_set || base->columns_set;

    new->filter_type = (add->filter_type_set == 0) ? base->filter_type : add->filter_type;
    new->filter_type_set = add->filter_type_set || base->filter_type_set;

    new->blur = (add->blur_set == 0) ? base->blur : add->blur;
    new->blur_set = add->blur_set || base->blur_set;

    return new;
}

static const char *set_magick_columns(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    conf->columns = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    conf->columns_set = 1;

    return NULL;
}

static const char *set_magick_rows(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    conf->rows = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    conf->rows_set = 1;

    return NULL;
}

static const char *set_magick_filter_type(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    conf->filter_type = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    conf->filter_type_set = 1;

    return NULL;
}

static const char *set_magick_blur(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    conf->blur = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    conf->blur_set = 1;

    return NULL;
}

static const command_rec magick_cmds[] = {
    AP_INIT_TAKE1("MagickResizeColumns", set_magick_columns, NULL, ACCESS_CONF,
        "Set the number of columns in the resized image"),
    AP_INIT_TAKE1("MagickResizeRows", set_magick_rows, NULL, ACCESS_CONF,
        "Set the number of rows in the resized image"),
    AP_INIT_TAKE1("MagickResizeFilterType", set_magick_filter_type, NULL, ACCESS_CONF,
        "Set the filter type used to resize the image. Must be one of bessel|blackman|box|catrom|"
        "cubic|gaussian|hamming|hanning|hermite|lanczos|mitchell|point|"
        "quadratic|sinc|triangle"),
    AP_INIT_TAKE1("MagickResizeBlur", set_magick_blur, NULL, ACCESS_CONF,
        "Set the blur used to resize the image"), { NULL },
};

static FilterTypes magick_parse_filter_type(const char *filter_type)
{

    switch (filter_type[0]) {
    case 'b': {
        if (!strcmp(filter_type, "bessel")) {
            return BesselFilter;
        }
        else if (!strcmp(filter_type, "blackman")) {
            return BlackmanFilter;
        }
        else if (!strcmp(filter_type, "box")) {
            return BoxFilter;
        }
        break;
    }
    case 'c': {
        if (!strcmp(filter_type, "catrom")) {
            return CatromFilter;
        }
        else if (!strcmp(filter_type, "cubic")) {
            return CubicFilter;
        }
        break;
    }
    case 'g': {
        if (!strcmp(filter_type, "gaussian")) {
            return GaussianFilter;
        }
        break;
    }
    case 'h': {
        if (!strcmp(filter_type, "hamming")) {
            return HammingFilter;
        }
        else if (!strcmp(filter_type, "hanning")) {
            return HanningFilter;
        }
        else if (!strcmp(filter_type, "hermite")) {
            return HermiteFilter;
        }
        break;
    }
    case 'l': {
        if (!strcmp(filter_type, "lanczos")) {
            return LanczosFilter;
        }
        break;
    }
    case 'm': {
        if (!strcmp(filter_type, "mitchell")) {
            return MitchellFilter;
        }
        break;
    }
    case 'p': {
        if (!strcmp(filter_type, "point")) {
            return PointFilter;
        }
        break;
    }
    case 'q': {
        if (!strcmp(filter_type, "quadratic")) {
            return QuadraticFilter;
        }
        break;
    }
    case 's': {
        if (!strcmp(filter_type, "sinc")) {
            return SincFilter;
        }
        break;
    }
    case 't': {
        if (!strcmp(filter_type, "triangle")) {
            return TriangleFilter;
        }
        break;
    }
    }

    return UndefinedFilter;
}

static apr_status_t magick_resize_out_filter(ap_filter_t *f, apr_bucket_brigade *bb)
{
    apr_bucket *e;

    apr_status_t rv = APR_SUCCESS;

    /* Do nothing if asked to filter nothing. */
    if (APR_BRIGADE_EMPTY(bb)) {
        return ap_pass_brigade(f->next, bb);
    }

    for (e = APR_BRIGADE_FIRST(bb);
         e != APR_BRIGADE_SENTINEL(bb);
         e = APR_BUCKET_NEXT(e))
    {

        /* EOS means we are done. */
        if (APR_BUCKET_IS_EOS(e)) {
            ap_remove_output_filter(f);
            rv = ap_pass_brigade(f->next, bb);
            continue;
        }

        /* Magick bucket? */
        if (AP_BUCKET_IS_MAGICK(e)) {

            magick_conf *conf = ap_get_module_config(f->r->per_dir_config,
                    &magick_resize_module);

            ap_bucket_magick *m = e->data;

            unsigned long columns = 0;
            unsigned long rows = 0;
            FilterTypes filter_type = DEFAULT_FILTER_TYPE;
            double blur = 1;

            if (conf->columns) {
                const char *err = NULL, *str;

                str = ap_expr_str_exec(f->r, conf->columns, &err);
                if (err) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                    "Failure while evaluating the columns expression for '%s', "
                                    "columns ignored: %s", f->r->uri, err);
                }
                else {
                    columns = apr_atoi64(str);
                    if (errno == ERANGE) {
                        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                      "Columns expression for '%s' out of range, "
                                      "columns ignored: %s", f->r->uri, str);
                    }
                }
            }

            if (conf->rows) {
                const char *err = NULL, *str;

                str = ap_expr_str_exec(f->r, conf->rows, &err);
                if (err) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                    "Failure while evaluating the rows expression for '%s', "
                                    "rows ignored: %s", f->r->uri, err);
                }
                else {
                    rows = apr_atoi64(str);
                    if (errno == ERANGE) {
                        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                      "Rows expression for '%s' out of range, "
                                      "rows ignored: %s", f->r->uri, str);
                    }
                }
            }

            if (conf->filter_type) {
                const char *err = NULL, *str;

                str = ap_expr_str_exec(f->r, conf->filter_type, &err);
                if (err) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                    "Failure while evaluating the filter type expression for '%s', "
                                    "filter type ignored: %s", f->r->uri, err);
                }
                else {
                    filter_type = magick_parse_filter_type(str);
                    if (filter_type == UndefinedFilter) {
                        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                      "Filter type for '%s' of '%s' not recognised, "
                                      "must be one of bessel|blackman|box|catrom|"
                                      "cubic|gaussian|hamming|hanning|hermite|lanczos|mitchell|point|"
                                      "quadratic|sinc|triangle, using 'cubic'", f->r->uri, str);
                    }
                }
            }

            if (conf->blur) {
                const char *err = NULL, *str;
                char *end;

                str = ap_expr_str_exec(f->r, conf->blur, &err);
                if (err) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                    "Failure while evaluating the blur expression for '%s', "
                                    "blur ignored: %s", f->r->uri, err);
                }
                else {
                    blur = strtod(str, &end);
                    if (errno == ERANGE) {
                        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                      "Blur expression for '%s' out of range, "
                                      "blur ignored: %s", f->r->uri, str);
                    }
                }
            }

            if (columns == 0 && rows == 0) {
                /* no resize requested, do nothing */
                continue;
            }
            if (columns == 0) {
                columns = ((unsigned long long) (rows
                        * MagickGetImageWidth(m->wand))) / MagickGetImageHeight(
                                m->wand);
            }
            else if (rows == 0) {
                rows = ((unsigned long long) (columns
                        * MagickGetImageHeight(m->wand))) / MagickGetImageWidth(
                                m->wand);
            }
            if (!MagickResizeImage(m->wand, columns, rows,
                    filter_type, blur)) {
                char *description;
                ExceptionType severity;

                description = MagickGetException(m->wand, &severity);
                ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, f->r,
                        "MagickResizeImage: %s (severity %d)", description,
                        severity);
                MagickRelinquishMemory(description);

                return APR_EGENERAL;
            }

        }

    }

    return rv;

}


static void register_hooks(apr_pool_t *p)
{
    ap_register_output_filter("MAGICK_RESIZE", magick_resize_out_filter, NULL,
            AP_FTYPE_CONTENT_SET);
}

AP_DECLARE_MODULE(magick_resize) =
{
    STANDARD20_MODULE_STUFF,
    create_magick_dir_config, /* dir config creater */
    merge_magick_dir_config,  /* dir merger --- default is to override */
    NULL,                     /* server config */
    NULL,                     /* merge server config */
    magick_cmds,              /* command apr_table_t */
    register_hooks            /* register hooks */
};
