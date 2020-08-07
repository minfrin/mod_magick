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
 * The Apache mod_magick_strip module provides a filter that strips all
 * image metadata from an image read by mod_magick.
 *
 *  Author: Graham Leggett
 *
 * Basic configuration:
 *
 * <Location />
 *   <IfModule magick_strip_module>
 *     <If "%{QUERY_STRING} =~ /./">
 *       SetOutputFilter MAGICK;MAGICK_STRIP
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
#include "util_filter.h"

#include "mod_magick.h"

module AP_MODULE_DECLARE_DATA magick_strip_module;

static apr_status_t magick_strip_out_filter(ap_filter_t *f, apr_bucket_brigade *bb)
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

            ap_bucket_magick *m = e->data;

            if (!MagickStripImage(m->wand)) {
                char *description;
                ExceptionType severity;

                description = MagickGetException(m->wand, &severity);
                ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, f->r,
                        "MagickStripImage: %s (severity %d)", description,
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
    ap_register_output_filter("MAGICK_STRIP", magick_strip_out_filter, NULL,
            AP_FTYPE_CONTENT_SET);
}

AP_DECLARE_MODULE(magick_strip) =
{
    STANDARD20_MODULE_STUFF,
    NULL,                     /* dir config creater */
    NULL,                     /* dir merger --- default is to override */
    NULL,                     /* server config */
    NULL,                     /* merge server config */
    NULL,                     /* command apr_table_t */
    register_hooks            /* register hooks */
};
