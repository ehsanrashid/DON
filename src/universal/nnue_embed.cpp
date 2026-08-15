// Standalone NNUE embedding for universal binary builds

#include "../evaluate.h"

extern const unsigned char gEmbeddedData[] = {
#embed EvalFileDefaultName
};
extern const unsigned int gEmbeddedSize = sizeof(gEmbeddedData);
