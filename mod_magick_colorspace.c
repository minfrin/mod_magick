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
 * The Apache mod_magick_colorspace module provides a filter that sets the
 * colorspace type of the image generated by mod_magick.
 *
 *  Author: Graham Leggett
 *
 * Basic configuration:
 *
 * <Location />
 *   <If "%{QUERY_STRING} =~ /./">
 *     SetOutputFilter MAGICK_COLORSPACE
 *     MagickColorspace srgb
 *   </If>
 * </Location>
 *
 * The MagickColorspace directive sets the colorspace to be used by the
 * output image.
 *
 * Possible values are:
 *
 * cmyk|gray|hsl|hwb|ohta|rgb|srgb|transparent|xyz|ycbcr|ycc|yiq|ypbpr|yuv
 *
 * The default value is srgb.
 */

#include <apr_strings.h>

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "ap_expr.h"
#include "util_filter.h"

#include "mod_magick.h"

module AP_MODULE_DECLARE_DATA magick_colorspace_module;

#define DEFAULT_COLORSPACE_TYPE sRGBColorspace

typedef struct magick_conf {
    int colorspace_set:1; /* have the colorspace been set */
    ap_expr_info_t *colorspace;  /* resize to colorspace */
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

    new->colorspace = (add->colorspace_set == 0) ? base->colorspace : add->colorspace;
    new->colorspace_set = add->colorspace_set || base->colorspace_set;

    return new;
}

static const char *set_magick_colorspace(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    conf->colorspace = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    conf->colorspace_set = 1;

    return NULL;
}

static const command_rec magick_cmds[] = {
    AP_INIT_TAKE1("MagickColorspace", set_magick_colorspace, NULL, ACCESS_CONF | OR_ALL,
        "Set the colorspace type used to render the image. Must be one of "
        "cmyk|gray|hsl|hwb|ohta|rgb|srgb|transparent|xyz|ycbcr|ycc|yiq|ypbpr|yuv. "
    	"Default is 'srgb'."
        ), { NULL },
};

static ColorspaceType magick_parse_colorspace_type(const char *colorspace)
{

    switch (colorspace[0]) {
    case 'c': {
        if (!strcmp(colorspace, "cmyk")) {
            return CMYKColorspace;
        }
        break;
    }
    case 'g': {
        if (!strcmp(colorspace, "gray")) {
            return GRAYColorspace;
        }
        break;
    }
    case 'h': {
        if (!strcmp(colorspace, "hsl")) {
            return HSLColorspace;
        }
        else if (!strcmp(colorspace, "hwb")) {
            return HWBColorspace;
        }
        break;
    }
    case 'o': {
        if (!strcmp(colorspace, "ohta")) {
            return OHTAColorspace;
        }
        break;
    }
    case 'r': {
        if (!strcmp(colorspace, "rgb")) {
            return RGBColorspace;
        }
        break;
    }
    case 's': {
        if (!strcmp(colorspace, "srgb")) {
            return sRGBColorspace;
        }
        break;
    }
    case 't': {
        if (!strcmp(colorspace, "transparent")) {
            return TransparentColorspace;
        }
        break;
    }
    case 'x': {
        if (!strcmp(colorspace, "xyz")) {
            return XYZColorspace;
        }
        break;
    }
    case 'y': {
        if (!strcmp(colorspace, "ycbcr")) {
            return YCbCrColorspace;
        }
        else if (!strcmp(colorspace, "ycc")) {
            return YCCColorspace;
        }
        else if (!strcmp(colorspace, "yiq")) {
            return YIQColorspace;
        }
        else if (!strcmp(colorspace, "ypbpr")) {
        	return YPbPrColorspace;
        }
        else if (!strcmp(colorspace, "yuv")) {
            return YUVColorspace;
        }
        break;
    }
    }

    return UndefinedColorspace;
}

static apr_status_t magick_colorspace_out_filter(ap_filter_t *f, apr_bucket_brigade *bb)
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
                    &magick_colorspace_module);

            ap_bucket_magick *m = e->data;

            ColorspaceType colorspace = DEFAULT_COLORSPACE_TYPE;

            if (conf->colorspace) {
                const char *err = NULL, *str;

                str = ap_expr_str_exec(f->r, conf->colorspace, &err);
                if (err) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                    "Failure while evaluating the colorspace type expression for '%s', "
                                    "colorspace ignored: %s", f->r->uri, err);
                }
                else {
                    colorspace = magick_parse_colorspace_type(str);
                    if (colorspace == UndefinedColorspace) {
                        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                      "Colorspace type for '%s' of '%s' not recognised, "
                                      "must be one of cmyk|gray|hsl|hwb|ohta|rgb|srgb|transparent|xyz|ycbcr|ycc|yiq|ypbpr|yuv"
                                      ", using 'srgb'", f->r->uri, str);
                    }
                }
            }

            if (!MagickSetImageColorspace(m->wand, colorspace)) {
                char *description;
                ExceptionType severity;

                description = MagickGetException(m->wand, &severity);
                ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, f->r,
                        "MagickSetImageColorspace: %s (severity %d)", description,
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
    ap_register_output_filter("MAGICK_COLORSPACE", magick_colorspace_out_filter, NULL,
            AP_FTYPE_CONTENT_SET);
}

AP_DECLARE_MODULE(magick_colorspace) =
{
    STANDARD20_MODULE_STUFF,
    create_magick_dir_config, /* dir config creater */
    merge_magick_dir_config,  /* dir merger --- default is to override */
    NULL,                     /* server config */
    NULL,                     /* merge server config */
    magick_cmds,              /* command apr_table_t */
    register_hooks            /* register hooks */
};
