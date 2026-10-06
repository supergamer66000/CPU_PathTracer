//
// Created by super on 30/12/2025.
//

#ifndef CONTEXT_H
#define CONTEXT_H

#include <embree4/rtcore.h>

namespace cpt {
    struct EngineContext {
        RTCDevice device;

        EngineContext(RTCDevice device) :
            device(device) {}
    };
}

#endif //CONTEXT_H
