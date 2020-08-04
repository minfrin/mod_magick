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
 *     </If>
 *   </IfModule>
 * </Location>
 *
 */

#include <apr.h>
#include <apr_hash.h>
#include <apr_strings.h>

#include "httpd.h"
#include "http_config.h"
#include "http_log.h"

#include "mod_magick.h"

module AP_MODULE_DECLARE_DATA magick_module;

#define DEFAULT_MAX_SIZE 10*1024*1024

typedef struct magick_conf {
    int recipe_set:1; /* has the recipe been set */
    int size_set:1; /* has the size been set */
    apr_off_t size; /* maximum image size */
    const char *recipe; /* default recipe (if any) */
    apr_hash_t *recipes; /* recipes by name */
} magick_conf;

typedef struct magick_ctx {
    apr_bucket_brigade *bb;
    apr_bucket_brigade *tmp;
    MagickWand *wand;
    apr_off_t seen_bytes;
    int seen_buckets;
    int seen_eos;
} magick_ctx;


static void *create_magick_dir_config(apr_pool_t *p, char *dummy)
{
    magick_conf *new = (magick_conf *) apr_pcalloc(p, sizeof(magick_conf));

    new->size = DEFAULT_MAX_SIZE;
    new->recipes = apr_hash_make(p);

    return (void *) new;
}

static void *merge_magick_dir_config(apr_pool_t *p, void *basev, void *addv)
{
    magick_conf *new = (magick_conf *) apr_pcalloc(p, sizeof(magick_conf));
    magick_conf *add = (magick_conf *) addv;
    magick_conf *base = (magick_conf *) basev;

    new->size = (add->size_set == 0) ? base->size : add->size;
    new->size_set = add->size_set || base->size_set;

    new->recipe = (add->recipe_set == 0) ? base->recipe : add->recipe;
    new->recipe_set = add->recipe_set || base->recipe_set;

    new->recipes = apr_hash_overlay(p, add->recipes, base->recipes);

    return new;
}

static const char *add_magick_recipe(cmd_parms *cmd, void *dconf, const char *name,
        const char *value)
{
    magick_conf *conf = dconf;

    apr_hash_set(conf->recipes, name, APR_HASH_KEY_STRING, value);

    return NULL;
}

static const char *set_magick_recipe(cmd_parms *cmd, void *dconf, const char *name)
{
    magick_conf *conf = dconf;

    conf->recipe = name;
    conf->recipe_set = 1;

    return NULL;
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

static const command_rec magick_cmds[] = {
    AP_INIT_TAKE2("AddMagickRecipe", add_magick_recipe, NULL, ACCESS_CONF,
        "Add the named recipe to be used by the filter"), { NULL },
    AP_INIT_TAKE1("SetMagickRecipe", set_magick_recipe, NULL, ACCESS_CONF,
        "Set the default recipe to be used by the filter"), { NULL },
    AP_INIT_TAKE1("MagickMaxSize", set_magick_size, NULL, ACCESS_CONF,
                "Maximum size of the image processed by the magick filter")
};

static apr_status_t magick_bucket_cleanup(void *data)
{
    ap_bucket_magick *m = data;

    /*
     * If the pool gets cleaned up, we have to copy the data out
     * of the pool and onto the heap.  But the apr_buckets out there
     * that point to this magick bucket need to be notified such that
     * they can morph themselves into a regular heap bucket the next
     * time they try to read.  To avoid having to manipulate
     * reference counts and b->data pointers, the ap_bucket_magick
     * actually _contains_ an apr_bucket_heap as its first element,
     * so the two share their apr_bucket_refcount member, and you
     * can typecast a magick bucket struct to make it look like a
     * regular old heap bucket struct.
     */
    m->heap.base = apr_bucket_alloc(m->heap.alloc_len, m->list);
    memcpy(m->heap.base, m->base, m->heap.alloc_len);

    if (m->base) {
        MagickRelinquishMemory((void *)m->base);
        m->base = NULL;
    }

    m->wand = NULL;
    m->pool = NULL;

    return APR_SUCCESS;
}

static apr_status_t magick_bucket_read(apr_bucket *b, const char **str,
                                       apr_size_t *len, apr_read_type_e block)
{
    ap_bucket_magick *m = b->data;
    const char *base = m->base;

    if (m->wand) {
        m->base = base = (const char *)MagickWriteImageBlob(m->wand,
                &b->length);
        m->heap.alloc_len = b->length;
        m->wand = NULL;
    }

    if (m->pool == NULL) {
        /*
         * pool has been cleaned up... masquerade as a heap bucket from now
         * on. subsequent bucket operations will use the heap bucket code.
         */
        b->type = &apr_bucket_type_heap;
        base = m->heap.base;
    }
    *str = base + b->start;
    *len = b->length;
    return APR_SUCCESS;
}

static void magick_bucket_destroy(void *data)
{
    ap_bucket_magick *m = data;

    /* If the pool is cleaned up before the last reference goes
     * away, the data is really now on the heap; heap_destroy() takes
     * over.  free() in heap_destroy() thinks it's freeing
     * an apr_bucket_heap, when in reality it's freeing the whole
     * apr_bucket_magick for us.
     */
    if (m->pool) {
        /* the shared resource is still in the pool
         * because the pool has not been cleaned up yet
         */
        if (apr_bucket_shared_destroy(m)) {

            if (m->base) {
                MagickRelinquishMemory((void *)m->base);
            }

            apr_pool_cleanup_kill(m->pool, m, magick_bucket_cleanup);
            apr_bucket_free(m);
        }
    }
    else {
        /* the shared resource is no longer in the pool, it's
         * on the heap, but this reference still thinks it's a pool
         * bucket.  we should just go ahead and pass control to
         * heap_destroy() for it since it doesn't know any better.
         */
        apr_bucket_type_heap.destroy(m);
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

AP_DECLARE(apr_bucket *) ap_bucket_magick_make(apr_bucket *b, MagickWand *wand,
                                               apr_pool_t *p)
{
    ap_bucket_magick *m;

    m = apr_bucket_alloc(sizeof(*m), b->list);

    m->base = NULL;
    m->wand = wand;
    m->pool = p;
    m->list = b->list;

    b = apr_bucket_shared_make(b, m, 0, -1);
    b->type = &ap_bucket_type_magick;

    /* pre-initialize heap bucket member */
    m->heap.alloc_len = 0;
    m->heap.base      = NULL;
    m->heap.free_func = apr_bucket_free;

    apr_pool_cleanup_register(m->pool, m, magick_bucket_cleanup,
                              apr_pool_cleanup_null);
    return b;
}

AP_DECLARE(apr_bucket *) ap_bucket_magick_create(apr_bucket_alloc_t *list,
                                                 MagickWand *wand,
                                                 apr_pool_t *p)
{
    apr_bucket *b = apr_bucket_alloc(sizeof(*b), list);

    APR_BUCKET_INIT(b);
    b->free = apr_bucket_free;
    b->list = list;
    return ap_bucket_magick_make(b, wand, p);
}

static apr_status_t magick_wand_cleanup(void *data)
{
    MagickWand *wand = data;
    DestroyMagickWand(wand);
    return APR_SUCCESS;
}

static apr_status_t magick_out_filter(ap_filter_t *f, apr_bucket_brigade *bb)
{
    request_rec *r = f->r;
    magick_ctx *ctx = f->ctx;

    magick_conf *conf = ap_get_module_config(f->r->per_dir_config, &magick_module);

    apr_status_t rv = APR_SUCCESS;
    apr_size_t size;

    /* first time in? create a context */
    if (!ctx) {
        ctx = f->ctx = apr_pcalloc(r->pool, sizeof(*ctx));
        ctx->bb = apr_brigade_create(r->pool, f->c->bucket_alloc);
    }

    /* Do nothing if asked to filter nothing. */
    if (APR_BRIGADE_EMPTY(bb)) {
        return ap_pass_brigade(f->next, bb);
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

            /* flush makes no sense */
            apr_bucket_delete(e);

            continue;
        }

        /* metadata buckets are preserved as is */
        if (APR_BUCKET_IS_METADATA(e)) {

            /* metadata makes no sense */
            apr_bucket_delete(e);

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
        const unsigned char *data;
        apr_bucket *e;

        apr_brigade_pflatten(ctx->bb, (char **)&data, &size, r->pool);

        ctx->wand = NewMagickWand();

        apr_pool_cleanup_register(r->pool, (void *) ctx->wand, magick_wand_cleanup,
                magick_wand_cleanup);

        if (!MagickReadImageBlob(ctx->wand, data, size)) {
            char *description;
            ExceptionType severity;

            description = MagickGetException(ctx->wand, &severity);
            ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, r,
                    "MagickReadImageBlob: %s (severity %d)", description,
                    severity);
            MagickRelinquishMemory(description);

            return APR_EGENERAL;
        }

        /* insert wand bucket */
        e = ap_bucket_magick_create(r->connection->bucket_alloc, ctx->wand,
                r->pool);
        APR_BRIGADE_INSERT_HEAD(bb, e);

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
