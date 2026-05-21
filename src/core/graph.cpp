#include "calc/calculator.hpp"

#include <cmath>

namespace calc {

bool graph_to_screen(const GraphWindow& window, CalcReal x, CalcReal y, int& sx, int& sy) {
    const CalcReal w = window.xmax - window.xmin;
    const CalcReal h = window.ymax - window.ymin;
    if (w == 0.0 || h == 0.0 || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }

    const CalcReal px = (x - window.xmin) * static_cast<CalcReal>(kLcdWidth - 1) / w;
    const CalcReal py = (window.ymax - y) * static_cast<CalcReal>(kLcdHeight - 1) / h;
    if (px < -32768.0 || px > 32767.0 || py < -32768.0 || py > 32767.0) {
        return false;
    }

    sx = static_cast<int>(px + 0.5);
    sy = static_cast<int>(py + 0.5);
    return sx >= 0 && sx < kLcdWidth && sy >= 0 && sy < kLcdHeight;
}

CalcReal screen_to_graph_x(const GraphWindow& window, int sx) {
    return window.xmin + (window.xmax - window.xmin) * static_cast<CalcReal>(sx) /
                             static_cast<CalcReal>(kLcdWidth - 1);
}

CalcReal screen_to_graph_y(const GraphWindow& window, int sy) {
    return window.ymax - (window.ymax - window.ymin) * static_cast<CalcReal>(sy) /
                             static_cast<CalcReal>(kLcdHeight - 1);
}

}  // namespace calc
