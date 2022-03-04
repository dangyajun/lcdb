/*!
 * bloom.c - bloom filter for rdb
 * Copyright (c) 2022, Christopher Jeffrey (MIT License).
 * https://github.com/chjj/rdb
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bloom.h"
#include "buffer.h"
#include "hash.h"
#include "internal.h"
#include "slice.h"

/*
 * Default
 */

static void
bloom_build(const rdb_bloom_t *bloom,
            rdb_buffer_t *dst,
            const rdb_slice_t *keys,
            size_t length);

static int
bloom_match(const rdb_bloom_t *bloom,
            const rdb_slice_t *filter,
            const rdb_slice_t *key);

static const rdb_bloom_t bloom_default = {
  /* .name = */ "leveldb.BuiltinBloomFilter2",
  /* .build = */ bloom_build,
  /* .match = */ bloom_match,
  /* .bits_per_key = */ 10,
  /* .k = */ 6, /* (size_t)(10 * 0.69) == 6 */
  /* .user_policy = */ NULL
};

/*
 * Globals
 */

const rdb_bloom_t *rdb_bloom_default = &bloom_default;

/*
 * Bloom
 */

rdb_bloom_t *
rdb_bloom_create(int bits_per_key) {
  rdb_bloom_t *bloom = rdb_malloc(sizeof(rdb_bloom_t));
  rdb_bloom_init(bloom, bits_per_key);
  return bloom;
}

void
rdb_bloom_destroy(rdb_bloom_t *bloom) {
  rdb_free(bloom);
}

void
rdb_bloom_init(rdb_bloom_t *bloom, int bits_per_key) {
  /* We intentionally round down to reduce probing cost a little bit. */
  bloom->name = bloom_default.name;
  bloom->build = bloom_default.build;
  bloom->match = bloom_default.match;
  bloom->bits_per_key = bits_per_key;
  bloom->k = bits_per_key * 0.69; /* 0.69 =~ ln(2). */
  bloom->user_policy = NULL;

  if (bloom->k < 1)
    bloom->k = 1;

  if (bloom->k > 30)
    bloom->k = 30;
}

int
rdb_bloom_name(char *buf, size_t size, const rdb_bloom_t *bloom) {
  const char *name = bloom->name;
  size_t len = strlen(name);

  if (7 + len + 1 > size)
    return 0;

  memcpy(buf + 0, "filter.", 7);
  memcpy(buf + 7, name, len + 1);

  return 1;
}

static uint32_t
bloom_hash(const rdb_slice_t *key) {
  return rdb_hash(key->data, key->size, 0xbc9f1d34);
}

static size_t
bloom_size(const rdb_bloom_t *bloom, size_t n) {
  /* Compute bloom filter size (in both bits and bytes). */
  size_t bits = n * bloom->bits_per_key;

  /* For small n, we can see a very high false positive rate.
     Fix it by enforcing a minimum bloom filter length. */
  if (bits < 64)
    bits = 64;

  return (bits + 7) / 8;
}

static void
bloom_add(const rdb_bloom_t *bloom,
          uint8_t *data,
          const rdb_slice_t *key,
          size_t bits) {
  /* Use double-hashing to generate a sequence of hash values.
     See analysis in [Kirsch,Mitzenmacher 2006]. */
  uint32_t hash = bloom_hash(key);
  uint32_t delta = (hash >> 17) | (hash << 15); /* Rotate right 17 bits. */
  size_t i;

  for (i = 0; i < bloom->k; i++) {
    uint32_t pos = hash % bits;

    data[pos / 8] |= (1 << (pos % 8));

    hash += delta;
  }
}

static void
bloom_build(const rdb_bloom_t *bloom,
            rdb_buffer_t *dst,
            const rdb_slice_t *keys,
            size_t length) {
  size_t bytes = bloom_size(bloom, length);
  size_t bits = bytes * 8;
  uint8_t *data;
  size_t i;

  data = rdb_buffer_pad(dst, bytes + 1);

  for (i = 0; i < length; i++)
    bloom_add(bloom, data, &keys[i], bits);

  data[bytes] = bloom->k; /* Remember # of probes in filter. */
}

static int
bloom_match(const rdb_bloom_t *bloom,
            const rdb_slice_t *filter,
            const rdb_slice_t *key) {
  const uint8_t *data = filter->data;
  size_t len = filter->size;
  uint32_t hash, delta;
  size_t i, bits, k;

  (void)bloom;

  if (len < 2)
    return 0;

  bits = (len - 1) * 8;

  /* Use the encoded k so that we can read filters generated by
     bloom filters created using different parameters. */
  k = data[len - 1];

  if (k > 30) {
    /* Reserved for potentially new encodings for short bloom
       filters. Consider it a match. */
    return 1;
  }

  hash = bloom_hash(key);
  delta = (hash >> 17) | (hash << 15); /* Rotate right 17 bits. */

  for (i = 0; i < k; i++) {
    uint32_t pos = hash % bits;

    if ((data[pos / 8] & (1 << (pos % 8))) == 0)
      return 0;

    hash += delta;
  }

  return 1;
}
