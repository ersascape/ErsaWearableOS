#include "ersa/board/board_factory.h"
#include "sdkconfig.h"

#if defined(CONFIG_ERSA_BSP_AMPERE_TERRA)
#include "bsp/ampere/xiao_esp32c3/terra/board_terra.h"
#endif

namespace ersa {
namespace board {

Board& BoardFactory::selected() {
#if defined(CONFIG_ERSA_BSP_AMPERE_TERRA)
    return BoardTerra::instance();
#else
#error "No Ersa BSP is selected in Kconfig"
#endif
}

} // namespace board
} // namespace ersa
