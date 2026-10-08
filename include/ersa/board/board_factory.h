#pragma once

#include "ersa/board/board.h"

namespace ersa {
namespace board {

class BoardFactory {
public:
    static Board& selected();
};

} // namespace board
} // namespace ersa
