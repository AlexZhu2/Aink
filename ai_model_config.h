#ifndef AI_MODEL_CONFIG_H
#define AI_MODEL_CONFIG_H

#include <stddef.h>

typedef enum {
  AI_PROVIDER_OPENAI = 0,
  AI_PROVIDER_GEMINI = 1,
  AI_PROVIDER_KIMI = 2,
  AI_PROVIDER_MIMO = 3,
  AI_PROVIDER_COUNT,
} AiProvider;

const char *ai_provider_name(AiProvider provider);
int ai_provider_model_count(AiProvider provider);
const char *ai_provider_model_id(AiProvider provider, int modelIndex);
const char *ai_provider_model_label(AiProvider provider, int modelIndex);

/** OpenAI-compatible POST /chat/completions URL; empty if provider uses another API. */
const char *ai_provider_chat_completions_url(AiProvider provider);
bool ai_provider_supports_vision(AiProvider provider);
bool ai_provider_model_supports_vision(AiProvider provider, int modelIndex);

#endif
