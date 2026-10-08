#include "ersa/board/board.h"

namespace ersa {
namespace board {

// The selected BSP provides this reference at link time. The board contract is
// therefore available before peripheral initialization begins.
#if defined(ARDUINO)
Board& boardImplementation();
#else
static Board* s_currentBoard = nullptr;
#endif

Board& Board::current() {
#if defined(ARDUINO)
    return boardImplementation();
#else
    return *s_currentBoard;
#endif
}

Board* Board::currentOrNull() {
#if defined(ARDUINO)
    return &boardImplementation();
#else
    return s_currentBoard;
#endif
}

} // namespace board
} // namespace ersa
