/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
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
 */

#pragma once

/*
 * Speaks replies through OpenAI's speech API (CONFIG_MUSE_TTS_OPENAI_KEY),
 * since Muse doesn't speak gadget replies. A task of its own fetches the MP3;
 * the chat session reads it here and plays it like any other.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether a key is compiled in. */
bool muse_tts_configured(void);

/* Starts fetching text's speech, dropping any earlier one. False if not
 * configured or out of memory. */
bool muse_tts_start(const char *text);

/* Moves up to cap bytes of the MP3 that has arrived into buf. */
size_t muse_tts_read(uint8_t *buf, size_t cap);

/* True once the fetch has ended and everything it got has been read; *ok
 * says whether it all arrived. */
bool muse_tts_finished(bool *ok);

/* Drops the current fetch, if any. */
void muse_tts_cancel(void);

#ifdef __cplusplus
}
#endif
