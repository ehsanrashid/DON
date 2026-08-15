// Standalone NNUE embedding for universal binary builds

#include "../evaluate.h"

extern const unsigned char gNNUEEmbeddedData[] = {
#embed EvalFileDefaultName
};
extern const unsigned int gNNUEEmbeddedSize = sizeof(gNNUEEmbeddedData);
