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
 * The Apache mod_magick module provides image filtering for the Apache
 * httpd server.
 *
 *  Author: Graham Leggett
 *
 * Basic configuration:
 *
 * <Location />
 *   <IfModule magick_module>
 *     <If "%{QUERY_STRING} =~ /./">
 *       SetOutputFilter MAGICK
 *       AddMagickOption jpeg:preserve-settings true
 *     </If>
 *   </IfModule>
 * </Location>
 *
 * The MAGICK module converts a response into a magick bucket, which can be
 * transformed by specific downstream magick filters to modify the image.
 * The first filter that attempts to read the bucket will cause the output
 * image to be rendered.
 *
 * The AddMagickOption allows the setting of options that affect the
 * operation of GraphicsMagick. The options accepted are those as documented
 * under the -define option in the gm tool.
 *
 * The MagickMaxSize option sets the largest size the source image is allowed to
 * be. Beyond this size requests will be rejected to prevent the processing of
 * huge images.
 */

#include <apr.h>
#include <apr_hash.h>
#include <apr_strings.h>

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "util_filter.h"
#include "ap_expr.h"

#include "mod_magick.h"

module AP_MODULE_DECLARE_DATA magick_module;

#define DEFAULT_MAX_SIZE 10*1024*1024

typedef struct magick_conf {
    int size_set:1; /* has the size been set */
    apr_off_t size; /* maximum image size */
    apr_hash_t *options; /* options */
} magick_conf;

typedef struct magick_option {
    const char *format;  /* set to format */
    const char *key;  /* set to key */
    ap_expr_info_t *value;  /* set to value */
} magick_option;

typedef struct magick_do {
    request_rec *r;
    MagickWand *wand;
} magick_do;

typedef struct magick_ctx {
    apr_bucket_brigade *bb;
    apr_bucket_brigade *mbb;
    apr_size_t seen_bytes;
    int seen_buckets;
    int seen_eos;
} magick_ctx;


static void *create_magick_dir_config(apr_pool_t *p, char *dummy)
{
    magick_conf *new = (magick_conf *) apr_pcalloc(p, sizeof(magick_conf));

    new->size = DEFAULT_MAX_SIZE;
    new->options = apr_hash_make(p);

    return (void *) new;
}

static void *merge_magick_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    magick_conf *new = (magick_conf *) apr_pcalloc(p, sizeof(magick_conf));
    magick_conf *add = (magick_conf *) addv;
    magick_conf *base = (magick_conf *) basev;

    new->size = (add->size_set == 0) ? base->size : add->size;
    new->size_set = add->size_set || base->size_set;

    new->options = apr_hash_overlay(p, add->options, base->options);

    return new;
}

static const char *set_magick_size(cmd_parms *cmd, void *dconf, const char *arg)
{
    magick_conf *conf = dconf;

    if (APR_SUCCESS != apr_strtoff(&(conf->size), arg, NULL, 10) || conf->size
            <= 0) {
        return "MagickMaxSize must be a size in bytes, and greater than zero";
    }
    conf->size_set = 1;

    return NULL;
}

static const char *add_magick_option(cmd_parms *cmd, void *dconf,
        const char *key, const char *value)
{
    magick_conf *conf = dconf;
    const char *expr_err = NULL;

    magick_option *option = apr_palloc(cmd->pool, sizeof(magick_option));

    option->key = strchr(key, ':');
    if (!option->key) {
        return apr_psprintf(cmd->pool, "Key '%s' needs a colon character", key);
    }
    option->format = apr_pstrndup(cmd->pool, key, option->key - key);
    option->key++;
     option->value = ap_expr_parse_cmd(cmd, value, AP_EXPR_FLAG_STRING_RESULT,
            &expr_err, NULL);

    if (expr_err) {
        return apr_pstrcat(cmd->temp_pool,
                "Cannot parse expression '", value, "': ",
                expr_err, NULL);
    }

    apr_hash_set(conf->options, key, APR_HASH_KEY_STRING, option);

    return NULL;
}

static const command_rec magick_cmds[] = {
    AP_INIT_TAKE1("MagickMaxSize", set_magick_size, NULL, ACCESS_CONF,
        "Maximum size of the image processed by the magick filter"),
    AP_INIT_TAKE2("AddMagickOption", add_magick_option, NULL, ACCESS_CONF,
        "Add key/value option to be used by the filter."), { NULL },
};

static apr_status_t magick_bucket_read(apr_bucket *b, const char **str,
                                       apr_size_t *len, apr_read_type_e block)
{
    ap_bucket_magick *m = b->data;

    if (m->wand) {
        m->base = (char *)MagickWriteImageBlob(m->wand,
                &b->length);
        m->alloc_len = b->length;
        DestroyMagickWand(m->wand);
        m->wand = NULL;

        /* morph into a magick heap bucket from now on */
        b->type = &ap_bucket_type_magick_heap;
    }

    *str = m->base + b->start;
    *len = b->length;
    return APR_SUCCESS;
}

static void magick_bucket_destroy(void *data)
{
    ap_bucket_magick *m = data;

    if (apr_bucket_shared_destroy(m)) {

        if (m->wand) {
            DestroyMagickWand(m->wand);
            m->wand = NULL;
        }

        if (m->base) {
            MagickRelinquishMemory((void *)m->base);
            m->base = NULL;
        }

        apr_bucket_free(m);
    }
}

AP_DECLARE_DATA const apr_bucket_type_t ap_bucket_type_magick = {
    "MAGICK", 5, APR_BUCKET_DATA,
    magick_bucket_destroy,
    magick_bucket_read,
    apr_bucket_setaside_noop, /* don't need to setaside thanks to the cleanup*/
    apr_bucket_shared_split,
    apr_bucket_shared_copy
};

AP_DECLARE_DATA const apr_bucket_type_t ap_bucket_type_magick_heap = {
    "MAGICK_HEAP", 5, APR_BUCKET_DATA,
    magick_bucket_destroy,
    magick_bucket_read,
    apr_bucket_setaside_noop, /* don't need to setaside thanks to the cleanup*/
    apr_bucket_shared_split,
    apr_bucket_shared_copy
};

AP_DECLARE(apr_bucket *) ap_bucket_magick_make(apr_bucket *b)
{
    ap_bucket_magick *m;

    m = apr_bucket_alloc(sizeof(*m), b->list);

    m->base = NULL;

    b = apr_bucket_shared_make(b, m, 0, -1);
    b->type = &ap_bucket_type_magick;

    /* pre-initialize heap bucket member */
    m->alloc_len = 0;
    m->base      = NULL;

    m->wand = NewMagickWand();

    return b;
}

AP_DECLARE(apr_bucket *) ap_bucket_magick_create(apr_bucket_alloc_t *list)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return ap_bucket_magick_make(b);
}

static int magick_set_option(void *ctx, const void *key, apr_ssize_t klen, const void *val)
{
    magick_do *mdo = ctx;
    const magick_option *option = val;

    const char *err = NULL;
    const char *str;

    str = ap_expr_str_exec(mdo->r, option->value, &err);
    if (err) {
        ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, mdo->r,
                        "Failure while evaluating the option value expression for '%s', "
                        "option ignored: %s", mdo->r->uri, err);
    }
    else {
        MagickSetImageOption(mdo->wand, option->format, option->key, str);
    }

    return 1;
}

static apr_status_t magick_out_filter(ap_filter_t *f, apr_bucket_brigade *bb)
{
    request_rec *r = f->r;
    magick_ctx *ctx = f->ctx;

    magick_conf *conf = ap_get_module_config(f->r->per_dir_config, &magick_module);

    apr_status_t rv = APR_SUCCESS;
    apr_size_t size;

    /* Do nothing if asked to filter nothing. */
    if (APR_BRIGADE_EMPTY(bb)) {
        return ap_pass_brigade(f->next, bb);
    }

    /* first time in? create a context */
    if (!ctx) {

    	ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));
        ctx->bb = apr_brigade_create(r->pool, f->c->bucket_alloc);
        ctx->mbb = apr_brigade_create(r->pool, f->c->bucket_alloc);

        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, f->r,
                "MAGICK filter enabled: %s", f->r->uri);
    }

    while (APR_SUCCESS == rv && !APR_BRIGADE_EMPTY(bb)) {
        const char *data;
        apr_bucket *e;

        e = APR_BRIGADE_FIRST(bb);

        /* EOS means we are done. */
        if (APR_BUCKET_IS_EOS(e)) {
            ctx->seen_eos = 1;
            break;
        }

        /* A flush takes precedence over buffering */
        if (APR_BUCKET_IS_FLUSH(e)) {

            /* pass the bucket across */
            APR_BUCKET_REMOVE(e);
            APR_BRIGADE_INSERT_TAIL(ctx->mbb, e);

            continue;
        }

        /* metadata buckets are preserved as is */
        if (APR_BUCKET_IS_METADATA(e)) {

            /* pass the bucket across */
            APR_BUCKET_REMOVE(e);
            APR_BRIGADE_INSERT_TAIL(ctx->mbb, e);

            continue;
        }

        if (APR_SUCCESS == (rv = apr_bucket_read(e, &data, &size,
                APR_BLOCK_READ))) {

            ctx->seen_bytes += size;
            if (ctx->seen_bytes > conf->size) {

                ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_ENOSPC, r,
                        "Response is too large (>%" APR_OFF_T_FMT
                        "), aborting request.", conf->size);
                return APR_ENOSPC;
            }

            /* pass the bucket across */
            APR_BUCKET_REMOVE(e);
            APR_BRIGADE_INSERT_TAIL(ctx->bb, e);

        }

    }

    if (ctx->seen_eos) {

        /* keep the metadata and flush buckets */
        APR_BRIGADE_PREPEND(bb, ctx->mbb);

        if (ctx->seen_bytes) {

            unsigned char *data;
            apr_bucket *e;
            ap_bucket_magick *m;
            magick_do mdo;

            /* insert wand bucket */
            e = ap_bucket_magick_create(r->connection->bucket_alloc);
            APR_BRIGADE_INSERT_HEAD(bb, e);

            m = e->data;

            data = MagickMalloc(ctx->seen_bytes);
            apr_brigade_flatten(ctx->bb, (char *) data, &ctx->seen_bytes);
            apr_brigade_cleanup(ctx->bb);

            /* pass flags needed to pass through parameters from the
             * original image.
             */
            mdo.r = f->r;
            mdo.wand = m->wand;

            apr_hash_do(magick_set_option, &mdo, conf->options);

            if (!MagickReadImageBlob(m->wand, data, ctx->seen_bytes)) {
                char *description;
                ExceptionType severity;

                description = MagickGetException(m->wand, &severity);
                ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, r,
                        "MagickReadImageBlob: %s (severity %d)", description,
                        severity);
                MagickRelinquishMemory(description);

                MagickFree(data);
                return APR_EGENERAL;
            }
            MagickFree(data);

        }

        /* pass what we have left down the chain */
        ap_remove_output_filter(f);
        return ap_pass_brigade(f->next, bb);
    }

    return rv;

}

static void register_hooks(apr_pool_t *p)
{
    ap_register_output_filter("MAGICK", magick_out_filter, NULL,
            AP_FTYPE_CONTENT_SET);
}

AP_DECLARE_MODULE(magick) =
{
    STANDARD20_MODULE_STUFF,
    create_magick_dir_config, /* dir config creater */
    merge_magick_dir_config,  /* dir merger --- default is to override */
    NULL,                     /* server config */
    NULL,                     /* merge server config */
    magick_cmds,              /* command apr_table_t */
    register_hooks            /* register hooks */
};
