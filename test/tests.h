/*!
 * tests.h - test utilities for lcdb
 * Copyright (c) 2022, Christopher Jeffrey (MIT License).
 * https://github.com/chjj/lcdb
 *
 * Parts of this software are based on google/leveldb:
 *   Copyright (c) 2011, The LevelDB Authors. All rights reserved.
 *   https://github.com/google/leveldb
 *
 * See LICENSE for more information.
 */

#ifndef LDB_TESTS_H
#define LDB_TESTS_H

/*
 * Assertions
 */

#undef ASSERT

#define ASSERT(expr) do {                       \
  if (!(expr))                                  \
    ldb_assert_fail(__FILE__, __LINE__, #expr); \
} while (0)

/*
 * Functions
 */

void
ldb_assert_fail(const char *file, int line, const char *expr);

#endif /* LDB_TESTS_H */
