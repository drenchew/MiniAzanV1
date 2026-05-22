#include "SpiArchitecture.h"

namespace SpiArch {

SPIClass& sdSpi() {
    static SPIClass bus(VSPI);
    return bus;
}

SPIClass& uiSpi() {
    static SPIClass bus(HSPI);
    return bus;
}

}  // namespace SpiArch
