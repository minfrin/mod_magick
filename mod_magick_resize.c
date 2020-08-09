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
 *   <Location />
 *     <IfModule magick_resize_module>
 *       <If "%{QUERY_STRING} =~ /./">
 *         SetOutputFilter MAGICK_RESIZE
 *       </If>
 *     </IfModule>
 *   </Location>
 *
 * All resize directives take on a list of expressions, the first expression
 * to return a valid value wins. This allows support for responsive behaviour
 * such as HTTP Client Hints.
 *
 * For example, here we first take into account the HTTP Client Hint "Width"
 * header, followed by the query string, followed by the default fallback value
 * "100".
 *
 *   SetOutputFilter MAGICK;MAGICK_RESIZE
 *   <If "%{req:Width} != ''">
 *     MagickResizeColumns %{req:Width}
 *   </If>
 *   MagickResizeColumns %{QUERY_STRING} 100
 *
 * Note that in the above example, we need to include the If section to ensure
 * the Vary header is has the Width header correctly added. Apache httpd 2.4
 * has a bug where conditional expressions set the Vary header, but string
 * expressions do not. Without this, caching breaks, and you want caching.
 *
 * In the absence of the above bug, the above line should look like this:
 *
 *   #MagickResizeColumns %{req:Width} %{QUERY_STRING} 100
 *
 * In the absence of a valid fallback value, or if the fallback value is zero,
 * the original image value is maintained.
 *
 * The MagickResizeFactor can be used to apply a multiplier to the width and
 * height. For example, to use the HTTP Client Hint DPR header:
 *
 *   SetOutputFilter MAGICK;MAGICK_RESIZE
 *   <If "%{req:DPR} != ''">
 *     MagickResizeFactor %{req:DPR} 1
 *   </If>
 *
 * The MagickResizeModulus limits the possible image sizes so that caches are
 * not overwhelmed.
 */

#include <apr_strings.h>

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "ap_expr.h"
#include "util_filter.h"

#include "mod_magick.h"

module AP_MODULE_DECLARE_DATA magick_resize_module;

#define DEFAULT_FILTER_TYPE CubicFilter

typedef struct magick_conf {
    int modulus_set:1; /* has the modulus been set */
    apr_array_header_t *columns;  /* resize to columns */
    apr_array_header_t *rows; /* resize to rows */
    apr_array_header_t *filter_type; /* resize filter type */
    apr_array_header_t *blur; /* resize blur */
    apr_array_header_t *factor; /* resize scaling factor */
    apr_off_t modulus; /* the modulus to set */
} magick_conf;

static void *create_magick_dir_config(apr_pool_t *p, char *dummy)
{
    magick_conf *new = (magick_conf *) apr_pcalloc(p, sizeof(magick_conf));

    new->columns = apr_array_make(p, 2, sizeof(ap_expr_info_t *));
    new->rows = apr_array_make(p, 2, sizeof(ap_expr_info_t *));
    new->filter_type = apr_array_make(p, 2, sizeof(ap_expr_info_t *));
    new->blur = apr_array_make(p, 2, sizeof(ap_expr_info_t *));
    new->factor = apr_array_make(p, 2, sizeof(ap_expr_info_t *));
    new->modulus = 1;

    return (void *) new;
}

static void *merge_magick_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    magick_conf *new = (magick_conf *) apr_pcalloc(p, sizeof(magick_conf));
    magick_conf *add = (magick_conf *) addv;
    magick_conf *base = (magick_conf *) basev;

    new->rows = apr_array_append(p, add->rows, base->rows);
    new->columns = apr_array_append(p, add->columns, base->columns);
    new->filter_type = apr_array_append(p, add->filter_type, base->filter_type);
    new->blur = apr_array_append(p, add->blur, base->blur);
    new->factor = apr_array_append(p, add->factor, base->factor);

    new->modulus = (add->modulus_set == 0) ? base->modulus : add->modulus;
    new->modulus_set = add->modulus_set || base->modulus_set;

    return new;
}

static const char *set_magick_columns(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    ap_expr_info_t **columns = apr_array_push(conf->columns);

    *columns = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    return NULL;
}

static const char *set_magick_rows(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    ap_expr_info_t **rows = apr_array_push(conf->rows);

    *rows = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    return NULL;
}

static const char *set_magick_filter_type(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    ap_expr_info_t **filter_type = apr_array_push(conf->filter_type);

    *filter_type = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    return NULL;
}

static const char *set_magick_blur(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    ap_expr_info_t **blur = apr_array_push(conf->blur);

    *blur = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    return NULL;
}

static const char *set_magick_factor(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    ap_expr_info_t **factor = apr_array_push(conf->factor);

    *factor = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    return NULL;
}

static const char *set_magick_modulus(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;

    if (APR_SUCCESS != apr_strtoff(&(conf->modulus), arg, NULL, 10) || conf->modulus
            <= 0) {
        return "MagickResizeModulus must be greater than zero";
    }

    conf->modulus_set = 1;

    return NULL;
}

static const command_rec magick_cmds[] = {
    AP_INIT_ITERATE("MagickResizeColumns", set_magick_columns, NULL, ACCESS_CONF,
        "Set the number of columns in the resized image"),
    AP_INIT_ITERATE("MagickResizeRows", set_magick_rows, NULL, ACCESS_CONF,
        "Set the number of rows in the resized image"),
    AP_INIT_ITERATE("MagickResizeFilterType", set_magick_filter_type, NULL, ACCESS_CONF,
        "Set the filter type used to resize the image. Must be one of bessel|blackman|box|catrom|"
        "cubic|gaussian|hamming|hanning|hermite|lanczos|mitchell|point|"
        "quadratic|sinc|triangle"),
    AP_INIT_ITERATE("MagickResizeBlur", set_magick_blur, NULL, ACCESS_CONF,
        "Set the blur used to resize the image"),
    AP_INIT_ITERATE("MagickResizeFactor", set_magick_factor, NULL, ACCESS_CONF,
        "Set the factor to multiply rows and columns by, such as the Device Pixel Ratio (DPR)"),
    AP_INIT_ITERATE("MagickResizeModulus", set_magick_modulus, NULL, ACCESS_CONF,
        "Set the modulus to apply to the width and height."),
    { NULL },
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

    for (e = APR_BRIGADE_FIRST(bb);
         e != APR_BRIGADE_SENTINEL(bb);
         e = APR_BUCKET_NEXT(e))
    {

        /* EOS means we are done. */
        if (APR_BUCKET_IS_EOS(e)) {
            ap_remove_output_filter(f);
            break;
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
            double factor = 1;

            if (conf->columns) {
                const char *err = NULL, *str;
                int i;

                for (i = 0; i < conf->columns->nelts; ++i) {
                    ap_expr_info_t *expr = APR_ARRAY_IDX(conf->columns, i,
                            ap_expr_info_t *);

                    str = ap_expr_str_exec(f->r, expr, &err);
                    if (err) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Failure while evaluating the columns expression for '%s', "
                                        "column value skipped: %s", f->r->uri,
                                err);
                        continue;
                    } else if (!str || !str[strspn(str, " \t\r\n")]) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Columns expression for '%s' empty, "
                                        "row value skipped", f->r->uri);
                        continue;
                    } else {
                        columns = apr_atoi64(str);
                        if (errno == ERANGE) {
                            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                    "Columns expression for '%s' out of range, "
                                            "columns ignored: %s", f->r->uri,
                                    str);
                            columns = 0;
                            continue;
                        }
                    }
                    break;
                }
            }

            if (conf->rows) {
                const char *err = NULL, *str;
                int i;

                for (i = 0; i < conf->rows->nelts; ++i) {
                    ap_expr_info_t *expr = APR_ARRAY_IDX(conf->rows, i,
                            ap_expr_info_t *);

                    str = ap_expr_str_exec(f->r, expr, &err);
                    if (err) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Failure while evaluating the rows expression for '%s', "
                                        "row value skipped: %s", f->r->uri,
                                err);
                        continue;
                    } else if (!str || !str[strspn(str, " \t\r\n")]) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Rows expression for '%s' empty, "
                                        "row value skipped", f->r->uri);
                        continue;
                    } else {
                        rows = apr_atoi64(str);
                        if (errno == ERANGE) {
                            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                    "Rows expression for '%s' out of range, "
                                            "rows ignored: %s", f->r->uri,
                                    str);
                            rows = 0;
                            continue;
                        }
                    }
                    break;
                }
            }

            if (conf->filter_type) {
                const char *err = NULL, *str;
                int i;

                for (i = 0; i < conf->filter_type->nelts; ++i) {
                    ap_expr_info_t *expr = APR_ARRAY_IDX(conf->filter_type, i,
                            ap_expr_info_t *);

                    str = ap_expr_str_exec(f->r, expr, &err);
                    if (err) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Failure while evaluating the filtertype expression for '%s', "
                                        "filtertype value skipped: %s",
                                f->r->uri, err);
                        continue;
                    } else if (!str || !str[strspn(str, " \t\r\n")]) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Filtertype expression for '%s' empty, "
                                        "filtertype value skipped", f->r->uri);
                        continue;
                    } else {
                        filter_type = magick_parse_filter_type(str);
                        if (filter_type == UndefinedFilter) {
                            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                    "Filter type for '%s' of '%s' not recognised, "
                                            "must be one of bessel|blackman|box|catrom|"
                                            "cubic|gaussian|hamming|hanning|hermite|lanczos|mitchell|point|"
                                            "quadratic|sinc|triangle, using 'cubic'",
                                    f->r->uri, str);
                            filter_type = DEFAULT_FILTER_TYPE;
                            continue;
                        }
                    }
                    break;
                }
            }

            if (conf->blur) {
                const char *err = NULL, *str;
                char *end;
                int i;

                for (i = 0; i < conf->blur->nelts; ++i) {
                    ap_expr_info_t *expr = APR_ARRAY_IDX(conf->blur, i,
                            ap_expr_info_t *);

                    str = ap_expr_str_exec(f->r, expr, &err);
                    if (err) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Failure while evaluating the blur expression for '%s', "
                                        "blur value skipped: %s", f->r->uri,
                                err);
                        continue;
                    } else if (!str || !str[strspn(str, " \t\r\n")]) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Blur expression for '%s' empty, "
                                        "blur value skipped", f->r->uri);
                        continue;
                    } else {
                        blur = strtod(str, &end);
                        if (errno == ERANGE) {
                            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                    "Blur expression for '%s' out of range, "
                                            "blur ignored: %s", f->r->uri,
                                    str);
                            blur = 1;
                            continue;
                        }
                    }
                    break;
                }
            }

            if (conf->factor) {
                const char *err = NULL, *str;
                char *end;
                int i;

                for (i = 0; i < conf->factor->nelts; ++i) {
                    ap_expr_info_t *expr = APR_ARRAY_IDX(conf->factor, i,
                            ap_expr_info_t *);

                    str = ap_expr_str_exec(f->r, expr, &err);
                    if (err) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Failure while evaluating the factor expression for '%s', "
                                        "factor value skipped: %s", f->r->uri,
                                err);
                        continue;
                    } else if (!str || !str[strspn(str, " \t\r\n")]) {
                        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                "Factor expression for '%s' empty, "
                                        "factor value skipped", f->r->uri);
                        continue;
                    } else {
                        factor = strtod(str, &end);
                        if (errno == ERANGE) {
                            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r,
                                    "Factor expression for '%s' out of range, "
                                            "factor ignored: %s", f->r->uri,
                                    str);
                            factor = 1;
                            continue;
                        }
                    }
                    break;
                }
            }

            rows *= factor;
            columns *= factor;

            if (rows % conf->modulus) {
                rows = ((unsigned long)(rows / conf->modulus) + 1) * conf->modulus;
            }

            if (columns % conf->modulus) {
                columns = ((unsigned long)(columns / conf->modulus) + 1) * conf->modulus;
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

    return ap_pass_brigade(f->next, bb);
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
