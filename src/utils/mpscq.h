
/*
 *
 * Copyright 2016 gRPC authors.
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
 * @file mpscq.h
 * @brief Lock-free multi-producer single-consumer queue.
 */

#pragma once

#include "src/utils/atomic.h"

/**
 * @brief Lock-free multi-producer single-consumer queue.
 *
 * Supports concurrent push from multiple threads but restricts pop to a
 * single consumer thread. Derived from the gRPC implementation.
 */
class MultiProducerSingleConsumerQueue final {
public:
    /** @brief Intrusive queue node. Embed this in your data structure. */
    struct Node {
        /** @brief Pointer to the next node in the queue. */
        Atomic<Node *> next;
    };

    /** @brief Constructor. Initializes the queue with a stub sentinel node. */
    MultiProducerSingleConsumerQueue();

    /** @brief Destructor. Asserts the queue is empty. */
    ~MultiProducerSingleConsumerQueue();

    /**
     * @brief Pushes a node onto the queue (thread-safe for multiple producers).
     * @param node The node to enqueue.
     * @return True if this was the first element pushed (useful for wake-up signaling).
     */
    bool push(Node *node);

    /**
     * @brief Pops the oldest node from the queue (single consumer only).
     * @return The dequeued node, or nullptr if the queue is empty.
     */
    Node *pop();

    /**
     * @brief Pops the oldest node and reports whether the queue is now empty.
     * @param empty Output flag set to true if the queue was empty before this pop.
     * @return The dequeued node, or nullptr if the queue was empty or in a transitional state.
     */
    Node *PopAndCheckEnd(bool *empty);

private:
    union {
        char _padding[64];
        Atomic<Node *> _newest;
    };
    Node *_oldest;
    Node _stub;
};
