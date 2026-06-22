/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/**
 * @file murmur_hash.h
 * @brief MurmurHash3 (32-bit) hash function declaration.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Computes a 32-bit MurmurHash3 hash of the given key.
 * @param key Pointer to the data to hash.
 * @param len Length of the key in bytes.
 * @param seed Seed value for the hash function.
 * @return The 32-bit hash value.
 */
uint32_t murmur_hash3(const void* key, size_t len, uint32_t seed);
