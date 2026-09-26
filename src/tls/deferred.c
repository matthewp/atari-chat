#include <stdio.h>
#include <string.h>
#include <osbind.h>
#include "deferred.h"
#include "trust_anchors.h"
#include "rsa/rsa16.h"

/* The one connection in progress. The RSA stand-in has no context argument. */
static br_ssl_client_context *client;
static deferred_x509_context *current;

/* What the stand-in recorded about the key-exchange signature. */
static struct {
    unsigned char sig[512];
    size_t sig_len;
    const unsigned char *hash_oid;
    unsigned char hash[32];
    int recorded;
} ske;

/* ---- the stand-in X.509 validator ---------------------------------------- */

static void x_start_chain(const br_x509_class **vt, const char *server_name)
{
    deferred_x509_context *ctx = (deferred_x509_context *)vt;

    ctx->ncerts = 0;
    ctx->used = 0;
    ctx->overflow = 0;
    strncpy(ctx->server_name, server_name ? server_name : "", sizeof(ctx->server_name) - 1);
    ctx->server_name[sizeof(ctx->server_name) - 1] = '\0';
}

static void x_start_cert(const br_x509_class **vt, uint32_t length)
{
    deferred_x509_context *ctx = (deferred_x509_context *)vt;

    if (ctx->ncerts >= 8 || ctx->used + length > sizeof(ctx->certs)) {
        ctx->overflow = 1;
        return;
    }
    ctx->cert_start[ctx->ncerts] = ctx->used;
    ctx->cert_len[ctx->ncerts] = length;
    if (ctx->ncerts == 0)
        br_x509_decoder_init(&ctx->leaf, NULL, NULL);
}

static void x_append(const br_x509_class **vt, const unsigned char *buf, size_t len)
{
    deferred_x509_context *ctx = (deferred_x509_context *)vt;

    if (ctx->overflow)
        return;
    memcpy(ctx->certs + ctx->used, buf, len);
    ctx->used += len;
    if (ctx->ncerts == 0)
        br_x509_decoder_push(&ctx->leaf, buf, len);
}

static void x_end_cert(const br_x509_class **vt)
{
    deferred_x509_context *ctx = (deferred_x509_context *)vt;

    if (!ctx->overflow)
        ctx->ncerts++;
}

static unsigned x_end_chain(const br_x509_class **vt)
{
    deferred_x509_context *ctx = (deferred_x509_context *)vt;
    int err;

    if (ctx->overflow)
        return BR_ERR_X509_LIMIT_EXCEEDED;
    if (ctx->ncerts == 0)
        return BR_ERR_X509_EMPTY_CHAIN;
    err = br_x509_decoder_last_error(&ctx->leaf);
    if (err)
        return err;
    if (br_x509_decoder_get_pkey(&ctx->leaf) == NULL)
        return BR_ERR_X509_UNSUPPORTED;
    return 0;
}

static const br_x509_pkey *x_get_pkey(const br_x509_class *const *vt, unsigned *usages)
{
    deferred_x509_context *ctx = (deferred_x509_context *)vt;

    if (usages)
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return br_x509_decoder_get_pkey(&ctx->leaf);
}

const br_x509_class deferred_x509_vtable = {
    sizeof(deferred_x509_context),
    x_start_chain,
    x_start_cert,
    x_append,
    x_end_cert,
    x_end_chain,
    x_get_pkey
};

void deferred_init(deferred_x509_context *ctx, br_ssl_client_context *cc)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->vtable = &deferred_x509_vtable;
    memset(&ske, 0, sizeof(ske));
    client = cc;
    current = ctx;
}

/* ---- the stand-in RSA verifier ------------------------------------------ */

/*
 * The hash BearSSL signs over for an ECDHE ServerKeyExchange (see
 * verify_SKE_sig() in ssl_hs_client.c). We only support SHA-256.
 */
static void ske_hash(unsigned char out[32])
{
    br_ssl_engine_context *eng = &client->eng;
    br_sha256_context h;
    unsigned char head[4];

    head[0] = 3;                            /* named_curve */
    head[1] = 0;
    head[2] = eng->ecdhe_curve;
    head[3] = eng->ecdhe_point_len;
    br_sha256_init(&h);
    br_sha256_update(&h, eng->client_random, sizeof(eng->client_random));
    br_sha256_update(&h, eng->server_random, sizeof(eng->server_random));
    br_sha256_update(&h, head, sizeof(head));
    br_sha256_update(&h, eng->ecdhe_point, eng->ecdhe_point_len);
    br_sha256_out(&h, out);
}

uint32_t deferred_rsa_vrfy(const unsigned char *x, size_t xlen,
                           const unsigned char *hash_oid, size_t hash_len,
                           const br_rsa_public_key *pk, unsigned char *hash_out)
{
    (void)pk;   /* it's the leaf's key; deferred_verify() re-fetches it */

    if (hash_len != 32 || xlen > sizeof(ske.sig) || client == NULL)
        return 0;
    memcpy(ske.sig, x, xlen);
    ske.sig_len = xlen;
    ske.hash_oid = hash_oid;
    ske_hash(ske.hash);
    ske.recorded = 1;
    memcpy(hash_out, ske.hash, 32);
    return 1;
}

/* ---- the real checks ----------------------------------------------------- */

/* Days since 1 Jan 0 AD (proleptic Gregorian), as br_x509_minimal wants. */
static uint32_t days_since_year0(int y, int m, int d)
{
    static const int cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    uint32_t days = (uint32_t)y * 365 + y / 4 - y / 100 + y / 400;
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;

    /* The formula above counts year y's own leap day; remove it for Jan/Feb. */
    if (leap && m <= 2)
        days--;
    return days + cum[m - 1] + (d - 1) + 1;
}

/* ---- cache of verified chains ------------------------------------------- */

/*
 * Each entry says: this exact leaf certificate (by SHA-256 of its DER bytes)
 * was fully verified up to a trust anchor for this host name, and is valid
 * between these dates. A later connection presenting the same certificate
 * can skip the chain check. The key-exchange signature is still checked
 * every time: that is what ties the connection to the certificate.
 */
#define CACHE_FILE "TLSCACHE.DAT"
#define CACHE_MAGIC 0x41434331UL    /* "ACC1" */
#define CACHE_SLOTS 8

typedef struct {
    char host[64];
    unsigned char leaf_hash[32];
    uint32_t not_before_days, not_after_days;
} cache_entry;

typedef struct {
    uint32_t magic;
    cache_entry entry[CACHE_SLOTS];
} cache_file;

static cache_file cache;

static void cache_load(void)
{
    FILE *f = fopen(CACHE_FILE, "rb");

    memset(&cache, 0, sizeof(cache));
    if (f) {
        if (fread(&cache, sizeof(cache), 1, f) != 1 || cache.magic != CACHE_MAGIC)
            memset(&cache, 0, sizeof(cache));
        fclose(f);
    }
    cache.magic = CACHE_MAGIC;
}

static void cache_save(void)
{
    FILE *f = fopen(CACHE_FILE, "wb");

    if (f) {
        fwrite(&cache, sizeof(cache), 1, f);
        fclose(f);
    }
}

static void leaf_hash(deferred_x509_context *ctx, unsigned char out[32])
{
    br_sha256_context h;

    br_sha256_init(&h);
    br_sha256_update(&h, ctx->certs + ctx->cert_start[0], ctx->cert_len[0]);
    br_sha256_out(&h, out);
}

/* Is this leaf already verified for this host, and valid today? */
static int cache_lookup(deferred_x509_context *ctx, uint32_t today)
{
    unsigned char hash[32];
    int i;

    cache_load();
    leaf_hash(ctx, hash);
    for (i = 0; i < CACHE_SLOTS; i++) {
        cache_entry *e = &cache.entry[i];
        if (strcmp(e->host, ctx->server_name) == 0
            && memcmp(e->leaf_hash, hash, 32) == 0)
            return today >= e->not_before_days && today <= e->not_after_days;
    }
    return 0;
}

static void cache_store(deferred_x509_context *ctx)
{
    cache_entry *e = NULL;
    int i;

    cache_load();
    /* Replace this host's old entry, else use an empty slot, else slot 0. */
    for (i = 0; i < CACHE_SLOTS && !e; i++)
        if (strcmp(cache.entry[i].host, ctx->server_name) == 0)
            e = &cache.entry[i];
    for (i = 0; i < CACHE_SLOTS && !e; i++)
        if (cache.entry[i].host[0] == '\0')
            e = &cache.entry[i];
    if (!e)
        e = &cache.entry[0];

    memset(e, 0, sizeof(*e));
    memcpy(e->host, ctx->server_name, strlen(ctx->server_name));  /* < 64, see start_chain */
    leaf_hash(ctx, e->leaf_hash);
    e->not_before_days = ctx->leaf.notbefore_days;
    e->not_after_days = ctx->leaf.notafter_days;
    cache_save();
}

int deferred_verify(deferred_x509_context *ctx, deferred_log_fn log)
{
    static br_x509_minimal_context xc;
    const br_x509_pkey *pk;
    br_rsa_public_key rsa;
    unsigned char recovered[32];
    /* Mask: TOS returns these as 16-bit values that sign-extend after 16:00. */
    unsigned date = Tgetdate() & 0xffff, time = Tgettime() & 0xffff;
    uint32_t today;
    unsigned err;
    int i;

    if (!ske.recorded)
        return BR_ERR_BAD_SIGNATURE;

    /* An ST without a battery clock boots in the past. Certificate dates
       are meaningless then, so refuse rather than guess. */
    if (1980 + (date >> 9) < 2025)
        return BR_ERR_X509_TIME_UNKNOWN;
    today = days_since_year0(1980 + (date >> 9), (date >> 5) & 15, date & 31);

    /* 1. The key-exchange signature, with the leaf certificate's key. */
    log("Checking key exchange signature...");
    pk = br_x509_decoder_get_pkey(&ctx->leaf);
    if (pk == NULL || pk->key_type != BR_KEYTYPE_RSA)
        return BR_ERR_WRONG_KEY_USAGE;
    rsa = pk->key.rsa;
    if (!rsa16_pkcs1_vrfy(ske.sig, ske.sig_len, ske.hash_oid, 32, &rsa, recovered)
        || memcmp(recovered, ske.hash, 32) != 0)
        return BR_ERR_BAD_SIGNATURE;

    /* 2. The certificate chain, up to a trust anchor, for this host name. */
    if (cache_lookup(ctx, today)) {
        log("Certificate chain verified before (cached).");
        return 0;
    }
    log("Checking certificate chain...");
    br_x509_minimal_init(&xc, &br_sha256_vtable, TAs, TAs_NUM);
    br_x509_minimal_set_hash(&xc, br_sha1_ID, &br_sha1_vtable);
    br_x509_minimal_set_hash(&xc, br_sha256_ID, &br_sha256_vtable);
    br_x509_minimal_set_hash(&xc, br_sha384_ID, &br_sha384_vtable);
    br_x509_minimal_set_hash(&xc, br_sha512_ID, &br_sha512_vtable);
    br_x509_minimal_set_rsa(&xc, rsa16_pkcs1_vrfy);
    br_x509_minimal_set_time(&xc, today,
        (time >> 11) * 3600 + ((time >> 5) & 63) * 60 + (time & 31) * 2);

    xc.vtable->start_chain(&xc.vtable, ctx->server_name);
    for (i = 0; i < ctx->ncerts; i++) {
        xc.vtable->start_cert(&xc.vtable, ctx->cert_len[i]);
        xc.vtable->append(&xc.vtable, ctx->certs + ctx->cert_start[i], ctx->cert_len[i]);
        xc.vtable->end_cert(&xc.vtable);
    }
    err = xc.vtable->end_chain(&xc.vtable);
    if (err)
        return (int)err;

    /* 3. The key the chain vouches for must be the one used in the handshake. */
    {
        const br_x509_pkey *vouched = xc.vtable->get_pkey(&xc.vtable, NULL);
        if (vouched == NULL || vouched->key_type != BR_KEYTYPE_RSA
            || vouched->key.rsa.nlen != rsa.nlen
            || memcmp(vouched->key.rsa.n, rsa.n, rsa.nlen) != 0)
            return BR_ERR_X509_BAD_SERVER_NAME;
    }

    cache_store(ctx);
    return 0;
}
