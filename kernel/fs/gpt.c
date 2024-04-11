#include <fs/gpt.h>

int gpt_validate(struct GPT *gpt) {
    if (gpt->signature != GPT_SIGNATURE) {
        return -1;
    }
    return 0;
}
