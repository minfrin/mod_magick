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
 * The Apache mod_magick_format module provides a filter that sets the
 * output format of an image read by mod_magick.
 *
 *  Author: Graham Leggett
 *
 * Basic configuration:
 *
 * <Location />
 *   <If "%{QUERY_STRING} =~ /./">
 *     SetOutputFilter MAGICK_FORMAT
 *     MagickFormat PNG
 *   </If>
 * </Location>
 *
 * The MagickFormat directive sets the output format to be used. The list
 * of supported formats can be found in the manual of the GraphicsMagick
 * 'gm' command.
 */

#include <apr_strings.h>

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "ap_expr.h"
#include "util_filter.h"

#include "mod_magick.h"

module AP_MODULE_DECLARE_DATA magick_format_module;

typedef struct magick_conf {
    int format_set:1; /* has the format been set */
    ap_expr_info_t *format;  /* set to format */
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

    new->format = (add->format_set == 0) ? base->format : add->format;
    new->format_set = add->format_set || base->format_set;

    return new;
}

static const char *set_magick_format(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    conf->format = ap_expr_parse_cmd(cmd, arg, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", arg, "': ",
                expr_err, NULL);
    }

    conf->format_set = 1;

    return NULL;
}

static const command_rec magick_cmds[] = {
    AP_INIT_TAKE1("MagickFormat", set_magick_format, NULL, ACCESS_CONF | OR_ALL,
        "Set the format of the output image"), { NULL },
};

static apr_status_t magick_format_out_filter(ap_filter_t *f, apr_bucket_brigade *bb)
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
                    &magick_format_module);

            ap_bucket_magick *m = e->data;

            const char *format;
            char *mime;

            if (conf->format) {
                const char *err = NULL;

                format = ap_expr_str_exec(f->r, conf->format, &err);
                if (err) {
                    ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                    "Failure while evaluating the format expression for '%s', "
                                    "format ignored: %s", f->r->uri, err);
                    continue;
                }
            }
            else {
                ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, f->r,
                                "No format expression for '%s', "
                                "format ignored", f->r->uri);
                continue;
            }

            if (!MagickSetImageFormat(m->wand, format)) {
                char *description;
                ExceptionType severity;

                description = MagickGetException(m->wand, &severity);
                ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, f->r,
                        "MagickSetImageFormat: %s (severity %d)", description,
                        severity);
                MagickRelinquishMemory(description);

                return APR_EGENERAL;
            }

            mime = MagickToMime(format);
            ap_set_content_type(f->r, apr_pstrdup(f->r->pool, mime));
            MagickRelinquishMemory(mime);

        }

    }

    return ap_pass_brigade(f->next, bb);
}


static void register_hooks(apr_pool_t *p)
{
    ap_register_output_filter("MAGICK_FORMAT", magick_format_out_filter, NULL,
            AP_FTYPE_CONTENT_SET);
}

AP_DECLARE_MODULE(magick_format) =
{
    STANDARD20_MODULE_STUFF,
    create_magick_dir_config, /* dir config creater */
    merge_magick_dir_config,  /* dir merger --- default is to override */
    NULL,                     /* server config */
    NULL,                     /* merge server config */
    magick_cmds,              /* command apr_table_t */
    register_hooks            /* register hooks */
};
